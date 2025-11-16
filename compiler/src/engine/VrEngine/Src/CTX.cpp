// CTX.cpp - implementation for CTX abstraction
#include "CTX.h"
#include "Model/ModelFileLoading.h"
#include "Misc/Log.h"
#include "OVR_Math.h"

namespace CTX
{

    static OVR::Quatf HprToQuat(float hDeg, float pDeg, float rDeg)
    {
        const float h = hDeg * (MATH_FLOAT_PI / 180.0f);
        const float p = pDeg * (MATH_FLOAT_PI / 180.0f);
        const float r = rDeg * (MATH_FLOAT_PI / 180.0f);
        OVR::Quatf qH(OVR::Vector3f(0, 1, 0), h);
        OVR::Quatf qP(OVR::Vector3f(1, 0, 0), p);
        OVR::Quatf qR(OVR::Vector3f(0, 0, 1), r);
        return qH * qP * qR; // heading then pitch then roll
    }

    bool Model::load(OVRFW::ovrFileSys &fs, const std::string &uri)
    {
        glbBuffer_.clear();
        if (!fs.ReadFile(uri.c_str(), glbBuffer_))
        {
            ALOGE("CTX::Model::load failed for uri %s", uri.c_str());
            return false;
        }
        renderer_ = std::make_unique<OVRFW::SimpleGlbRenderer>();
        OVR::Matrix4f poseCorrection; // identity
        if (!renderer_->Init(glbBuffer_, poseCorrection))
        {
            ALOGE("CTX::Model::load renderer init failed for %s", uri.c_str());
            renderer_.reset();
            return false;
        }
        dirty_ = true;
        return true;
    }

    void Model::setHpr(float hDeg, float pDeg, float rDeg)
    {
        rot_ = HprToQuat(hDeg, pDeg, rDeg);
        dirty_ = true;
    }

    void Model::updatePose()
    {
        if (!renderer_ || !dirty_)
            return;
        OVR::Posef pose(rot_, pos_);
        renderer_->Update(pose);
        dirty_ = false;
    }

    void Model::emitSurfaces(std::vector<OVRFW::ovrDrawSurface> &surfaces)
    {
        if (!renderer_ || !renderer_->IsInitialized())
            return;
        // SimpleGlbRenderer handles its own surface emission with current pose.
        renderer_->Render(surfaces);
        // Apply scale by post-multiplying modelMatrix of emitted surfaces.
        if (scale_ != OVR::Vector3f(1.0f))
        {
            for (auto &s : surfaces)
            {
                // naive uniform scaling in place (could keep original count to only scale new surfaces)
                OVR::Matrix4f scaleM = OVR::Matrix4f::Scaling(scale_);
                s.modelMatrix = s.modelMatrix * scaleM;
            }
        }
    }

} // namespace CTX
