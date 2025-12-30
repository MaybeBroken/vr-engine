#include "EnvironmentDepthProvider.h"
#include <cstring>
#include <vector>
#include "OVR_Math.h"
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <cstdio>
bool EnvironmentDepthProvider::Init(XrInstance instance, XrSession session)
{
    instance_ = instance;
    session_ = session;
    supported_ = false;
    useOldFrameApi_ = false;
    apiMode_ = DepthApiMode::kUnknown;

    // Load META environment depth function pointers (swapchain-based API)
    xrGetInstanceProcAddr(instance_, "xrCreateEnvironmentDepthProviderMETA", (PFN_xrVoidFunction *)&pfnCreateProvider_);
    xrGetInstanceProcAddr(instance_, "xrDestroyEnvironmentDepthProviderMETA", (PFN_xrVoidFunction *)&pfnDestroyProvider_);
    xrGetInstanceProcAddr(instance_, "xrStartEnvironmentDepthProviderMETA", (PFN_xrVoidFunction *)&pfnStartProvider_);
    xrGetInstanceProcAddr(instance_, "xrStopEnvironmentDepthProviderMETA", (PFN_xrVoidFunction *)&pfnStopProvider_);
    xrGetInstanceProcAddr(instance_, "xrCreateEnvironmentDepthSwapchainMETA", (PFN_xrVoidFunction *)&pfnCreateSwapchain_);
    xrGetInstanceProcAddr(instance_, "xrDestroyEnvironmentDepthSwapchainMETA", (PFN_xrVoidFunction *)&pfnDestroySwapchain_);
    xrGetInstanceProcAddr(instance_, "xrEnumerateEnvironmentDepthSwapchainImagesMETA", (PFN_xrVoidFunction *)&pfnEnumSwapchainImages_);
    xrGetInstanceProcAddr(instance_, "xrAcquireEnvironmentDepthSwapchainImageMETA", (PFN_xrVoidFunction *)&pfnAcquireImage_);
    xrGetInstanceProcAddr(instance_, "xrReleaseEnvironmentDepthSwapchainImageMETA", (PFN_xrVoidFunction *)&pfnReleaseImage_);
    xrGetInstanceProcAddr(instance_, "xrGetEnvironmentDepthSwapchainStateMETA", (PFN_xrVoidFunction *)&pfnGetSwapchainState_);
    xrGetInstanceProcAddr(instance_, "xrAcquireEnvironmentDepthImageMETA", (PFN_xrVoidFunction *)&pfnAcquireImageOld_);
// Old v1 API (frame-based)
#if defined(XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META)
    xrGetInstanceProcAddr(instance_, "xrGetEnvironmentDepthFrameStateMETA", (PFN_xrVoidFunction *)&pfnGetFrameState_);
#endif

    // Detect API availability: prefer provider-based acquire (returns near/far) when available.
    const bool providerAcquireAvailable = (pfnCreateProvider_ && pfnCreateSwapchain_ && pfnEnumSwapchainImages_ && pfnAcquireImageOld_);
    const bool swapchainAcquireAvailable = (pfnCreateProvider_ && pfnCreateSwapchain_ && pfnEnumSwapchainImages_ && pfnAcquireImage_ && pfnReleaseImage_);
#if defined(XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META)
    const bool oldFrameAvailable = (pfnGetFrameState_ != nullptr);
#else
    const bool oldFrameAvailable = false;
#endif

    if (providerAcquireAvailable)
    {
        apiMode_ = DepthApiMode::kAcquireImage;
        useOldFrameApi_ = false;
    }
    else if (swapchainAcquireAvailable)
    {
        apiMode_ = DepthApiMode::kNewSwapchain;
        useOldFrameApi_ = false;
    }
    else if (oldFrameAvailable)
    {
        apiMode_ = DepthApiMode::kFrameState;
        useOldFrameApi_ = true;
    }
    else
    {
        std::printf("[EnvDepth] No supported environment depth API found.\n");
        return false;
    }

    const char *apiLabel = "UNKNOWN";
    switch (apiMode_)
    {
    case DepthApiMode::kNewSwapchain:
        apiLabel = "NEW swapchain acquire/release";
        break;
    case DepthApiMode::kFrameState:
        apiLabel = "OLD frame-state";
        break;
    case DepthApiMode::kAcquireImage:
        apiLabel = "OLD acquire-image (no release)";
        break;
    default:
        break;
    }
    std::printf("[EnvDepth] Using %s API for environment depth.\n", apiLabel);

    if (apiMode_ == DepthApiMode::kNewSwapchain || apiMode_ == DepthApiMode::kAcquireImage)
    {
        // Create provider
        XrEnvironmentDepthProviderCreateInfoMETA providerCI{};
        providerCI.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_PROVIDER_CREATE_INFO_META;
        providerCI.next = nullptr;
        if (pfnCreateProvider_(session_, &providerCI, &provider_) != XR_SUCCESS || provider_ == XR_NULL_HANDLE)
        {
            std::printf("[EnvDepth] Failed to create environment depth provider.\n");
            return false;
        }

        // Create swapchain. Let runtime choose optimal resolution/format.
        XrEnvironmentDepthSwapchainCreateInfoMETA swapchainCI{};
        swapchainCI.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_CREATE_INFO_META;
        swapchainCI.next = nullptr;
        if (pfnCreateSwapchain_(provider_, &swapchainCI, &swapchain_) != XR_SUCCESS || swapchain_ == XR_NULL_HANDLE)
        {
            std::printf("[EnvDepth] Failed to create environment depth swapchain.\n");
            return false;
        }

        // Enumerate images
        uint32_t count = 0;
        if (pfnEnumSwapchainImages_(swapchain_, 0, &count, nullptr) != XR_SUCCESS || count == 0)
        {
            std::printf("[EnvDepth] Failed to enumerate swapchain images.\n");
            return false;
        }
        images_.resize(count);
        for (auto &img : images_)
        {
            img.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            img.next = nullptr;
            img.image = 0;
        }
        if (pfnEnumSwapchainImages_(swapchain_, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader *>(images_.data())) != XR_SUCCESS)
        {
            images_.clear();
            std::printf("[EnvDepth] Failed to get swapchain images array.\n");
            return false;
        }

        // Query initial swapchain state if available
        if (pfnGetSwapchainState_)
        {
            XrEnvironmentDepthSwapchainStateMETA state{};
            state.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META;
            state.next = nullptr;
            if (pfnGetSwapchainState_(swapchain_, &state) == XR_SUCCESS)
            {
                lastW_ = (int)state.width;
                lastH_ = (int)state.height;
            }
        }

        // Start depth provider when the runtime exposes it; older headers may omit start/stop APIs.
        if (pfnStartProvider_ && !providerStarted_)
        {
            if (pfnStartProvider_(provider_) == XR_SUCCESS)
            {
                providerStarted_ = true;
            }
            else
            {
                std::printf("[EnvDepth] Failed to start environment depth provider.\n");
            }
        }
    }

    supported_ = true;
    depthTex_ = 0; // will point at acquired image per-frame (new API) or frame state's texture (old API)
    lastW_ = 0;
    lastH_ = 0;
    return true;
}

void EnvironmentDepthProvider::Shutdown()
{
    // Stop the provider first if it was explicitly started.
    if (providerStarted_ && pfnStopProvider_ && provider_ != XR_NULL_HANDLE)
    {
        pfnStopProvider_(provider_);
        providerStarted_ = false;
    }

    if (apiMode_ == DepthApiMode::kNewSwapchain)
    {
        // Release any outstanding image (new API only)
        if (pfnReleaseImage_ && swapchain_ != XR_NULL_HANDLE && acquiredIndex_ >= 0)
        {
            pfnReleaseImage_(swapchain_, (uint32_t)acquiredIndex_);
            acquiredIndex_ = -1;
        }
        // Destroy swapchain
        if (pfnDestroySwapchain_ && swapchain_ != XR_NULL_HANDLE)
        {
            pfnDestroySwapchain_(swapchain_);
            swapchain_ = XR_NULL_HANDLE;
        }
        // Destroy provider
        if (pfnDestroyProvider_ && provider_ != XR_NULL_HANDLE)
        {
            pfnDestroyProvider_(provider_);
            provider_ = XR_NULL_HANDLE;
        }
    }
    else if (apiMode_ == DepthApiMode::kAcquireImage)
    {
        // No acquire/release in this path; still clean up swapchain/provider
        if (pfnDestroySwapchain_ && swapchain_ != XR_NULL_HANDLE)
        {
            pfnDestroySwapchain_(swapchain_);
            swapchain_ = XR_NULL_HANDLE;
        }
        if (pfnDestroyProvider_ && provider_ != XR_NULL_HANDLE)
        {
            pfnDestroyProvider_(provider_);
            provider_ = XR_NULL_HANDLE;
        }
    }
    supported_ = false;
}

bool EnvironmentDepthProvider::AcquireAndUpload(XrTime predictedTime, XrSpace baseSpace)
{
    if (!supported_ || apiMode_ == DepthApiMode::kUnknown)
    {
        return false;
    }

    switch (apiMode_)
    {
    case DepthApiMode::kFrameState:
#if defined(XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META)
    {
        if (!pfnGetFrameState_)
            return false;

        XrEnvironmentDepthFrameStateMETA frameState{};
        frameState.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_FRAME_STATE_META;
        frameState.next = nullptr;

        XrResult res = pfnGetFrameState_(session_, &frameState);
        if (res != XR_SUCCESS)
        {
            return false;
        }

        depthTex_ = (GLuint)frameState.environmentDepthTexture;
        lastW_ = (int)frameState.width;
        lastH_ = (int)frameState.height;
        nearMeters_ = frameState.nearZ;
        farMeters_ = frameState.farZ;
        return depthTex_ != 0;
    }
#else
        return false;
#endif

    case DepthApiMode::kAcquireImage:
    {
        if (!pfnAcquireImageOld_ || swapchain_ == XR_NULL_HANDLE || images_.empty())
            return false;

        XrEnvironmentDepthImageAcquireInfoMETA acquireInfo{};
        acquireInfo.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META;
        acquireInfo.next = nullptr;
        acquireInfo.space = baseSpace;
        acquireInfo.displayTime = predictedTime;

        XrEnvironmentDepthImageMETA envImage{};
        envImage.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_META;
        envImage.next = nullptr;

        if (pfnAcquireImageOld_(provider_, &acquireInfo, &envImage) != XR_SUCCESS)
        {
            return false;
        }

        if (envImage.swapchainIndex >= images_.size())
        {
            return false;
        }

        depthTex_ = images_[envImage.swapchainIndex].image;
        nearMeters_ = envImage.nearZ;
        farMeters_ = envImage.farZ;
        return depthTex_ != 0;
    }

    case DepthApiMode::kNewSwapchain:
    {
        // Release previous image before acquiring a new one.
        if (acquiredIndex_ >= 0 && pfnReleaseImage_)
        {
            pfnReleaseImage_(swapchain_, (uint32_t)acquiredIndex_);
            acquiredIndex_ = -1;
        }

        if (swapchain_ == XR_NULL_HANDLE)
            return false;

        XrEnvironmentDepthImageAcquireInfoMETA acquireInfo{};
        acquireInfo.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_IMAGE_ACQUIRE_INFO_META;
        acquireInfo.next = nullptr;
        acquireInfo.space = baseSpace; // not used in this path but set for future compatibility
        acquireInfo.displayTime = predictedTime;

        uint32_t imageIndex = 0;
        if (pfnAcquireImage_(swapchain_, &acquireInfo, &imageIndex) != XR_SUCCESS)
        {
            return false;
        }
        if (imageIndex >= images_.size())
        {
            // Defensive: release if index invalid
            if (pfnReleaseImage_)
            {
                pfnReleaseImage_(swapchain_, imageIndex);
            }
            return false;
        }

        acquiredIndex_ = (int)imageIndex;

        // Point our texture at the acquired GL image name provided by the runtime (new API).
        depthTex_ = images_[imageIndex].image;

        // Refresh swapchain state for dimensions if available
        if (pfnGetSwapchainState_)
        {
            XrEnvironmentDepthSwapchainStateMETA state{};
            state.type = (XrStructureType)XR_TYPE_ENVIRONMENT_DEPTH_SWAPCHAIN_STATE_META;
            state.next = nullptr;
            if (pfnGetSwapchainState_(swapchain_, &state) == XR_SUCCESS)
            {
                lastW_ = (int)state.width;
                lastH_ = (int)state.height;
            }
        }

        return depthTex_ != 0;
    }

    case DepthApiMode::kUnknown:
    default:
        return false;
    }
}
