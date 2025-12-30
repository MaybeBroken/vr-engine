// EnvironmentDepthProvider.h - wraps Meta OpenXR environment depth acquisition
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
// Ensure platform dep types used by openxr_platform.h are visible
#include <jni.h>
#include <EGL/egl.h>
#include <openxr/openxr_platform.h>
#include <vector>

// Utility to acquire environment depth frames and upload to a GL texture.
// This is designed for Quest 3 (XR_META_environment_depth) and fast-motion responsiveness.
class EnvironmentDepthProvider
{
public:
    EnvironmentDepthProvider() = default;
    ~EnvironmentDepthProvider() = default;

    // Initialize after session is ready. Returns true if environment depth is supported.
    bool Init(XrInstance instance, XrSession session);
    void Shutdown();

    // Acquire the latest depth and upload to texture. Returns true if valid this frame.
    // baseSpace should be the XrSpace used for rendering the scene (for correct pose reprojection).
    bool AcquireAndUpload(XrTime predictedTime, XrSpace baseSpace);

    // GL texture with depth in meters (format depends on runtime; we upload as float).
    GLuint GetTexture() const { return depthTex_; }

    // Suggested near/far range for linearization.
    float GetNearMeters() const { return nearMeters_; }
    float GetFarMeters() const { return farMeters_; }
    int GetWidth() const { return lastW_; }
    int GetHeight() const { return lastH_; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;

    // Function pointer typedefs for XR_META_environment_depth (modern swapchain path)
    typedef XrResult(XRAPI_PTR *PFN_xrCreateEnvironmentDepthProviderMETA)(
        XrSession session,
        const XrEnvironmentDepthProviderCreateInfoMETA *createInfo,
        XrEnvironmentDepthProviderMETA *outProvider);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyEnvironmentDepthProviderMETA)(
        XrEnvironmentDepthProviderMETA provider);
    typedef XrResult(XRAPI_PTR *PFN_xrCreateEnvironmentDepthSwapchainMETA)(
        XrEnvironmentDepthProviderMETA provider,
        const XrEnvironmentDepthSwapchainCreateInfoMETA *createInfo,
        XrEnvironmentDepthSwapchainMETA *outSwapchain);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyEnvironmentDepthSwapchainMETA)(
        XrEnvironmentDepthSwapchainMETA swapchain);
    typedef XrResult(XRAPI_PTR *PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA)(
        XrEnvironmentDepthSwapchainMETA swapchain,
        uint32_t imageCapacityInput,
        uint32_t *imageCountOutput,
        XrSwapchainImageBaseHeader *images);
    typedef XrResult(XRAPI_PTR *PFN_xrAcquireEnvironmentDepthSwapchainImageMETA)(
        XrEnvironmentDepthSwapchainMETA swapchain,
        const XrEnvironmentDepthImageAcquireInfoMETA *acquireInfo,
        uint32_t *imageIndex);
    typedef XrResult(XRAPI_PTR *PFN_xrReleaseEnvironmentDepthSwapchainImageMETA)(
        XrEnvironmentDepthSwapchainMETA swapchain,
        uint32_t imageIndex);
    typedef XrResult(XRAPI_PTR *PFN_xrStartEnvironmentDepthProviderMETA)(
        XrEnvironmentDepthProviderMETA provider);
    typedef XrResult(XRAPI_PTR *PFN_xrStopEnvironmentDepthProviderMETA)(
        XrEnvironmentDepthProviderMETA provider);
    typedef XrResult(XRAPI_PTR *PFN_xrGetEnvironmentDepthSwapchainStateMETA)(
        XrEnvironmentDepthSwapchainMETA swapchain,
        XrEnvironmentDepthSwapchainStateMETA *state);
#if defined(XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META)
    // OLD v1 API (frame-based, no swapchain acquire/release)
    typedef XrResult(XRAPI_PTR *PFN_xrGetEnvironmentDepthFrameStateMETA)(
        XrSession session,
        XrEnvironmentDepthFrameStateMETA *frameState);
#endif

    PFN_xrCreateEnvironmentDepthProviderMETA pfnCreateProvider_ = nullptr;
    PFN_xrDestroyEnvironmentDepthProviderMETA pfnDestroyProvider_ = nullptr;
    PFN_xrCreateEnvironmentDepthSwapchainMETA pfnCreateSwapchain_ = nullptr;
    PFN_xrDestroyEnvironmentDepthSwapchainMETA pfnDestroySwapchain_ = nullptr;
    PFN_xrEnumerateEnvironmentDepthSwapchainImagesMETA pfnEnumSwapchainImages_ = nullptr;
    PFN_xrAcquireEnvironmentDepthSwapchainImageMETA pfnAcquireImage_ = nullptr;
    PFN_xrReleaseEnvironmentDepthSwapchainImageMETA pfnReleaseImage_ = nullptr;
    PFN_xrGetEnvironmentDepthSwapchainStateMETA pfnGetSwapchainState_ = nullptr;
    PFN_xrStartEnvironmentDepthProviderMETA pfnStartProvider_ = nullptr;
    PFN_xrStopEnvironmentDepthProviderMETA pfnStopProvider_ = nullptr;
    PFN_xrAcquireEnvironmentDepthImageMETA pfnAcquireImageOld_ = nullptr; // legacy v1 acquire (no release)
#if defined(XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META)
    PFN_xrGetEnvironmentDepthFrameStateMETA pfnGetFrameState_ = nullptr;  // legacy frame-state API
#endif

    // Runtime objects
    XrEnvironmentDepthProviderMETA provider_ = XR_NULL_HANDLE;
    XrEnvironmentDepthSwapchainMETA swapchain_ = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLESKHR> images_{};
    int acquiredIndex_ = -1;
    bool supported_ = false;
    bool useOldFrameApi_ = false; // true when not using the newer swapchain acquire/release API
    bool providerStarted_ = false;

    enum class DepthApiMode
    {
        kUnknown = 0,
        kNewSwapchain,
        kFrameState,
        kAcquireImage
    } apiMode_ = DepthApiMode::kUnknown;

    // Cached info for shader
    GLuint depthTex_ = 0; // points at current acquired swapchain image (OpenGL texture name)
    int lastW_ = 0;
    int lastH_ = 0;
    float nearMeters_ = 0.1f;
    float farMeters_ = 10.0f;
};
