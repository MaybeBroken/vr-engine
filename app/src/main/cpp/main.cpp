#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <string>

#define LOG_TAG "VREngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct OpenXRContext {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    bool running = false;
};

struct OpenGLESContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
};

class VREngine {
private:
    android_app* app;
    OpenXRContext xr;
    OpenGLESContext gl;

public:
    VREngine(android_app* app) : app(app) {
        LOGI("VREngine initialized");
    }

    ~VREngine() {
        cleanup();
    }

    bool initializeOpenXR() {
        LOGI("Initializing OpenXR...");
        
        // Create OpenXR instance
        XrInstanceCreateInfo instanceCreateInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy(instanceCreateInfo.applicationInfo.applicationName, "VR Engine");
        instanceCreateInfo.applicationInfo.applicationVersion = 1;
        strcpy(instanceCreateInfo.applicationInfo.engineName, "VR Engine");
        instanceCreateInfo.applicationInfo.engineVersion = 1;
        instanceCreateInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        
        // Required extensions for Android
        std::vector<const char*> extensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME
        };
        
        instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceCreateInfo.enabledExtensionNames = extensions.data();
        
        // Android-specific instance creation
        XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        androidInfo.applicationVM = app->activity->vm;
        androidInfo.applicationActivity = app->activity->clazz;
        instanceCreateInfo.next = &androidInfo;
        
        XrResult result = xrCreateInstance(&instanceCreateInfo, &xr.instance);
        if (result != XR_SUCCESS) {
            LOGE("Failed to create OpenXR instance: %d", result);
            return false;
        }
        
        LOGI("OpenXR instance created successfully");
        
        // Get system
        XrSystemGetInfo systemGetInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        
        result = xrGetSystem(xr.instance, &systemGetInfo, &xr.systemId);
        if (result != XR_SUCCESS) {
            LOGE("Failed to get OpenXR system: %d", result);
            return false;
        }
        
        LOGI("OpenXR system acquired: %llu", xr.systemId);
        return true;
    }

    bool initializeOpenGLES() {
        LOGI("Initializing OpenGL ES...");
        
        gl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (gl.display == EGL_NO_DISPLAY) {
            LOGE("Failed to get EGL display");
            return false;
        }
        
        EGLint major, minor;
        if (!eglInitialize(gl.display, &major, &minor)) {
            LOGE("Failed to initialize EGL");
            return false;
        }
        
        LOGI("EGL initialized: version %d.%d", major, minor);
        
        // Choose EGL config
        const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8,
            EGL_NONE
        };
        
        EGLint numConfigs;
        if (!eglChooseConfig(gl.display, configAttribs, &gl.config, 1, &numConfigs)) {
            LOGE("Failed to choose EGL config");
            return false;
        }
        
        // Create EGL context
        const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        
        gl.context = eglCreateContext(gl.display, gl.config, EGL_NO_CONTEXT, contextAttribs);
        if (gl.context == EGL_NO_CONTEXT) {
            LOGE("Failed to create EGL context");
            return false;
        }
        
        // Create a pbuffer surface for rendering
        const EGLint surfaceAttribs[] = {
            EGL_WIDTH, 16,
            EGL_HEIGHT, 16,
            EGL_NONE
        };
        
        gl.surface = eglCreatePbufferSurface(gl.display, gl.config, surfaceAttribs);
        if (gl.surface == EGL_NO_SURFACE) {
            LOGE("Failed to create EGL surface");
            return false;
        }
        
        if (!eglMakeCurrent(gl.display, gl.surface, gl.surface, gl.context)) {
            LOGE("Failed to make EGL context current");
            return false;
        }
        
        LOGI("OpenGL ES context created successfully");
        
        // Print GL info
        LOGI("GL Vendor: %s", glGetString(GL_VENDOR));
        LOGI("GL Renderer: %s", glGetString(GL_RENDERER));
        LOGI("GL Version: %s", glGetString(GL_VERSION));
        
        return true;
    }

    bool initialize() {
        if (!initializeOpenGLES()) {
            return false;
        }
        
        if (!initializeOpenXR()) {
            return false;
        }
        
        LOGI("VR Engine initialization complete");
        return true;
    }

    void cleanup() {
        LOGI("Cleaning up VR Engine...");
        
        // Cleanup OpenXR
        if (xr.session != XR_NULL_HANDLE) {
            xrDestroySession(xr.session);
            xr.session = XR_NULL_HANDLE;
        }
        
        if (xr.instance != XR_NULL_HANDLE) {
            xrDestroyInstance(xr.instance);
            xr.instance = XR_NULL_HANDLE;
        }
        
        // Cleanup OpenGL ES
        if (gl.display != EGL_NO_DISPLAY) {
            eglMakeCurrent(gl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            
            if (gl.surface != EGL_NO_SURFACE) {
                eglDestroySurface(gl.display, gl.surface);
                gl.surface = EGL_NO_SURFACE;
            }
            
            if (gl.context != EGL_NO_CONTEXT) {
                eglDestroyContext(gl.display, gl.context);
                gl.context = EGL_NO_CONTEXT;
            }
            
            eglTerminate(gl.display);
            gl.display = EGL_NO_DISPLAY;
        }
        
        LOGI("Cleanup complete");
    }
};

static void handleAppCmd(android_app* app, int32_t cmd) {
    VREngine* engine = (VREngine*)app->userData;
    
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (engine && app->window != nullptr) {
                engine->initialize();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            break;
        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY");
            break;
    }
}

void android_main(android_app* app) {
    LOGI("android_main started");
    
    VREngine engine(app);
    app->userData = &engine;
    app->onAppCmd = handleAppCmd;
    
    // Main loop
    while (true) {
        int events;
        android_poll_source* source;
        
        // Poll for events
        while (ALooper_pollAll(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            
            if (app->destroyRequested != 0) {
                LOGI("Destroy requested, exiting");
                return;
            }
        }
    }
}
