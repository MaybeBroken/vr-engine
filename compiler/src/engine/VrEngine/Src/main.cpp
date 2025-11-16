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
        BackgroundColor = OVR::Vector4f(0.00f, 0.1f, 0.9f, 1.0f);
        OpenXRVersion = XR_API_VERSION_1_1;
        // create ctx objects
        scene_ = std::make_unique<CTX::Scene>();
        renderer_ = CTX::CreateOpenGLRenderer();
    }
    // CLASS_INIT_EXIT

    // Returns a list of OpenXr extensions needed for this app
    // APP_EXTENSIONS_ENTRY
    virtual std::vector<const char *> GetExtensions() override
    {
        std::vector<const char *> extensions = XrApp::GetExtensions();
        // APP_EXTENSIONS_MOD_ENTRY
        // Expose common Meta/OpenXR extension names here if needed by CTX.
        // The project may require additional extensions; keep XrApp defaults.
        // APP_EXTENSIONS_MOD_EXIT
        return extensions;
    }
    // APP_EXTENSIONS_EXIT

    // APP_INIT_ENTRY
    // Must return true if the application initializes successfully.
    virtual bool AppInit(const xrJava *context) override
    {
        // APP_INIT_MOD_ENTRY

        // Initialize renderer-level resources that are independent of session
        if (renderer_)
        {
            renderer_->Initialize();
        }
        auto fileSys = std::unique_ptr<OVRFW::ovrFileSys>(OVRFW::ovrFileSys::Create(*context));
        if (fileSys)
        {
            std::string cubeModelPath = "apk:///assets/cube.obj";
            model = CTX::LoadMeshFromFile(*scene_, *fileSys, cubeModelPath);
        }
        if (!model)
        {
            ALOG("AppInit::LoadMeshFromFile FAILED.");
            return false;
        }

        // APP_INIT_MOD_EXIT
        return true;
    }
    // APP_INIT_EXIT

    // APP_SHUTDOWN_ENTRY
    virtual void AppShutdown(const xrJava *context) override
    {
        // APP_SHUTDOWN_MOD_ENTRY
        if (renderer_)
        {
            renderer_->Shutdown();
            renderer_.reset();
        }
        scene_.reset();
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
        // Load a simple cube model into the CTX scene
        scene_->roots.push_back(model);

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
        // leave renderer running across sessions if desired; otherwise shutdown here
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
        // UPDATE_MOD_EXIT
    }
    // UPDATE_EXIT

    // Render eye buffers while running
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
        // Example: call CTX renderer for each eye using provided view/projection
        if (renderer_)
        {
            // The real XrApp provides view/proj per eye; here we use a simple identity
            // as a placeholder. Integrate with real per-eye matrices in production.
            OVR::Matrix4f view = OVR::Matrix4f::Identity();
            OVR::Matrix4f proj = OVR::Matrix4f::Identity();
            renderer_->RenderScene(*scene_, view, proj);
        }
        // RENDER_MOD_EXIT
    }
    // Called by the framework when GL state for an eye is bound (framebuffer set).
    // We render the CTX scene here so we draw directly into the XR-provided swapchain image.
    virtual void AppEyeGLStateSetup(const OVRFW::ovrApplFrameIn &in, const ovrFramebuffer *fb, int eye) override
    {
        // Call base GL state setup first (viewport, scissor, clear)
        OVRFW::XrApp::AppEyeGLStateSetup(in, fb, eye);

        if (renderer_)
        {
            // Use the per-eye view/projection matrices already computed by the framework.
            // in.Eye[eye].ViewMatrix and in.Eye[eye].ProjectionMatrix hold OVR::Matrix4f
            OVR::Matrix4f view = in.Eye[eye].ViewMatrix;
            OVR::Matrix4f proj = in.Eye[eye].ProjectionMatrix;
            renderer_->RenderScene(*scene_, view, proj);
        }
    }
    // RENDER_EXIT
    // PUBLIC_EXIT

private:
    // PRIVATE_ENTRY
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;
    std::unique_ptr<CTX::Scene> scene_;
    std::unique_ptr<CTX::Renderer> renderer_;
    CTX::Node::Ptr model;

    // PRIVATE_EXIT
};
// CLASS_EXIT

// ENTRY_POINT_INIT
ENTRY_POINT(VrEngine)
// ENTRY_POINT_EXIT