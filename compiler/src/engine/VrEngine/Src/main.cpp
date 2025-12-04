#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#define XR_USE_PLATFORM_ANDROID 1
// INCLUDES_ENTRY
#include <cstdint>
#include <cstdio>
#include <algorithm>
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

        // Request passthrough extension if available (FB path)
        const bool hasFbPassthrough = std::any_of(
            props.begin(), props.end(), [](const XrExtensionProperties &p)
            { return strcmp(p.extensionName, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0; });
        if (hasFbPassthrough)
        {
            extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
        }

        // APP_EXTENSIONS_MOD_EXIT
        return extensions;
    }
    // APP_EXTENSIONS_EXIT

    // APP_INIT_ENTRY
    virtual bool AppInit(const xrJava *context) override
    {
        // APP_INIT_MOD_ENTRY
        ui_ = std::make_unique<OVRFW::TinyUI>();
        ui_->Init();
        // APP_INIT_MOD_EXIT
        return true;
    }
    // APP_INIT_EXIT

    // APP_SHUTDOWN_ENTRY
    virtual void AppShutdown(const xrJava *context) override
    {
        // APP_SHUTDOWN_MOD_ENTRY
        if (ui_)
        {
            ui_->Shutdown();
            ui_.reset();
        }
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
        // Nothing to push; glb surfaces will be emitted during Render.

        // Initialize passthrough via CTX so API usage is centralized.
        if (ctx_)
        {
            ctx_->InitPassthrough(GetInstance(), GetSession(), GetExtensions());
            // Enable room views (scene meshes) for MR visualization
            ctx_->EnableRoomViews(GetInstance(), GetSession(), true);
            // Enable hand joint visualization
            ctx_->EnableHandViews(GetInstance(), GetSession(), true);
        }

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
        // Shutdown passthrough via CTX
        if (ctx_)
        {
            ctx_->ShutdownPassthrough(GetInstance());
        }
        // SESSION_END_MOD_EXIT
    }
    // SESSION_END_EXIT

    // Update state
    // UPDATE_ENTRY
    virtual void Update(const OVRFW::ovrApplFrameIn &in) override
    {
        // UPDATE_MOD_ENTRY

        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Update(in.LeftRemotePose);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Update(in.RightRemotePose);
        }
        // Update hand joints each frame in local space
        if (ctx_)
        {
            ctx_->UpdateHands(GetInstance(), GetSession(), LocalSpace, in.PredictedDisplayTime);
        }
        // Update UI status text
        if (ui_ && ctx_)
        {
            char buf[256];
            snprintf(buf, sizeof(buf), "PT:%s  Scene:%s  Hands:%s",
                     ctx_->IsPassthroughActive() ? "ON" : "OFF",
                     ctx_->IsRoomViewsEnabled() ? "ON" : "OFF",
                     ctx_->IsHandViewsEnabled() ? "ON" : "OFF");
            ui_->SetMessage(buf);
        }
        // UPDATE_MOD_EXIT
    }
    // UPDATE_EXIT
    // RENDER_ENTRY
    virtual void Render(const OVRFW::ovrApplFrameIn &in, OVRFW::ovrRendererOutput &out) override
    {
        // RENDER_MOD_ENTRY
        /// Render controllers
        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Render(out.Surfaces);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Render(out.Surfaces);
        }
        if (ctx_)
        {
            ctx_->RenderAll(out.Surfaces);
        }
        if (ui_)
        {
            ui_->Render(in, out);
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
        // Build default projection layer via base, then attach alpha blend factors.
        OVRFW::XrApp::ProjectionAddLayer(layers, layerCount);
        // Attach FB alpha blend extension struct with premultiplied factors.
        if (alphaBlendSupported_ && layerCount > 0)
        {
            alphaBlend_.type = XR_TYPE_COMPOSITION_LAYER_ALPHA_BLEND_FB;
            alphaBlend_.next = nullptr;
            alphaBlend_.srcFactorColor = XR_BLEND_FACTOR_ONE_FB;
            alphaBlend_.dstFactorColor = XR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA_FB;
            alphaBlend_.srcFactorAlpha = XR_BLEND_FACTOR_ONE_FB;
            alphaBlend_.dstFactorAlpha = XR_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA_FB;
            // The last added layer is the projection layer.
            layers[layerCount - 1].Projection.next = &alphaBlend_;
            // Ensure layer flags include blending from source alpha
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
    std::unique_ptr<OVRFW::TinyUI> ui_;
    XrCompositionLayerAlphaBlendFB alphaBlend_{};
    bool alphaBlendSupported_ = false;

    // PRIVATE_EXIT
};
// CLASS_EXIT

// ENTRY_POINT_INIT
ENTRY_POINT(VrEngine)
// ENTRY_POINT_EXIT