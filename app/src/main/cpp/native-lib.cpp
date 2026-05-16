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
#include "vulkan_context.h"

#undef LOG_TAG
#define LOG_TAG "NativeCamera"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct AppEngine {
    struct android_app* app;
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
    return result == 0; // PERMISSION_GRANTED
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

// Device State Callbacks
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

// Session State Callbacks
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
        LOGI("Camera permission not granted. Requesting...");
        requestCameraPermission(engine->app);
        return;
    }

    camera_status_t status = ACameraManager_getCameraIdList(engine->cameraManager, &engine->cameraIdList);
    if (status != ACAMERA_OK || engine->cameraIdList->numCameras == 0) {
        LOGE("Failed to get camera ID list (status: %d) or no cameras found", status);
        return;
    }

    LOGI("Found %d cameras", engine->cameraIdList->numCameras);

    // Find the first back-facing camera
    engine->cameraId = nullptr;
    for (int i = 0; i < engine->cameraIdList->numCameras; ++i) {
        const char* id = engine->cameraIdList->cameraIds[i];
        ACameraMetadata* metadata;
        ACameraManager_getCameraCharacteristics(engine->cameraManager, id, &metadata);
        
        uint32_t tag = ACAMERA_LENS_FACING;
        ACameraMetadata_const_entry entry;
        ACameraMetadata_getConstEntry(metadata, tag, &entry);
        
        auto facing = (acamera_metadata_enum_android_lens_facing_t)entry.data.u8[0];
        LOGI("Camera ID: %s, Facing: %s", id, (facing == ACAMERA_LENS_FACING_BACK) ? "BACK" : "FRONT");

        if (facing == ACAMERA_LENS_FACING_BACK && engine->cameraId == nullptr) {
            engine->cameraId = id;
        }
        ACameraMetadata_free(metadata);
    }

    if (engine->cameraId == nullptr) {
        engine->cameraId = engine->cameraIdList->cameraIds[0];
        LOGI("No back camera found, using ID: %s", engine->cameraId);
    }

    LOGI("Opening Camera: %s", engine->cameraId);
    status = ACameraManager_openCamera(engine->cameraManager, engine->cameraId, &deviceCallbacks, &engine->cameraDevice);
    if (status != ACAMERA_OK) {
        LOGE("Failed to open camera: %d", status);
    }
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

    if (engine->cameraIdList != nullptr) {
        ACameraManager_deleteCameraIdList(engine->cameraIdList);
        engine->cameraIdList = nullptr;
    }
}

static void onImageAvailable(void* context, AImageReader* reader) {
    auto* engine = (AppEngine*)context;
    AImage* image = nullptr;
    media_status_t status = AImageReader_acquireLatestImage(reader, &image);
    
    if (status == AMEDIA_OK && image != nullptr) {
        // Here we will eventually get the AHardwareBuffer and pass it to Vulkan
        // For now, just release to keep the queue moving
        AImage_delete(image);
    }
}

static void startPreview(AppEngine* engine) {
    if (engine->cameraDevice == nullptr || engine->app->window == nullptr) {
        LOGE("Camera device or window not ready for preview");
        return;
    }

    LOGI("Starting preview initialization with AImageReader...");

    // 1. Initialize AImageReader
    media_status_t mediaStatus = AImageReader_newWithUsage(1920, 1080, AIMAGE_FORMAT_YUV_420_888, 
                                                          AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, 
                                                          3, &engine->imageReader);
    if (mediaStatus != AMEDIA_OK) {
        LOGE("Failed to create AImageReader: %d", mediaStatus);
        return;
    }

    AImageReader_ImageListener listener = {
        .context = engine,
        .onImageAvailable = onImageAvailable,
    };
    AImageReader_setImageListener(engine->imageReader, &listener);
    AImageReader_getWindow(engine->imageReader, &engine->imageReaderWindow);

    // 2. Prepare outputs (using ImageReader window instead of app window)
    ACaptureSessionOutputContainer_create(&engine->outputContainer);
    ACaptureSessionOutput_create(engine->imageReaderWindow, &engine->sessionOutput);
    ACaptureSessionOutputContainer_add(engine->outputContainer, engine->sessionOutput);

    // 3. Create capture session
    LOGI("Creating capture session...");
    camera_status_t status = ACameraDevice_createCaptureSession(engine->cameraDevice, engine->outputContainer, &sessionCallbacks, &engine->captureSession);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture session: %d", status);
        return;
    }

    // 4. Prepare preview request
    LOGI("Creating capture request...");
    status = ACameraDevice_createCaptureRequest(engine->cameraDevice, TEMPLATE_PREVIEW, &engine->captureRequest);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture request: %d", status);
        return;
    }

    ACameraOutputTarget_create(engine->imageReaderWindow, &engine->outputTarget);
    ACaptureRequest_addTarget(engine->captureRequest, engine->outputTarget);

    // 5. Start repeating request
    LOGI("Setting repeating request...");
    status = ACameraCaptureSession_setRepeatingRequest(engine->captureSession, nullptr, 1, &engine->captureRequest, nullptr);
    if (status != ACAMERA_OK) {
        LOGE("Failed to start repeating request: %d", status);
    } else {
        LOGI("Preview repeating request set successfully via AImageReader.");
    }
}

static void onAppCmd(struct android_app* app, int32_t cmd) {
    auto* engine = (AppEngine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr) {
                if (engine->vulkan->init(app->window)) {
                    LOGI("Vulkan Initialized on window creation");
                }
                openCamera(engine);
                startPreview(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            closeCamera(engine);
            engine->vulkan->cleanup();
            ANativeWindow_release(app->window);
            break;
        case APP_CMD_STOP:
            LOGI("APP_CMD_STOP");
            break;
        default:
            break;
    }
}

void android_main(struct android_app* state) {
    LOGI("Starting NativeActivity Engine...");

    VulkanContext vulkan;
    AppEngine engine = {};
    engine.app = state;
    engine.cameraManager = ACameraManager_create();
    engine.vulkan = &vulkan;

    state->userData = &engine;
    state->onAppCmd = onAppCmd;

    // Main loop
    while (true) {
        int ident;
        int events;
        struct android_poll_source* source;

        while ((ident = ALooper_pollOnce(0, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) {
                source->process(state, source);
            }

            if (state->destroyRequested != 0) {
                LOGI("Destroy requested. Exiting loop.");
                closeCamera(&engine);
                if (engine.cameraManager != nullptr) {
                    ACameraManager_delete(engine.cameraManager);
                }
                return;
            }
        }
    }
}
