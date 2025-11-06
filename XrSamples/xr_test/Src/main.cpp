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
class VrEngine : public OVRFW::XrApp
{
public:
    VrEngine() : OVRFW::XrApp()
    {
        BackgroundColor = OVR::Vector4f(0.00f, 0.1f, 0.9f, 0.3f);
        OpenXRVersion = XR_API_VERSION_1_1;
    }
    virtual std::vector<const char *> GetExtensions() override
    {
        std::vector<const char *> extensions = XrApp::GetExtensions();
        return extensions;
    }
    virtual bool AppInit(const xrJava *context) override
    {
        return true;
    }
    virtual void AppShutdown(const xrJava *context) override
    {
        OVRFW::XrApp::AppShutdown(context);
    }
    virtual bool SessionInit() override
    {
        CurrentSpace = LocalSpace;
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
    }
    virtual void SessionEnd() override
    {
        controllerRenderL_.Shutdown();
        controllerRenderR_.Shutdown();
    }
    virtual void Update(const OVRFW::ovrApplFrameIn &in) override
    {
        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Update(in.LeftRemotePose);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Update(in.RightRemotePose);
        }
    }
    virtual void Render(const OVRFW::ovrApplFrameIn &in, OVRFW::ovrRendererOutput &out) override
    {
        if (in.LeftRemoteTracked)
        {
            controllerRenderL_.Render(out.Surfaces);
        }
        if (in.RightRemoteTracked)
        {
            controllerRenderR_.Render(out.Surfaces);
        }
    }
private:
    OVRFW::ControllerRenderer controllerRenderL_;
    OVRFW::ControllerRenderer controllerRenderR_;
};
ENTRY_POINT(VrEngine)