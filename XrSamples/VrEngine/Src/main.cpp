#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <openxr/openxr.h>

#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>

#include "XrApp.h"

// PCM Haptic API
PFN_xrGetDeviceSampleRateFB xrGetDeviceSampleRateFB = nullptr;

#include "Input/ControllerRenderer.h"
#include "Input/TinyUI.h"
#include "Render/SimpleBeamRenderer.h"

// INCLUDES_ENTRY

//

// INCLUDES_END
// VAR_SPACE_ENTRY

//

// VAR_SPACE_EXIT
// CLASS_ENTRY>@delimiter=VrEngine
class VrEngine : public OVRFW::XrApp
{
public:
    // PUBLIC_ENTRY

    // CLASS_INIT_ENTRY
    VrEngine() : OVRFW::XrApp()
    {
        BackgroundColor = OVR::Vector4f(0.00f, 0.1f, 0.9f, 1.0f);
        OpenXRVersion = XR_API_VERSION_1_1;
    }
    // CLASS_INIT_EXIT

    // Returns a list of OpenXr extensions needed for this app
    // APP_EXTENSIONS_ENTRY
    virtual std::vector<const char *> GetExtensions() override
    {
        std::vector<const char *> extensions = XrApp::GetExtensions();
        // APP_EXTENSIONS_MOD_ENTRY

        // APP_EXTENSIONS_MOD_EXIT
        return extensions;
    }
    // APP_EXTENSIONS_EXIT

    // APP_INIT_ENTRY
    // Must return true if the application initializes successfully.
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
        // RENDER_MOD_EXIT
    }
    // RENDER_EXIT
    // PUBLIC_EXIT

private:
    // PRIVATE_ENTRY
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;
    // PRIVATE_EXIT
};
// CLASS_EXIT

// ENTRY_POINT_INIT
ENTRY_POINT(VrEngine)
// ENTRY_POINT_EXIT