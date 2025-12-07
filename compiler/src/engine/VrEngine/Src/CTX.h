// CTX.h - abstraction layer for loading, controlling, and rendering glb models
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include "OVR_Math.h"
#include "OVR_FileSys.h"
#include "Model/ModelFile.h"
#include "Render/GlProgram.h"
#include "Render/GlBuffer.h"
// Ensure GLuint type is available for texture IDs
#include <GLES3/gl3.h>

namespace CTX
{
    // Simple action types for binding from engine input/gestures
    enum class Action
    {
        PinchLeft,
        PinchRight,
        ButtonA,
        ButtonB,
        SwipeLeft,
        SwipeRight
    };

    struct ActionEvent
    {
        Action action;
        float value;            // e.g., pinch strength [0..1]
        OVR::Vector3f position; // world-space hint (optional)
        bool active;            // true on press/gesture on, false on release
    };

    class Model
    {
    public:
        Model() = default;
        bool load(OVRFW::ovrFileSys &fs, const std::string &uri);
        // Position, rotation, and scale controls
        void setPos(float x, float y, float z)
        {
            pos_ = OVR::Vector3f(x, y, z);
            dirty_ = true;
        }
        OVR::Vector3f getPos() const { return pos_; }
        OVR::Vector3f getScale() const { return scale_; }
        OVR::Quatf getHpr() const { return rot_; }
        void setScale(float s);
        void setHpr(float hDeg, float pDeg, float rDeg);
        bool isLoaded() const { return modelFile_ != nullptr; }
        void updatePose();
        void emitSurfaces(std::vector<OVRFW::ovrDrawSurface> &surfaces);
        // Passthrough helpers
        void setOpacity(float o)
        {
            opacity_ = o;
            dirty_ = true;
        }
        void setAlphaBlend(float a)
        {
            alphaBlend_ = a;
            dirty_ = true;
        }
        // Environment depth occlusion controls
        void setEnvironmentDepthTexture(GLuint glTex)
        {
            envDepthTex_ = glTex;
            dirty_ = true;
        }
        void setEnvironmentDepthEnabled(bool enabled)
        {
            envDepthEnabled_ = enabled ? 1.0f : 0.0f;
            dirty_ = true;
        }
        void setEnvironmentDepthRange(float nearMeters, float farMeters)
        {
            depthNear_ = nearMeters;
            depthFar_ = farMeters;
            dirty_ = true;
        }

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
        // environment depth
        GLuint envDepthTex_ = 0;
        float envDepthEnabled_ = 0.0f;
        float depthNear_ = 0.1f;
        float depthFar_ = 10.0f;
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
            models_.emplace_back(std::make_unique<Model>());
            models_.back()->load(fs, uri);
            return *models_.back();
        }
        // Enable a simple passthrough-friendly mode by ensuring alpha blending
        // is active and model opacity/alpha allow camera feed to show through.
        void EnablePassthrough(bool enable)
        {
            passthroughEnabled_ = enable;
            for (auto &mp : models_)
            {
                auto &m = *mp;
                if (enable)
                {
                    // Prefer using texture alpha and keep geometry opaque where intended.
                    // Setting AlphaBlend to 0 lets diffuse.a drive transparency.
                    m.setAlphaBlend(0.0f);
                    m.setOpacity(1.0f);
                }
                else
                {
                    m.setAlphaBlend(1.0f);
                    m.setOpacity(1.0f);
                }
            }
        }
        void RenderAll(std::vector<OVRFW::ovrDrawSurface> &surfaces)
        {
            for (auto &mp : models_)
            {
                auto &m = *mp;
                if (m.isLoaded())
                {
                    m.updatePose();
                    m.emitSurfaces(surfaces);
                }
            }
        }
        std::vector<std::unique_ptr<Model>> &Models() { return models_; }

        // Bind/unbind callbacks for actions
        using Callback = std::function<void(const ActionEvent &)>;
        void Bind(Action action, Callback cb)
        {
            callbacks_[action] = std::move(cb);
        }
        void Unbind(Action action)
        {
            callbacks_.erase(action);
        }
        // Trigger from engine
        void Trigger(const ActionEvent &evt)
        {
            auto it = callbacks_.find(evt.action);
            if (it != callbacks_.end() && it->second)
            {
                it->second(evt);
            }
        }

    private:
        std::vector<std::unique_ptr<Model>> models_;
        OVR::Matrix4f modelMatrix_;
        bool passthroughEnabled_ = false;
        std::unordered_map<Action, Callback> callbacks_;
    };

} // namespace CTX
