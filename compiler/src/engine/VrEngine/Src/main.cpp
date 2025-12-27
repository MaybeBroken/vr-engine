#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
// INCLUDES_ENTRY
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <openxr/openxr.h>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cstring>
#include "XrApp.h"
#include "Input/ControllerRenderer.h"
#include "Input/TinyUI.h"
#include "Render/SimpleBeamRenderer.h"
#include "CTX.h"
#include "OVR_Math.h"
#include "EnvironmentDepthProvider.h"

// Hand tracking EXT typedefs
typedef XrResult(XRAPI_PTR *PFN_xrCreateHandTrackerEXT)(XrSession session, const XrHandTrackerCreateInfoEXT *createInfo, XrHandTrackerEXT *handTracker);
typedef XrResult(XRAPI_PTR *PFN_xrDestroyHandTrackerEXT)(XrHandTrackerEXT handTracker);
typedef XrResult(XRAPI_PTR *PFN_xrLocateHandJointsEXT)(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT *locateInfo, XrHandJointLocationsEXT *locations);

// OpenXR FB Passthrough function pointer typedefs
typedef XrResult(XRAPI_PTR *PFN_xrCreatePassthroughFB)(XrSession session, const XrPassthroughCreateInfoFB *createInfo, XrPassthroughFB *passthrough);
typedef XrResult(XRAPI_PTR *PFN_xrDestroyPassthroughFB)(XrPassthroughFB passthrough);
typedef XrResult(XRAPI_PTR *PFN_xrPassthroughStartFB)(XrPassthroughFB passthrough);
typedef XrResult(XRAPI_PTR *PFN_xrPassthroughPauseFB)(XrPassthroughFB passthrough);
typedef XrResult(XRAPI_PTR *PFN_xrCreatePassthroughLayerFB)(XrSession session, const XrPassthroughLayerCreateInfoFB *createInfo, XrPassthroughLayerFB *outLayer);
typedef XrResult(XRAPI_PTR *PFN_xrDestroyPassthroughLayerFB)(XrPassthroughLayerFB layer);
typedef XrResult(XRAPI_PTR *PFN_xrPassthroughLayerResumeFB)(XrPassthroughLayerFB layer);
typedef XrResult(XRAPI_PTR *PFN_xrPassthroughLayerPauseFB)(XrPassthroughLayerFB layer);

// INCLUDES_END
// VAR_SPACE_ENTRY

//

// VAR_SPACE_EXIT
// CLASS_ENTRY
class VrEngine : public OVRFW::XrApp
{
public:
    // PUBLIC_ENTRY

    // CLASS_INIT_ENTRY
    VrEngine() : OVRFW::XrApp()
    {
        BackgroundColor = OVR::Vector4f(0.0f, 0.0f, 0.0f, 0.0f);
        OpenXRVersion = XR_API_VERSION_1_1;
        ctx_ = std::make_unique<CTX::Context>();
    }
    // CLASS_INIT_EXIT

    // Returns a list of OpenXr extensions needed for this app
    // APP_EXTENSIONS_ENTRY
    virtual std::vector<const char *> GetExtensions() override
    {
        std::vector<const char *> extensions = XrApp::GetExtensions();
        // APP_EXTENSIONS_MOD_ENTRY
        // Conditionally request alpha blend composition if the runtime supports it
        auto props = GetXrExtensionProperties();
        const bool hasAlphaBlend = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, XR_FB_COMPOSITION_LAYER_ALPHA_BLEND_EXTENSION_NAME) == 0; });
        if (hasAlphaBlend)
        {
            extensions.push_back(XR_FB_COMPOSITION_LAYER_ALPHA_BLEND_EXTENSION_NAME);
        }

        // Request passthrough if available (Meta Quest runtime)
        const bool hasPassthrough = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0; });
        if (hasPassthrough)
        {
            extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
        }

        // Request hand tracking if available
        const bool hasHandTracking = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0; });
        if (hasHandTracking)
        {
            extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        }

        // Request controller render models (high fidelity) if available.
        const bool hasControllerModel = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, XR_MSFT_CONTROLLER_MODEL_EXTENSION_NAME) == 0; });
        if (hasControllerModel)
        {
            extensions.push_back(XR_MSFT_CONTROLLER_MODEL_EXTENSION_NAME);
        }
        uint32_t extCount = 0;
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);

        std::vector<XrExtensionProperties> extProps(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
        xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, extProps.data());

        for (auto &e : extProps)
        {
            ALOG("EXT: %s v%d", e.extensionName, e.extensionVersion);
        }

        // Request environment depth (Meta Quest)
        const bool hasEnvDepth = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, "XR_META_environment_depth") == 0; });
        if (hasEnvDepth)
        {
            extensions.push_back("XR_META_environment_depth");
        }

        // APP_EXTENSIONS_MOD_EXIT
        return extensions;
    }
    // APP_EXTENSIONS_EXIT

    // APP_INIT_ENTRY
    virtual bool AppInit(const xrJava *context) override
    {
        // APP_INIT_MOD_ENTRY

        // APP_INIT_MOD_EXIT
        return true;
    }
    // APP_INIT_EXIT

    // APP_SHUTDOWN_ENTRY
    virtual void AppShutdown(const xrJava *context) override
    {
        // APP_SHUTDOWN_MOD_ENTRY
        ctx_.reset();
        OVRFW::XrApp::AppShutdown(context);
        // APP_SHUTDOWN_MOD_EXIT
    }
    // APP_SHUTDOWN_EXIT

    // SESSION_INIT_ENTRY
    virtual bool SessionInit() override
    {
        // SESSION_INIT_MOD_ENTRY
        /// Use LocalSpace instead of Stage Space.
        CurrentSpace = LocalSpace;
        // Query runtime-supported environment blend modes; guard alpha blend usage.
        alphaBlendSupported_ = false;
        uint32_t count = 0;
        // First call with capacity 0 to query count
        xrEnumerateEnvironmentBlendModes(
            GetInstance(),
            GetSystemId(),
            ViewportConfig.viewConfigurationType,
            0,
            &count,
            nullptr);
        if (count > 0)
        {
            std::vector<XrEnvironmentBlendMode> modes(count);
            if (xrEnumerateEnvironmentBlendModes(
                    GetInstance(),
                    GetSystemId(),
                    ViewportConfig.viewConfigurationType,
                    count,
                    &count,
                    modes.data()) == XR_SUCCESS)
            {
                for (auto m : modes)
                {
                    if (m == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)
                    {
                        alphaBlendSupported_ = true;
                        break;
                    }
                }
            }
        }
        /// Init session bound objects
        if (false == controllerRenderL_.Init(true))
        {
            ALOG("AppInit::Init L controller renderer FAILED.");
            return false;
        }
        if (false == controllerRenderR_.Init(false))
        {
            ALOG("AppInit::Init R controller renderer FAILED.");
            return false;
        }

        // Load higher-fidelity controller models via XR_MSFT_controller_model if supported.
        controllerModelExtSupported_ = false;

        // Check enabled extensions list
        auto exts = GetExtensions();
        auto hasExt = [&](const char *name)
        {
            return std::any_of(exts.begin(), exts.end(), [&](const char *e)
                               { return strcmp(e, name) == 0; });
        };
        if (hasExt(XR_MSFT_CONTROLLER_MODEL_EXTENSION_NAME))
        {
            controllerModelExtSupported_ =
                (xrGetInstanceProcAddr(GetInstance(), "xrGetControllerModelKeyMSFT", (PFN_xrVoidFunction *)&pfnGetControllerModelKeyMSFT_) == XR_SUCCESS) &&
                (xrGetInstanceProcAddr(GetInstance(), "xrLoadControllerModelMSFT", (PFN_xrVoidFunction *)&pfnLoadControllerModelMSFT_) == XR_SUCCESS);
        }

        // Enable passthrough if the runtime and manifest support it.
        // Note: actual passthrough surfaces require XR_FB_passthrough session objects.
        if (ctx_)
        {
            ctx_->EnablePassthrough(true);
        }

        // Create and start XR_FB_passthrough objects when available.
        // Load function pointers via xrGetInstanceProcAddr.
        PFN_xrCreatePassthroughFB pfnCreatePassthrough = nullptr;
        PFN_xrDestroyPassthroughFB pfnDestroyPassthrough = nullptr;
        PFN_xrPassthroughStartFB pfnPassthroughStart = nullptr;
        PFN_xrCreatePassthroughLayerFB pfnCreatePassthroughLayer = nullptr;
        PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayer = nullptr;
        PFN_xrPassthroughLayerResumeFB pfnPassthroughLayerResume = nullptr;

        auto loadProc = [&](const char *name, void **fn)
        {
            return xrGetInstanceProcAddr(GetInstance(), name, (PFN_xrVoidFunction *)fn);
        };

        bool passthroughExtEnabled = false;
        {
            // Check enabled extensions list
            auto exts = GetExtensions();
            for (auto e : exts)
            {
                if (strcmp(e, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
                {
                    passthroughExtEnabled = true;
                    break;
                }
            }
        }

        if (passthroughExtEnabled)
        {
            if (loadProc("xrCreatePassthroughFB", (void **)&pfnCreatePassthrough) == XR_SUCCESS &&
                loadProc("xrDestroyPassthroughFB", (void **)&pfnDestroyPassthrough) == XR_SUCCESS &&
                loadProc("xrPassthroughStartFB", (void **)&pfnPassthroughStart) == XR_SUCCESS &&
                loadProc("xrCreatePassthroughLayerFB", (void **)&pfnCreatePassthroughLayer) == XR_SUCCESS &&
                loadProc("xrDestroyPassthroughLayerFB", (void **)&pfnDestroyPassthroughLayer) == XR_SUCCESS &&
                loadProc("xrPassthroughLayerResumeFB", (void **)&pfnPassthroughLayerResume) == XR_SUCCESS)
            {
                // Create passthrough object
                XrPassthroughCreateInfoFB ptCreate{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
                ptCreate.flags = 0; // default
                if (pfnCreatePassthrough(GetSession(), &ptCreate, &passthrough_) == XR_SUCCESS)
                {
                    pfnPassthroughStart(passthrough_);

                    // Create a passthrough layer for camera reconstruction
                    XrPassthroughLayerCreateInfoFB layerCreate{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
                    layerCreate.passthrough = passthrough_;
                    layerCreate.flags = 0;                                                // no creation flags
                    layerCreate.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB; // default camera
                    if (pfnCreatePassthroughLayer(GetSession(), &layerCreate, &passthroughLayer_) == XR_SUCCESS)
                    {
                        pfnPassthroughLayerResume(passthroughLayer_);
                        passthroughActive_ = true;
                    }
                }
            }
        }

        // Initialize environment depth acquisition (Meta extension)
        envDepthProvider_.Init(GetInstance(), GetSession());

        // Initialize hand tracking after session is ready
        InitHandTracking();

        // Session-specific renderer setup can go here if needed.
        return true;
        // SESSION_INIT_MOD_EXIT
    }
    // SESSION_INIT_EXIT

    // SESSION_END_ENTRY
    virtual void SessionEnd() override
    {
        // SESSION_END_MOD_ENTRY
        controllerRenderL_.Shutdown();
        controllerRenderR_.Shutdown();
        // Destroy passthrough resources if created
        if (passthroughActive_)
        {
            PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayer = nullptr;
            PFN_xrDestroyPassthroughFB pfnDestroyPassthrough = nullptr;
            xrGetInstanceProcAddr(GetInstance(), "xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction *)&pfnDestroyPassthroughLayer);
            xrGetInstanceProcAddr(GetInstance(), "xrDestroyPassthroughFB", (PFN_xrVoidFunction *)&pfnDestroyPassthrough);
            if (pfnDestroyPassthroughLayer && passthroughLayer_ != XR_NULL_HANDLE)
            {
                pfnDestroyPassthroughLayer(passthroughLayer_);
            }
            if (pfnDestroyPassthrough && passthrough_ != XR_NULL_HANDLE)
            {
                pfnDestroyPassthrough(passthrough_);
            }
            passthroughLayer_ = XR_NULL_HANDLE;
            passthrough_ = XR_NULL_HANDLE;
            passthroughActive_ = false;
        }
        // Destroy hand trackers
        if (pfnDestroyHandTracker_)
        {
            if (leftHandTracker_ != XR_NULL_HANDLE)
            {
                pfnDestroyHandTracker_(leftHandTracker_);
                leftHandTracker_ = XR_NULL_HANDLE;
            }
            if (rightHandTracker_ != XR_NULL_HANDLE)
            {
                pfnDestroyHandTracker_(rightHandTracker_);
                rightHandTracker_ = XR_NULL_HANDLE;
            }
        }
        // SESSION_END_MOD_EXIT
    }
    // SESSION_END_EXIT

    // Update state
    virtual void Update(const OVRFW::ovrApplFrameIn &in) override
    {
        // UPDATE_ENTRY
        // UPDATE_MOD_ENTRY

        if (ctx_)
        {
            ctx_->Update(in.DeltaSeconds);
            if (in.Clicked(OVRFW::ovrApplFrameIn::kButtonA))
            {
                TriggerAction(CTX::Action::ButtonA, 1.0f, OVR::Vector3f(0.0f), true);
            }
            if (in.Clicked(OVRFW::ovrApplFrameIn::kButtonB))
            {
                TriggerAction(CTX::Action::ButtonB, 1.0f, OVR::Vector3f(0.0f), true);
            }

            // Map controller triggers to the same pinch actions as hand tracking
            const float triggerOn = 0.15f;

            const bool leftNow = in.LeftRemoteIndexTrigger > triggerOn;
            const OVR::Vector3f leftPos = OVR::Vector3f(in.LeftRemotePose.Translation.x,
                                                        in.LeftRemotePose.Translation.y,
                                                        in.LeftRemotePose.Translation.z);
            if (leftNow || leftTriggerHeld_)
            {
                TriggerAction(CTX::Action::PinchLeft, in.LeftRemoteIndexTrigger, leftPos, leftNow);
            }
            leftTriggerHeld_ = leftNow;

            const bool rightNow = in.RightRemoteIndexTrigger > triggerOn;
            const OVR::Vector3f rightPos = OVR::Vector3f(in.RightRemotePose.Translation.x,
                                                         in.RightRemotePose.Translation.y,
                                                         in.RightRemotePose.Translation.z);
            if (rightNow || rightTriggerHeld_)
            {
                TriggerAction(CTX::Action::PinchRight, in.RightRemoteIndexTrigger, rightPos, rightNow);
            }
            rightTriggerHeld_ = rightNow;
        }

        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Update(in.LeftRemotePose);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Update(in.RightRemotePose);
        }
        // Process hand tracking gestures
        UpdateHandTracking(in);

        // Acquire environment depth; if valid, enable occlusion globally.
        if (envDepthProvider_.AcquireAndUpload(in.PredictedDisplayTime))
        {
            // Bind on all models for now
            if (ctx_)
            {
                for (auto &m : ctx_->Models())
                {
                    m->setEnvironmentDepthTexture(envDepthProvider_.GetTexture());
                    m->setEnvironmentDepthEnabled(true);
                    m->setEnvironmentDepthRange(envDepthProvider_.GetNearMeters(), envDepthProvider_.GetFarMeters());
                    m->setEnvironmentDepthTextureSize(
                        static_cast<float>(envDepthProvider_.GetWidth()),
                        static_cast<float>(envDepthProvider_.GetHeight()));
                }
            }
        }
        else
        {
            if (ctx_)
            {
                for (auto &m : ctx_->Models())
                {
                    m->setEnvironmentDepthEnabled(false);
                }
            }
        }
        // UPDATE_MOD_EXIT
        // UPDATE_EXIT
    }
    // RENDER_ENTRY
    virtual void Render(const OVRFW::ovrApplFrameIn &in, OVRFW::ovrRendererOutput &out) override
    {
        // RENDER_MOD_ENTRY
        /// Render controllers
        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Render(out.Surfaces);
        }
        // if (in.RightRemoteTracked)
        // {
        //     controllerRenderR_.Render(out.Surfaces);
        // }
        if (ctx_)
        {
            ctx_->RenderAll(out.Surfaces);
        }
        // RENDER_MOD_EXIT
    }
    // Override composition helpers to enable framebuffer alpha blending
    virtual void PreEndFrame(XrFrameEndInfo &frameEndInfo) override
    {
        // Use alpha blend so the runtime composites against passthrough/camera.
        frameEndInfo.environmentBlendMode =
            alphaBlendSupported_ ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    }

    virtual void ProjectionAddLayer(OVRFW::XrApp::xrCompositorLayerUnion *layers, int &layerCount) override
    {
        // 1) If passthrough is active, submit it first so it's drawn behind projection layers.
        if (passthroughActive_)
        {
            XrCompositionLayerPassthroughFB ptLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
            ptLayer.next = nullptr;
            ptLayer.flags = XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT; // or PREMULTIPLIED if you prefer
            ptLayer.layerHandle = passthroughLayer_;

            layers[layerCount].Passthrough = ptLayer;
            layerCount++;
        }

        // 2) Now add the normal projection layer(s) using the base implementation.
        //    This will append projection layers after the passthrough we just added.
        OVRFW::XrApp::ProjectionAddLayer(layers, layerCount);

        // 3) Attach alpha-blend extension to the last added projection layer (if supported).
        //    layerCount > 0 is still a valid guard because ProjectionAddLayer appended at least one projection layer.
        if (alphaBlendSupported_ && layerCount > 0)
        {
            // Find the most-recent projection layer we just added. It should be at layerCount-1.
            alphaBlend_.type = XR_TYPE_COMPOSITION_LAYER_ALPHA_BLEND_FB;
            alphaBlend_.next = nullptr;
            alphaBlend_.srcFactorColor = XR_BLEND_FACTOR_ONE_FB;
            alphaBlend_.dstFactorColor = XR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA_FB;
            alphaBlend_.srcFactorAlpha = XR_BLEND_FACTOR_ONE_FB;
            alphaBlend_.dstFactorAlpha = XR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA_FB;

            // Attach the alpha-blend struct to the projection layer we just added.
            // Important: ensure that the layer at layerCount-1 is a Projection layer here.
            layers[layerCount - 1].Projection.next = &alphaBlend_;
            layers[layerCount - 1].Projection.layerFlags |= XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        }
    }
    // RENDER_EXIT
    // PUBLIC_EXIT

private:
    // PRIVATE_ENTRY
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;
    std::unique_ptr<CTX::Context> ctx_;
    XrCompositionLayerAlphaBlendFB alphaBlend_{};
    bool alphaBlendSupported_ = false;
    bool controllerModelExtSupported_ = false;
    // Passthrough handles
    XrPassthroughFB passthrough_ = XR_NULL_HANDLE;
    XrPassthroughLayerFB passthroughLayer_ = XR_NULL_HANDLE;
    bool passthroughActive_ = false;

    EnvironmentDepthProvider envDepthProvider_{};

    // Hand tracking state
    PFN_xrCreateHandTrackerEXT pfnCreateHandTracker_ = nullptr;
    PFN_xrDestroyHandTrackerEXT pfnDestroyHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT pfnLocateHandJoints_ = nullptr;
    PFN_xrGetControllerModelKeyMSFT pfnGetControllerModelKeyMSFT_ = nullptr;
    PFN_xrLoadControllerModelMSFT pfnLoadControllerModelMSFT_ = nullptr;
    XrHandTrackerEXT leftHandTracker_ = XR_NULL_HANDLE;
    XrHandTrackerEXT rightHandTracker_ = XR_NULL_HANDLE;
    bool handTrackingEnabled_ = false;
    bool leftPinchActive_ = false;
    bool rightPinchActive_ = false;
    std::vector<uint8_t> controllerModelBufL_;
    std::vector<uint8_t> controllerModelBufR_;

    void InitHandTracking()
    {
        // Load function pointers
        xrGetInstanceProcAddr(GetInstance(), "xrCreateHandTrackerEXT", (PFN_xrVoidFunction *)&pfnCreateHandTracker_);
        xrGetInstanceProcAddr(GetInstance(), "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction *)&pfnDestroyHandTracker_);
        xrGetInstanceProcAddr(GetInstance(), "xrLocateHandJointsEXT", (PFN_xrVoidFunction *)&pfnLocateHandJoints_);
        if (!pfnCreateHandTracker_ || !pfnLocateHandJoints_)
            return;

        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        ci.next = nullptr;
        ci.hand = XR_HAND_LEFT_EXT;
        if (pfnCreateHandTracker_(GetSession(), &ci, &leftHandTracker_) == XR_SUCCESS)
        {
            handTrackingEnabled_ = true;
        }
        ci.hand = XR_HAND_RIGHT_EXT;
        if (pfnCreateHandTracker_(GetSession(), &ci, &rightHandTracker_) == XR_SUCCESS)
        {
            handTrackingEnabled_ = true;
        }
    }

    void UpdateHandTracking(const OVRFW::ovrApplFrameIn &in)
    {
        if (!handTrackingEnabled_ || !pfnLocateHandJoints_)
            return;

        auto process = [&](XrHandTrackerEXT tracker, CTX::Action action, bool &activeFlag)
        {
            if (tracker == XR_NULL_HANDLE)
                return;
            XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
            li.baseSpace = CurrentSpace;
            li.time = in.PredictedDisplayTime;
            XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = joints;
            if (pfnLocateHandJoints_(tracker, &li, &locs) != XR_SUCCESS || !locs.isActive)
            {
                if (activeFlag)
                {
                    TriggerPinch(action, 0.0f, OVR::Vector3f(0.0f), false);
                    activeFlag = false;
                }
                return;
            }
            const auto &thumb = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
            const auto &index = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
            OVR::Vector3f t(thumb.pose.position.x, thumb.pose.position.y, thumb.pose.position.z);
            OVR::Vector3f i(index.pose.position.x, index.pose.position.y, index.pose.position.z);
            float dist = (t - i).Length();
            const float threshold = 0.025f;
            float strength = 0.0f;
            if (dist < threshold)
            {
                strength = std::min(1.0f, (threshold - dist) / threshold);
                TriggerPinch(action, strength, (t + i) * 0.5f, true);
                activeFlag = true;
            }
            else if (activeFlag)
            {
                TriggerPinch(action, 0.0f, (t + i) * 0.5f, false);
                activeFlag = false;
            }
        };

        process(leftHandTracker_, CTX::Action::PinchLeft, leftPinchActive_);
        process(rightHandTracker_, CTX::Action::PinchRight, rightPinchActive_);
    }

    void TriggerPinch(CTX::Action action, float strength, const OVR::Vector3f &pos, bool active)
    {
        if (!ctx_)
            return;
        CTX::ActionEvent evt{action, strength, pos, active};
        ctx_->Trigger(evt);
    }

    void TriggerAction(CTX::Action action, float strength, const OVR::Vector3f &pos, bool active)
    {
        if (!ctx_)
            return;
        CTX::ActionEvent evt{action, strength, pos, active};
        ctx_->Trigger(evt);
    }

    bool leftTriggerHeld_ = false;
    bool rightTriggerHeld_ = false;

    // PRIVATE_EXIT
};
// CLASS_EXIT

// ENTRY_POINT_INIT
ENTRY_POINT(VrEngine)
// ENTRY_POINT_EXIT