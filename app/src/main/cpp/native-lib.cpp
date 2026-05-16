#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraCaptureSession.h>

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
};

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
    camera_status_t status = ACameraManager_getCameraIdList(engine->cameraManager, &engine->cameraIdList);
    if (status != ACAMERA_OK || engine->cameraIdList->numCameras == 0) {
        LOGE("Failed to get camera ID list or no cameras found");
        return;
    }

    // Use the first camera (usually back-facing)
    engine->cameraId = engine->cameraIdList->cameraIds[0];
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

    if (engine->cameraIdList != nullptr) {
        ACameraManager_deleteCameraIdList(engine->cameraIdList);
        engine->cameraIdList = nullptr;
    }
}

static void startPreview(AppEngine* engine) {
    if (engine->cameraDevice == nullptr || engine->app->window == nullptr) {
        LOGE("Camera device or window not ready for preview");
        return;
    }

    // 1. Prepare outputs
    ACaptureSessionOutputContainer_create(&engine->outputContainer);
    ANativeWindow_acquire(engine->app->window);
    ACaptureSessionOutput_create(engine->app->window, &engine->sessionOutput);
    ACaptureSessionOutputContainer_add(engine->outputContainer, engine->sessionOutput);

    // 2. Create capture session
    camera_status_t status = ACameraDevice_createCaptureSession(engine->cameraDevice, engine->outputContainer, &sessionCallbacks, &engine->captureSession);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture session: %d", status);
        return;
    }

    // 3. Prepare preview request
    status = ACameraDevice_createCaptureRequest(engine->cameraDevice, TEMPLATE_PREVIEW, &engine->captureRequest);
    if (status != ACAMERA_OK) {
        LOGE("Failed to create capture request: %d", status);
        return;
    }

    ACameraOutputTarget_create(engine->app->window, &engine->outputTarget);
    ACaptureRequest_addTarget(engine->captureRequest, engine->outputTarget);

    // 4. Start repeating request
    status = ACameraCaptureSession_setRepeatingRequest(engine->captureSession, nullptr, 1, &engine->captureRequest, nullptr);
    if (status != ACAMERA_OK) {
        LOGE("Failed to start repeating request: %d", status);
    } else {
        LOGI("Preview started successfully");
    }
}

static void onAppCmd(struct android_app* app, int32_t cmd) {
    auto* engine = (AppEngine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr) {
                openCamera(engine);
                startPreview(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            closeCamera(engine);
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

    AppEngine engine = {};
    engine.app = state;
    engine.cameraManager = ACameraManager_create();

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
