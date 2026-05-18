#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>
#include <atomic>
#include "vulkan_context.h"

#undef LOG_TAG
#define LOG_TAG "NativeCamera"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

struct AppEngine {
    struct android_app* app;
    std::atomic<bool> isRendering{false};
    ACameraManager* cameraManager;
    ACameraIdList* cameraIdList;
    ACameraDevice* cameraDevice;
    ACaptureSessionOutputContainer* outputContainer;
    ACaptureSessionOutput* sessionOutput;
    ACameraOutputTarget* outputTarget;
    ACaptureRequest* captureRequest;
    ACameraCaptureSession* captureSession;
    const char* cameraId;
    VulkanContext* vulkan;
    AImageReader* imageReader;
    ANativeWindow* imageReaderWindow;
    AImageReader_ImageListener imageListener;
};

static bool checkCameraPermission(struct android_app* app) {
    JNIEnv* env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    jclass activityClass = env->GetObjectClass(app->activity->clazz);
    jmethodID checkMethod = env->GetMethodID(activityClass, "checkSelfPermission", "(Ljava/lang/String;)I");
    jstring permission = env->NewStringUTF("android.permission.CAMERA");
    jint result = env->CallIntMethod(app->activity->clazz, checkMethod, permission);
    env->DeleteLocalRef(permission);
    env->DeleteLocalRef(activityClass);
    app->activity->vm->DetachCurrentThread();
    return result == 0;
}

static void requestCameraPermission(struct android_app* app) {
    JNIEnv* env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    jclass activityClass = env->GetObjectClass(app->activity->clazz);
    jmethodID requestMethod = env->GetMethodID(activityClass, "requestPermissions", "([Ljava/lang/String;I)V");
    jobjectArray permissions = env->NewObjectArray(1, env->FindClass("java/lang/String"), env->NewStringUTF("android.permission.CAMERA"));
    env->CallVoidMethod(app->activity->clazz, requestMethod, permissions, 1);
    env->DeleteLocalRef(permissions);
    env->DeleteLocalRef(activityClass);
    app->activity->vm->DetachCurrentThread();
}

static void onDeviceDisconnected(void* context, ACameraDevice* device) {
    LOGI("Camera Device %s disconnected", ACameraDevice_getId(device));
}

static void onDeviceError(void* context, ACameraDevice* device, int error) {
    LOGE("Camera Device %s error: %d", ACameraDevice_getId(device), error);
}

static ACameraDevice_StateCallbacks deviceCallbacks = {
    .context = nullptr,
    .onDisconnected = onDeviceDisconnected,
    .onError = onDeviceError,
};

static void onSessionActive(void* context, ACameraCaptureSession* session) {
    LOGI("Session is now active");
}

static void onSessionReady(void* context, ACameraCaptureSession* session) {
    LOGI("Session is now ready");
}

static void onSessionClosed(void* context, ACameraCaptureSession* session) {
    LOGI("Session is now closed");
}

static ACameraCaptureSession_stateCallbacks sessionCallbacks = {
    .context = nullptr,
    .onClosed = onSessionClosed,
    .onReady = onSessionReady,
    .onActive = onSessionActive,
};

static void openCamera(AppEngine* engine) {
    if (!checkCameraPermission(engine->app)) {
        requestCameraPermission(engine->app);
        return;
    }
    camera_status_t status = ACameraManager_getCameraIdList(engine->cameraManager, &engine->cameraIdList);
    if (status != ACAMERA_OK || engine->cameraIdList->numCameras == 0) return;
    engine->cameraId = nullptr;
    for (int i = 0; i < engine->cameraIdList->numCameras; ++i) {
        const char* id = engine->cameraIdList->cameraIds[i];
        ACameraMetadata* metadata;
        ACameraManager_getCameraCharacteristics(engine->cameraManager, id, &metadata);
        uint32_t tag = ACAMERA_LENS_FACING;
        ACameraMetadata_const_entry entry;
        ACameraMetadata_getConstEntry(metadata, tag, &entry);
        auto facing = (acamera_metadata_enum_android_lens_facing_t)entry.data.u8[0];
        if (facing == ACAMERA_LENS_FACING_BACK && engine->cameraId == nullptr) engine->cameraId = id;
        ACameraMetadata_free(metadata);
    }
    if (engine->cameraId == nullptr) engine->cameraId = engine->cameraIdList->cameraIds[0];
    ACameraManager_openCamera(engine->cameraManager, engine->cameraId, &deviceCallbacks, &engine->cameraDevice);
}

static void closeCamera(AppEngine* engine) {
    if (engine->captureSession != nullptr) {
        ACameraCaptureSession_stopRepeating(engine->captureSession);
        ACameraCaptureSession_close(engine->captureSession);
        engine->captureSession = nullptr;
    }
    if (engine->captureRequest != nullptr) {
        ACaptureRequest_free(engine->captureRequest);
        engine->captureRequest = nullptr;
    }
    if (engine->outputTarget != nullptr) {
        ACameraOutputTarget_free(engine->outputTarget);
        engine->outputTarget = nullptr;
    }
    if (engine->sessionOutput != nullptr) {
        ACaptureSessionOutput_free(engine->sessionOutput);
        engine->sessionOutput = nullptr;
    }
    if (engine->outputContainer != nullptr) {
        ACaptureSessionOutputContainer_free(engine->outputContainer);
        engine->outputContainer = nullptr;
    }
    if (engine->cameraDevice != nullptr) {
        ACameraDevice_close(engine->cameraDevice);
        engine->cameraDevice = nullptr;
    }
    if (engine->imageReader != nullptr) {
        AImageReader_delete(engine->imageReader);
        engine->imageReader = nullptr;
        engine->imageReaderWindow = nullptr;
    }
}

static void onImageAvailable(void* context, AImageReader* reader) {
    auto* engine = (AppEngine*)context;
    engine->isRendering.store(true, std::memory_order_release);
    AImage* image = nullptr;
    // Use acquireLatestImage and process in a loop if multiple available
    while (AImageReader_acquireLatestImage(reader, &image) == AMEDIA_OK && image != nullptr) {
        AHardwareBuffer* buffer = nullptr;
        AImage_getHardwareBuffer(image, &buffer);
        if (buffer != nullptr) {
            AHardwareBuffer_acquire(buffer); // Keep the buffer alive for Vulkan
            if (engine->vulkan->updateCameraTexture(buffer)) {
                engine->vulkan->drawFrame();
            }
        }
        AImage_delete(image);
    }
    engine->isRendering.store(false, std::memory_order_release);
}

static void startPreview(AppEngine* engine) {
    if (engine->cameraDevice == nullptr || engine->app->window == nullptr) return;
    media_status_t mStatus = AImageReader_newWithUsage(1920, 1080, AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 3, &engine->imageReader);
    if (mStatus != AMEDIA_OK) return;
    engine->imageListener = { .context = engine, .onImageAvailable = onImageAvailable };
    AImageReader_setImageListener(engine->imageReader, &engine->imageListener);
    AImageReader_getWindow(engine->imageReader, &engine->imageReaderWindow);

    ACaptureSessionOutputContainer_create(&engine->outputContainer);
    ACaptureSessionOutput_create(engine->imageReaderWindow, &engine->sessionOutput);
    ACaptureSessionOutputContainer_add(engine->outputContainer, engine->sessionOutput);
    ACameraDevice_createCaptureSession(engine->cameraDevice, engine->outputContainer, &sessionCallbacks, &engine->captureSession);
    ACameraDevice_createCaptureRequest(engine->cameraDevice, TEMPLATE_PREVIEW, &engine->captureRequest);
    ACameraOutputTarget_create(engine->imageReaderWindow, &engine->outputTarget);
    ACaptureRequest_addTarget(engine->captureRequest, engine->outputTarget);
    ACameraCaptureSession_setRepeatingRequest(engine->captureSession, nullptr, 1, &engine->captureRequest, nullptr);
}

static void onAppCmd(struct android_app* app, int32_t cmd) {
    auto* engine = (AppEngine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                engine->vulkan->init(app->window, app->activity->assetManager);
                openCamera(engine);
                startPreview(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            while (engine->isRendering.load(std::memory_order_acquire)) {
                // Spin-wait until rendering is complete
            }
            closeCamera(engine);
            engine->vulkan->cleanup();
            ANativeWindow_release(app->window);
            break;
    }
}

static int32_t onInputEvent(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);
            LOGI("Touch detected at: %.2f, %.2f", x, y);
            // Intersection logic with UI button will go here
            return 1;
        }
    }
    return 0;
}

void android_main(struct android_app* state) {
    VulkanContext vulkan;
    AppEngine engine = {};
    engine.app = state;
    engine.cameraManager = ACameraManager_create();
    engine.vulkan = &vulkan;
    state->userData = &engine;
    state->onAppCmd = onAppCmd;
    state->onInputEvent = onInputEvent;

    while (true) {
        int ident; int events; struct android_poll_source* source;
        while ((ident = ALooper_pollOnce(0, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) source->process(state, source);
            if (state->destroyRequested != 0) {
                closeCamera(&engine);
                if (engine.cameraManager != nullptr) ACameraManager_delete(engine.cameraManager);
                return;
            }
        }
    }
}
