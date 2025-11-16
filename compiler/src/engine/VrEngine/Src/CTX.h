// CTX.h - abstraction layer for loading, controlling, and rendering glb models
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "OVR_Math.h"
#include "OVR_FileSys.h"
#include "Model/ModelFile.h"
#include "Render/GlProgram.h"
#include "Render/GlBuffer.h"

namespace CTX
{

    class Model
    {
    public:
        Model() = default;
        bool load(OVRFW::ovrFileSys &fs, const std::string &uri);
        void setPos(float x, float y, float z)
        {
            pos_ = OVR::Vector3f(x, y, z);
            dirty_ = true;
        }
        void setScale(float s);
        void setHpr(float hDeg, float pDeg, float rDeg);
        bool isLoaded() const { return modelFile_ != nullptr; }
        void updatePose();
        void emitSurfaces(std::vector<OVRFW::ovrDrawSurface> &surfaces);

    private:
        std::unique_ptr<OVRFW::ModelFile> modelFile_;
        OVRFW::GlProgram prog_{};
        std::unique_ptr<OVRFW::GlBuffer> jointsBuffer_;
        // simple lighting
        OVR::Vector3f specularDir_{1.0f, 1.0f, 0.0f};
        OVR::Vector3f specularColor_{1.0f, 0.95f, 0.8f};
        OVR::Vector3f ambientColor_{1.0f, 1.0f, 1.0f};
        float opacity_ = 1.0f;
        float alphaBlend_ = 1.0f;
        // transforms
        OVR::Matrix4f transform_{};
        std::vector<uint8_t> glbBuffer_;
        OVR::Vector3f pos_{0.0f};
        OVR::Vector3f scale_{1.0f};
        OVR::Quatf rot_{0.0f, 0.0f, 0.0f, 1.0f};
        bool dirty_ = false;
    };

    class Context
    {
    public:
        Model &LoadModel(OVRFW::ovrFileSys &fs, const std::string &uri)
        {
            models_.emplace_back();
            models_.back().load(fs, uri);
            return models_.back();
        }
        void RenderAll(std::vector<OVRFW::ovrDrawSurface> &surfaces)
        {
            for (auto &m : models_)
            {
                if (m.isLoaded())
                {
                    m.updatePose();
                    m.emitSurfaces(surfaces);
                }
            }
        }
        std::vector<Model> &Models() { return models_; }

    private:
        std::vector<Model> models_;
        OVR::Matrix4f modelMatrix_;
    };

} // namespace CTX
