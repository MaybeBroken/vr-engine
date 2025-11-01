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
// #INCLUDES_MARKER

class XrControllersApp : public OVRFW::XrApp
{
public:
    XrControllersApp() : OVRFW::XrApp()
    {
        BackgroundColor = OVR::Vector4f(0.00f, 0.1f, 0.9f, 1.0f);
        OpenXRVersion = XR_API_VERSION_1_1;
    }

    // Returns a list of OpenXr extensions needed for this app
    virtual std::vector<const char *> GetExtensions() override
    {
        std::vector<const char *> extensions = XrApp::GetExtensions();
        return extensions;
    }

    // Must return true if the application initializes successfully.
    virtual bool AppInit(const xrJava *context) override
    {
        if (false == ui_.Init(context, GetFileSys()))
        {
            ALOG("TinyUI::Init FAILED.");
            return false;
        }

        return true;
    }

    virtual void AppShutdown(const xrJava *context) override
    {
        /// unhook extensions

        OVRFW::XrApp::AppShutdown(context);
        ui_.Shutdown();
    }

    virtual bool SessionInit() override
    {
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
        beamRenderer_.Init(GetFileSys(), nullptr, OVR::Vector4f(1.0f), 1.0f);

        return true;
    }

    virtual void SessionEnd() override
    {
        controllerRenderL_.Shutdown();
        controllerRenderR_.Shutdown();
        beamRenderer_.Shutdown();
    }

    // Update state
    virtual void Update(const OVRFW::ovrApplFrameIn &in) override
    {

        /*
         */

        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Update(in.LeftRemotePose);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Update(in.RightRemotePose);
        }
        ui_.Update(in);

        /// Add some deliberate lag to the app
        if (delayUI_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

    // Render eye buffers while running
    virtual void Render(const OVRFW::ovrApplFrameIn &in, OVRFW::ovrRendererOutput &out) override
    {
        /// Render UI
        ui_.Render(in, out);
        /// Render controllers
        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Render(out.Surfaces);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Render(out.Surfaces);
        }

        /// Render beams
        beamRenderer_.Render(in, out);
    }

public:
private:
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;
    OVRFW::TinyUI ui_;
    OVRFW::SimpleBeamRenderer beamRenderer_;
    std::vector<OVRFW::ovrBeamRenderer::handle_t> beams_;

    /// UI lag
    bool delayUI_ = false;
};

ENTRY_POINT(XrControllersApp)
