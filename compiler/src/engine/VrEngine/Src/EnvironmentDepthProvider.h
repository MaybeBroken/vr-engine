// EnvironmentDepthProvider.h - wraps Meta OpenXR environment depth acquisition
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

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
    bool AcquireAndUpload(XrTime predictedTime);

    // GL texture with depth in meters (format depends on runtime; we upload as float).
    GLuint GetTexture() const { return depthTex_; }

    // Suggested near/far range for linearization.
    float GetNearMeters() const { return nearMeters_; }
    float GetFarMeters() const { return farMeters_; }

private:
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSession session_ = XR_NULL_HANDLE;

    // Meta environment depth handles and function pointers.
    // Replace these with actual SDK types if available in headers.
    typedef XrResult(XRAPI_PTR *PFN_xrCreateEnvironmentDepthMETA)(XrSession, const void *createInfo, XrEnvironmentDepthImageMETA *);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyEnvironmentDepthMETA)(XrEnvironmentDepthImageMETA);
    typedef XrResult(XRAPI_PTR *PFN_xrGetEnvironmentDepthFrameMETA)(XrEnvironmentDepthImageMETA, XrTime, void *depthInfo);

    PFN_xrCreateEnvironmentDepthMETA pfnCreate_ = nullptr;
    PFN_xrDestroyEnvironmentDepthMETA pfnDestroy_ = nullptr;
    PFN_xrGetEnvironmentDepthFrameMETA pfnGetFrame_ = nullptr;
    // In newer OpenXR headers, XrEnvironmentDepthImageMETA is a struct, not a handle.
    // Default-initialize instead of casting XR_NULL_HANDLE.
    XrEnvironmentDepthImageMETA handle_{};
    bool handleCreated_ = false;
    bool supported_ = false;

    GLuint depthTex_ = 0;
    int lastW_ = 0;
    int lastH_ = 0;
    float nearMeters_ = 0.1f;
    float farMeters_ = 10.0f;
};
