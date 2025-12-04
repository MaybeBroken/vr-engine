#include "EnvironmentDepthProvider.h"
#include <cstring>
#include "OVR_Math.h"
#include <openxr/openxr.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

bool EnvironmentDepthProvider::Init(XrInstance instance, XrSession session)
{
    instance_ = instance;
    session_ = session;
    supported_ = false;

    // Check enabled extensions by trying to fetch function pointers
    xrGetInstanceProcAddr(instance_, "xrCreateEnvironmentDepthMETA", (PFN_xrVoidFunction *)&pfnCreate_);
    xrGetInstanceProcAddr(instance_, "xrDestroyEnvironmentDepthMETA", (PFN_xrVoidFunction *)&pfnDestroy_);
    xrGetInstanceProcAddr(instance_, "xrGetEnvironmentDepthFrameMETA", (PFN_xrVoidFunction *)&pfnGetFrame_);
    if (!pfnCreate_ || !pfnGetFrame_)
    {
        return false;
    }

    // Create environment depth handle (placeholder create info); replace with real struct when available.
    struct CreateInfoStub
    {
        XrStructureType type;
        const void *next;
    } ci{(XrStructureType)1000, nullptr};
    // XrEnvironmentDepthImageMETA is a struct in newer headers; default-initialize and pass by pointer.
    XrEnvironmentDepthImageMETA h{};
    if (pfnCreate_(session_, &ci, &h) != XR_SUCCESS)
    {
        return false;3
    }
    handle_ = h;
    handleCreated_ = true;
    supported_ = true;

    if (depthTex_ == 0)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        depthTex_ = tex;
    }
    return true;
}

void EnvironmentDepthProvider::Shutdown()
{
    if (pfnDestroy_ && handleCreated_)
    {
        pfnDestroy_(handle_);
        handleCreated_ = false;
    }
}

bool EnvironmentDepthProvider::AcquireAndUpload(XrTime predictedTime)
{
    if (!supported_ || handle_ == (XrEnvironmentDepthImageMETA)XR_NULL_HANDLE)
        return false;

    struct DepthInfoStub
    {
        XrStructureType type;
        const void *next;
        int width;
        int height;
        const void *pixels; // pointer to float meters data
    } di{(XrStructureType)1001, nullptr, 0, 0, nullptr};

    if (pfnGetFrame_(handle_, predictedTime, &di) != XR_SUCCESS || di.pixels == nullptr || di.width <= 0 || di.height <= 0)
    {
        return false;
    }

    // Upload to GL R32F texture
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    if (di.width != lastW_ || di.height != lastH_)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, di.width, di.height, 0, GL_RED, GL_FLOAT, di.pixels);
        lastW_ = di.width;
        lastH_ = di.height;
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, di.width, di.height, GL_RED, GL_FLOAT, di.pixels);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}
