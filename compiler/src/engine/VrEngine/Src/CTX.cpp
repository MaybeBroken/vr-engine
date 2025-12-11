// CTX.cpp - implementation for CTX abstraction (ModelFile + GlProgram pipeline)
#include "CTX.h"
#include "Model/ModelFileLoading.h"
#include "Model/ModelDef.h"
#include "Model/ModelAnimationUtils.h"
#include "Render/GlProgram.h"
#include "Misc/Log.h"
#include "OVR_Math.h"
#include <algorithm>
#include <cmath>

namespace CTX
{

    bool Model::globalBlockActive_ = false;
    Model *Model::globalBlockingModel_ = nullptr;

    // Local copies of SimpleGlb shader sources to match working pipeline
    namespace Shaders
    {
        static const char *Vertex = R"glsl(
    attribute highp vec4 Position;
    attribute highp vec3 Normal;
    attribute highp vec2 TexCoord;
    attribute highp vec4 JointIndices;

    varying lowp vec3 oEye;
    varying lowp vec3 oNormal;
    varying lowp vec2 oTexCoord;

    uniform JointMatrices
    {
            highp mat4 Joints[16];
    } jb;

    vec3 multiply( mat4 m, vec3 v )
    {
        return vec3(
        m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
        m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
        m[0].z * v.x + m[1].z * v.y + m[2].z * v.z );
    }

    vec3 transposeMultiply( mat4 m, vec3 v )
    {
        return vec3(
        m[0].x * v.x + m[0].y * v.y + m[0].z * v.z,
        m[1].x * v.x + m[1].y * v.y + m[1].z * v.z,
        m[2].x * v.x + m[2].y * v.y + m[2].z * v.z );
    }

    void main()
    {
            highp vec4 localPos = jb.Joints[int(JointIndices.x)] * Position;
            gl_Position = TransformVertex( localPos );
            highp vec3 eye = transposeMultiply(sm.ViewMatrix[VIEW_ID],
                                                                                    -vec3(sm.ViewMatrix[VIEW_ID][3]));
            oEye = normalize(eye - vec3( ModelMatrix * localPos ));
            highp vec3 iNormal = multiply(jb.Joints[int(JointIndices.x)], Normal);
            oNormal = normalize(multiply(ModelMatrix,  iNormal));

            oTexCoord = TexCoord;
    }
    )glsl";

        static const char *Fragment = R"glsl(
    precision lowp float;

    uniform sampler2D Texture0;
    uniform lowp vec3 SpecularLightDirection;
    uniform lowp vec3 SpecularLightColor;
    // Environment depth-based occlusion controls
    uniform sampler2D EnvironmentDepthTex; // depth texture (meters or normalized)
    uniform lowp float EnvironmentDepthEnabled; // 0.0 disabled, >0 enabled
    uniform lowp float DepthNear; // near plane in meters
    uniform lowp float DepthFar;  // far plane in meters
    uniform highp float ScreenWidth;  // framebuffer width in pixels
    uniform highp float ScreenHeight; // framebuffer height in pixels
    uniform lowp vec3 AmbientLightColor;
    uniform float Opacity;
    uniform float AlphaBlend;

    varying lowp vec3 oEye;
    varying lowp vec3 oNormal;
    varying lowp vec2 oTexCoord;

    lowp vec3 multiply( lowp mat3 m, lowp vec3 v )
    {
        return vec3(
        m[0].x * v.x + m[1].x * v.y + m[2].x * v.z,
        m[0].y * v.x + m[1].y * v.y + m[2].y * v.z,
        m[0].z * v.x + m[1].z * v.y + m[2].z * v.z );
    }

    void main()
    {
        lowp vec3 eyeDir = normalize( oEye.xyz );
        lowp vec3 Normal = normalize( oNormal );

        lowp vec3 reflectionDir = dot( eyeDir, Normal ) * 2.0 * Normal - eyeDir;
        lowp vec4 diffuse = texture2D( Texture0, oTexCoord );
        lowp vec3 ambientValue = diffuse.xyz * AmbientLightColor;

        lowp float nDotL = max( dot( Normal , SpecularLightDirection ), 0.0 );
        lowp vec3 diffuseValue = diffuse.xyz * SpecularLightColor * nDotL;

        lowp float specularPower = 1.0f - diffuse.a;
        specularPower = specularPower * specularPower;

        lowp vec3 H = normalize( SpecularLightDirection + eyeDir );
        lowp float nDotH = max( dot( Normal, H ), 0.0 );
        lowp float specularIntensity = pow( nDotH, 64.0f * ( specularPower ) ) * specularPower;
        lowp vec3 specularValue = specularIntensity * SpecularLightColor;

        // Depth-based occlusion: compare fragment depth vs environment depth
        if (EnvironmentDepthEnabled > 0.5) {
            // Linearize device depth from gl_FragCoord.z to meters
            highp float ndcZ = gl_FragCoord.z * 2.0 - 1.0;
            highp float fragDepthMeters = (2.0 * DepthNear * DepthFar) / (DepthFar + DepthNear - ndcZ * (DepthFar - DepthNear));
            // Sample environment depth using screen-space UV derived from gl_FragCoord
            highp vec2 screenUV = vec2(gl_FragCoord.x / ScreenWidth, gl_FragCoord.y / ScreenHeight);
            highp float envDepthMeters = texture2D(EnvironmentDepthTex, screenUV).r;
            // Discard fragments farther than real geometry
            if (EnvironmentDepthEnabled > 0.5) {
                float d = texture2D(EnvironmentDepthTex, screenUV).r;
                gl_FragColor = vec4(d, d, d, 1.0);
                return;
            }
            if (envDepthMeters > 0.0 && fragDepthMeters > envDepthMeters) {
                discard;
            }
        }

        lowp vec3 controllerColor = diffuseValue + ambientValue + specularValue;

        float alphaBlendFactor = max(diffuse.w, AlphaBlend) * Opacity;

        // apply alpha
        gl_FragColor.w = alphaBlendFactor;
        // premult
        gl_FragColor.xyz = controllerColor * gl_FragColor.w;

    }
    )glsl";
    } // namespace Shaders

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

    int Model::findNode(const std::string &name)
    {
        if (!modelFile_)
        {
            return -1;
        }

        for (size_t i = 0; i < modelFile_->Nodes.size(); ++i)
        {
            if (modelFile_->Nodes[i].name == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool Model::setActiveNode(const std::string &name, bool active)
    {
        if (!modelFile_ || !modelState_)
        {
            return false;
        }

        const int idx = findNode(name);
        if (idx < 0)
        {
            return false;
        }

        if (nodeVisibility_.size() != modelFile_->Nodes.size())
        {
            nodeVisibility_.assign(modelFile_->Nodes.size(), 1);
        }

        nodeVisibility_[idx] = active ? 1 : 0;
        return true;
    }

    bool Model::isNodeVisible(int nodeIndex) const
    {
        if (!modelState_)
        {
            return true;
        }

        if (nodeVisibility_.empty())
        {
            return true;
        }

        int current = nodeIndex;
        while (current >= 0 && current < static_cast<int>(nodeVisibility_.size()))
        {
            if (nodeVisibility_[current] == 0)
            {
                return false;
            }

            const auto *node = modelState_->nodeStates[current].GetNode();
            if (!node)
            {
                break;
            }
            current = node->parentIndex;
        }

        return true;
    }

    bool Model::load(OVRFW::ovrFileSys &fs, const std::string &uri)
    {
        glbBuffer_.clear();
        if (!fs.ReadFile(uri.c_str(), glbBuffer_))
        {
            ALOGE("CTX::Model::load failed for uri %s", uri.c_str());
            return false;
        }

        // Build program and load ModelFile like SimpleGlbRenderer/EnvironmentRenderer
        OVRFW::ovrProgramParm UniformParms[] = {
            {"Texture0", OVRFW::ovrProgramParmType::TEXTURE_SAMPLED},
            {"SpecularLightDirection", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"SpecularLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"EnvironmentDepthTex", OVRFW::ovrProgramParmType::TEXTURE_SAMPLED},
            {"EnvironmentDepthEnabled", OVRFW::ovrProgramParmType::FLOAT},
            {"DepthNear", OVRFW::ovrProgramParmType::FLOAT},
            {"DepthFar", OVRFW::ovrProgramParmType::FLOAT},
            {"ScreenWidth", OVRFW::ovrProgramParmType::FLOAT},
            {"ScreenHeight", OVRFW::ovrProgramParmType::FLOAT},
            {"AmbientLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"Opacity", OVRFW::ovrProgramParmType::FLOAT},
            {"AlphaBlend", OVRFW::ovrProgramParmType::FLOAT},
            {"JointMatrices", OVRFW::ovrProgramParmType::BUFFER_UNIFORM},
        };

        // Use local shader copies
        const char *vSrc = Shaders::Vertex;
        const char *fSrc = Shaders::Fragment;

        prog_ = OVRFW::GlProgram::Build(
            "",
            vSrc,
            "",
            fSrc,
            UniformParms,
            sizeof(UniformParms) / sizeof(OVRFW::ovrProgramParm));

        OVRFW::MaterialParms materials = {};
        OVRFW::ModelGlPrograms programs = {};
        programs.ProgSingleTexture = &prog_;
        programs.ProgBaseColorPBR = &prog_;
        programs.ProgSkinnedBaseColorPBR = &prog_;
        programs.ProgLightMapped = &prog_;
        programs.ProgBaseColorEmissivePBR = &prog_;
        programs.ProgSkinnedBaseColorEmissivePBR = &prog_;
        programs.ProgSimplePBR = &prog_;
        programs.ProgSkinnedSimplePBR = &prog_;

        modelFile_.reset(OVRFW::LoadModelFile_glB(
            "ctx_model",
            (const char *)glbBuffer_.data(),
            (int)glbBuffer_.size(),
            programs,
            materials));
        if (!modelFile_ || (int)modelFile_->Models.size() < 1)
        {
            ALOGE("CTX::Model::load: LoadModelFile_glB failed");
            return false;
        }

        modelState_ = std::make_unique<OVRFW::ModelState>();
        modelState_->GenerateStateFromModelFile(modelFile_.get());
        nodeVisibility_.assign(modelFile_->Nodes.size(), 1);

        const size_t kMaxJoints = 16;
        auto initJointBuffer = [&](OVRFW::GlBuffer &buffer, const std::vector<OVR::Matrix4f> &transforms)
        {
            buffer.Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_UNIFORM, kMaxJoints * sizeof(OVR::Matrix4f), nullptr);
            if (auto *ptr = (OVR::Matrix4f *)buffer.MapBuffer())
            {
                const size_t count = std::min(transforms.size(), kMaxJoints);
                for (size_t i = 0; i < count; ++i)
                {
                    ptr[i] = transforms[i];
                }
                buffer.UnmapBuffer();
            }
        };

        // Default joints buffer (identity) used for unskinned meshes or as a fallback.
        defaultJointsBuffer_ = std::make_unique<OVRFW::GlBuffer>();
        initJointBuffer(*defaultJointsBuffer_, std::vector<OVR::Matrix4f>(kMaxJoints, OVR::Matrix4f::Identity()));

        // One joint buffer per skin so skinned meshes can animate independently.
        skinJointBuffers_.clear();
        skinJointBuffers_.resize(modelFile_->Skins.size());
        for (size_t skinIdx = 0; skinIdx < modelFile_->Skins.size(); ++skinIdx)
        {
            const auto &skin = modelFile_->Skins[skinIdx];
            std::vector<OVR::Matrix4f> poseTransforms;
            if (!skin.jointIndexes.empty())
            {
                poseTransforms.resize(skin.jointIndexes.size(), OVR::Matrix4f::Identity());
                for (size_t joint = 0; joint < skin.jointIndexes.size(); ++joint)
                {
                    const int nodeIndex = skin.jointIndexes[joint];
                    poseTransforms[joint] = (modelFile_->Nodes[nodeIndex].GetGlobalTransform() *
                                             skin.inverseBindMatrices[joint])
                                                .Transposed();
                }
            }
            else
            {
                poseTransforms.resize(kMaxJoints, OVR::Matrix4f::Identity());
            }

            skinJointBuffers_[skinIdx] = std::make_unique<OVRFW::GlBuffer>();
            initJointBuffer(*skinJointBuffers_[skinIdx], poseTransforms);
        }

        // Wire default uniform pointers and GPU state like SimpleGlbRenderer
        for (auto &model : modelFile_->Models)
        {
            for (auto &s : model.surfaces)
            {
                auto &gc = s.surfaceDef.graphicsCommand;
                gc.UniformData[0].Data = &gc.Textures[0];
                gc.UniformData[1].Data = &specularDir_;
                gc.UniformData[2].Data = &specularColor_;
                // Bind environment depth uniforms to model state
                gc.UniformData[3].Data = &gc.Textures[1];
                gc.UniformData[4].Data = &envDepthEnabled_;
                gc.UniformData[5].Data = &depthNear_;
                gc.UniformData[6].Data = &depthFar_;
                // Screen size for depth sampling: driven by the environment depth
                // texture resolution, which the engine updates each frame.
                gc.UniformData[7].Data = &envDepthWidth_;
                gc.UniformData[8].Data = &envDepthHeight_;
                gc.UniformData[9].Data = &ambientColor_;
                gc.UniformData[10].Data = &opacity_;
                gc.UniformData[11].Data = &alphaBlend_;
                gc.UniformData[12].Data = defaultJointsBuffer_.get();
                gc.GpuState.depthEnable = gc.GpuState.depthMaskEnable = true;
                gc.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
                gc.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
                gc.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
                gc.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
            }
        }

        // Lighting defaults (match SimpleGlbRenderer)
        specularDir_ = OVR::Vector3f(0.75f, -0.5f, 0.0f);
        specularColor_ = OVR::Vector3f(1.0f, 0.95f, 0.8f) * 0.75f;
        ambientColor_ = OVR::Vector3f(1.0f, 1.0f, 1.0f) * 0.75f;

        animationPlaying_ = false;
        activeAnimation_ = -1;
        animationTime_ = 0.0f;

        dirty_ = true;
        return true;
    }

    void Model::setHpr(float hDeg, float pDeg, float rDeg)
    {
        rot_ = HprToQuat(hDeg, pDeg, rDeg);
        dirty_ = true;
    }

    void Model::setQuat(const OVR::Quatf &q)
    {
        rot_ = q;
        dirty_ = true;
    }

    // New: setUniformScale (or if you have non-uniform use Vector3f)
    void Model::setScale(float uniformScale)
    {
        scale_ = OVR::Vector3f(uniformScale, uniformScale, uniformScale);
        // Do NOT re-init renderer for this; apply scale in the model matrix each frame.
        dirty_ = true;
    }

    // No reinit path required in ModelFile pipeline for per-frame transform changes.

    void Model::recalculateModelTransforms()
    {
        if (!modelState_)
            return;

        for (auto &nodeState : modelState_->nodeStates)
        {
            const auto *node = nodeState.GetNode();
            if (node != nullptr && node->parentIndex < 0)
            {
                nodeState.RecalculateMatrix();
            }
        }
    }

    void Model::updateJointsForSkin(int skinIndex)
    {
        if (!modelFile_ || !modelState_)
            return;
        if (skinIndex < 0 || skinIndex >= static_cast<int>(skinJointBuffers_.size()))
            return;
        auto &bufferPtr = skinJointBuffers_[skinIndex];
        if (!bufferPtr)
            return;

        const auto &skin = modelFile_->Skins[skinIndex];
        const size_t kMaxJoints = 16;

        if (auto *ptr = (OVR::Matrix4f *)bufferPtr->MapBuffer())
        {
            const int numJoints = std::min<int>(skin.jointIndexes.size(), kMaxJoints);

            OVR::Matrix4f inverseGlobalSkeletonTransform = OVR::Matrix4f::Identity();
            if (skin.skeletonRootIndex >= 0 &&
                skin.skeletonRootIndex < static_cast<int>(modelState_->nodeStates.size()))
            {
                inverseGlobalSkeletonTransform =
                    modelState_->nodeStates[skin.skeletonRootIndex].GetGlobalTransform().Inverted();
            }
            else if (!skin.jointIndexes.empty())
            {
                const int parentIdx =
                    modelState_->nodeStates[skin.jointIndexes[0]].GetNode()->parentIndex;
                if (parentIdx >= 0 && parentIdx < static_cast<int>(modelState_->nodeStates.size()))
                {
                    inverseGlobalSkeletonTransform =
                        modelState_->nodeStates[parentIdx].GetGlobalTransform().Inverted();
                }
            }

            for (int j = 0; j < numJoints; ++j)
            {
                const int nodeIdx = skin.jointIndexes[j];
                OVR::Matrix4f globalTransform = modelState_->nodeStates[nodeIdx].GetGlobalTransform();
                OVR::Matrix4f tempTransform = inverseGlobalSkeletonTransform * globalTransform;
                OVR::Matrix4f localJointTransform;
                if (j < static_cast<int>(skin.inverseBindMatrices.size()))
                {
                    localJointTransform = tempTransform * skin.inverseBindMatrices[j];
                }
                else
                {
                    localJointTransform = tempTransform;
                }
                ptr[j] = localJointTransform.Transposed();
            }
            bufferPtr->UnmapBuffer();
        }
    }

    void Model::updatePose(bool force)
    {
        if (!modelFile_)
            return;
        if (!dirty_ && !force)
            return;
        // Compose TRS for this model
        OVR::Matrix4f transM = OVR::Matrix4f::Translation(pos_);
        OVR::Matrix4f rotM = OVR::Matrix4f(rot_);
        OVR::Matrix4f scaleM = OVR::Matrix4f::Scaling(scale_);
        transform_ = transM * rotM * scaleM;

        if (modelState_)
        {
            modelState_->SetMatrix(transform_);
            recalculateModelTransforms();
        }

        dirty_ = false;
    }

    bool Model::HasAnimations() const
    {
        return modelFile_ && !modelFile_->Animations.empty();
    }

    bool Model::PlayAnimationByIndex(int index,
                                     OVRFW::ModelAnimationTimeType mode,
                                     float speed,
                                     float startTime,
                                     bool loop,
                                     Blocking blocking)
    {
        if (!modelFile_ || !modelState_)
            return false;
        if (index < 0 || index >= static_cast<int>(modelFile_->Animations.size()))
            return false;

        PendingAnimation req{index, mode, speed, startTime, loop, blocking};

        if (isBlockedForNewAnimation(blocking))
        {
            playNext_ = req; // keep only the most recent request
            requestBreakLoopAtCycleEnd();
            // Consider the request handled (queued) so callers can update state immediately.
            return true;
        }

        // Reset any prior queue because we're about to play immediately.
        playNext_.reset();

        loopEnabled_ = loop;
        blockingMode_ = blocking;
        localBlockActive_ = (blocking == Blocking::Local || blocking == Blocking::Global);
        breakLoopAfterCycle_ = false;
        if (blocking == Blocking::Global)
        {
            globalBlockActive_ = true;
            globalBlockingModel_ = this;
        }

        activeAnimation_ = index;
        animationMode_ = loop ? mode : OVRFW::MODEL_ANIMATION_TIME_TYPE_ONCE_FORWARD;
        animationSpeed_ = speed;
        animationTime_ = startTime;
        animationPlaying_ = true;

        modelState_->CalculateAnimationFrameAndFraction(animationMode_, animationTime_);
        OVRFW::ApplyAnimation(*modelState_, activeAnimation_);
        recalculateModelTransforms();
        updatePose(true);
        return true;
    }

    bool Model::PlayAnimationByName(const std::string &name,
                                    OVRFW::ModelAnimationTimeType mode,
                                    float speed,
                                    float startTime,
                                    bool loop,
                                    Blocking blocking)
    {
        if (!modelFile_ || !modelState_)
            return false;

        auto it = std::find_if(modelFile_->Animations.begin(), modelFile_->Animations.end(),
                               [&](const OVRFW::ModelAnimation &anim)
                               { return anim.name == name; });
        if (it == modelFile_->Animations.end())
            return false;
        const int index = static_cast<int>(std::distance(modelFile_->Animations.begin(), it));
        return PlayAnimationByIndex(index, mode, speed, startTime, loop, blocking);
    }

    int Model::GetAnimationCount() const
    {
        return modelFile_ ? static_cast<int>(modelFile_->Animations.size()) : 0;
    }

    float Model::getAnimationEndTime() const
    {
        if (!modelFile_)
            return 0.0f;
        float endTime = modelFile_->animationEndTime;
        if (endTime > 0.0f)
            return endTime;

        // Fallback: derive from timelines
        for (const auto &tl : modelFile_->AnimationTimeLines)
        {
            endTime = std::max(endTime, tl.endTime);
        }
        return endTime;
    }

    bool Model::isBlockedForNewAnimation(Blocking requested) const
    {
        // Local block always blocks new attempts on this model.
        if (localBlockActive_)
        {
            return true;
        }

        // Global block prevents other models from starting animations until cleared.
        if (globalBlockActive_ && globalBlockingModel_ != this)
        {
            return true;
        }

        // If this model already holds the global block, allow replaying (e.g., queued) unless explicitly blocked.
        (void)requested;
        return false;
    }

    void Model::clearBlocking()
    {
        if (blockingMode_ == Blocking::Global && globalBlockingModel_ == this)
        {
            globalBlockActive_ = false;
            globalBlockingModel_ = nullptr;
        }

        blockingMode_ = Blocking::None;
        localBlockActive_ = false;
        breakLoopAfterCycle_ = false;
    }

    void Model::tryPlayQueued()
    {
        if (!playNext_.has_value())
        {
            return;
        }

        // Attempt to play queued animation; if still blocked, keep it queued.
        PendingAnimation pending = *playNext_;
        playNext_.reset();
        PlayAnimationByIndex(pending.index, pending.mode, pending.speed, pending.startTime, pending.loop, pending.blocking);
    }

    void Model::requestBreakLoopAtCycleEnd()
    {
        // If this model owns the block and is looping, schedule a loop break at the end
        // of the current cycle so queued animations can proceed.
        if (animationPlaying_ && loopEnabled_ && (blockingMode_ == Blocking::Local || blockingMode_ == Blocking::Global))
        {
            breakLoopAfterCycle_ = true;
        }
    }

    bool Model::NextAnimation(bool loop, Blocking blocking)
    {
        const int count = GetAnimationCount();
        if (count == 0)
            return false;
        const int nextIndex = (activeAnimation_ >= 0) ? (activeAnimation_ + 1) % count : 0;
        const auto mode = loop ? OVRFW::MODEL_ANIMATION_TIME_TYPE_LOOP_FORWARD : OVRFW::MODEL_ANIMATION_TIME_TYPE_ONCE_FORWARD;
        return PlayAnimationByIndex(nextIndex, mode, animationSpeed_, 0.0f, loop, blocking);
    }

    bool Model::PrevAnimation(bool loop, Blocking blocking)
    {
        const int count = GetAnimationCount();
        if (count == 0)
            return false;
        int nextIndex = activeAnimation_;
        if (nextIndex < 0)
        {
            nextIndex = 0;
        }
        else
        {
            nextIndex = std::max(0, nextIndex - 1);
        }
        const auto mode = loop ? OVRFW::MODEL_ANIMATION_TIME_TYPE_LOOP_FORWARD : OVRFW::MODEL_ANIMATION_TIME_TYPE_ONCE_FORWARD;
        return PlayAnimationByIndex(nextIndex, mode, animationSpeed_, 0.0f, loop, blocking);
    }

    void Model::StopAnimation()
    {
        animationPlaying_ = false;
        activeAnimation_ = -1;
        clearBlocking();
        tryPlayQueued();
    }

    void Model::SetAnimationSpeed(float speed)
    {
        animationSpeed_ = std::max(0.0f, speed);
    }

    void Model::Update(float deltaSeconds)
    {
        bool advanced = false;
        if (animationPlaying_ && modelState_ && modelFile_)
        {
            if (activeAnimation_ >= 0 && activeAnimation_ < static_cast<int>(modelFile_->Animations.size()))
            {
                const float endTime = getAnimationEndTime();
                const bool hasEnd = endTime > 0.0f;
                animationTime_ += deltaSeconds * animationSpeed_;

                if (loopEnabled_ && hasEnd && animationTime_ >= endTime)
                {
                    if (breakLoopAfterCycle_ && playNext_.has_value())
                    {
                        // Finish the current loop once, then break and hand off to queued animation.
                        animationTime_ = endTime;
                        animationPlaying_ = false;
                        loopEnabled_ = false;
                    }
                    else
                    {
                        // Wrap around to keep looping.
                        animationTime_ = std::fmod(animationTime_, endTime);
                    }
                }
                else if (loopEnabled_ && !hasEnd && breakLoopAfterCycle_ && playNext_.has_value())
                {
                    // No known end time; break immediately to release the block and play the queue.
                    animationPlaying_ = false;
                    loopEnabled_ = false;
                }
                else if (!loopEnabled_ && hasEnd && animationTime_ >= endTime)
                {
                    // Clamp to the end and stop playing.
                    animationTime_ = endTime;
                    animationPlaying_ = false;
                }
                else if (!loopEnabled_ && !hasEnd)
                {
                    // Unknown duration for non-looping clip: stop after this frame to avoid hanging.
                    animationPlaying_ = false;
                }

                modelState_->CalculateAnimationFrameAndFraction(animationMode_, animationTime_);
                OVRFW::ApplyAnimation(*modelState_, activeAnimation_);
                recalculateModelTransforms();
                advanced = true;

                if (!animationPlaying_)
                {
                    clearBlocking();
                    tryPlayQueued();
                }
            }
            else
            {
                animationPlaying_ = false;
                clearBlocking();
                tryPlayQueued();
            }
        }

        updatePose(advanced);

        if (!animationPlaying_)
        {
            tryPlayQueued();
        }
    }

    void Model::emitSurfaces(std::vector<OVRFW::ovrDrawSurface> &surfaces)
    {
        if (!modelFile_)
            return;
        // Ensure base transform is up to date in case Update was not called this frame.
        updatePose(false);

        if (modelState_)
        {
            for (auto &nodeState : modelState_->nodeStates)
            {
                const int nodeIndex = static_cast<int>(&nodeState - modelState_->nodeStates.data());
                if (!isNodeVisible(nodeIndex))
                {
                    continue;
                }

                const auto *node = nodeState.GetNode();
                if (node == nullptr || node->model == nullptr)
                {
                    continue;
                }

                OVRFW::GlBuffer *jointBuffer = defaultJointsBuffer_.get();
                if (node->skinIndex >= 0 && node->skinIndex < static_cast<int>(skinJointBuffers_.size()))
                {
                    updateJointsForSkin(node->skinIndex);
                    jointBuffer = skinJointBuffers_[node->skinIndex].get();
                }

                for (auto &s : node->model->surfaces)
                {
                    auto &gc = s.surfaceDef.graphicsCommand;
                    gc.UniformData[12].Data = jointBuffer;
                    gc.Textures[1].texture = envDepthTex_;
                    gc.Textures[1].target = GL_TEXTURE_2D;
                    gc.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
                    gc.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
                    gc.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
                    gc.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;

                    OVRFW::ovrDrawSurface out{};
                    out.surface = &s.surfaceDef;
                    out.modelMatrix = nodeState.GetGlobalTransform();
                    surfaces.push_back(out);
                }
            }
            return;
        }

        // Fallback path if animation state is missing: render with root transform only.
        for (auto &model : modelFile_->Models)
        {
            for (auto &s : model.surfaces)
            {
                auto &gc = s.surfaceDef.graphicsCommand;
                gc.Textures[1].texture = envDepthTex_;
                gc.Textures[1].target = GL_TEXTURE_2D;
                gc.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
                gc.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
                gc.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
                gc.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;

                OVRFW::ovrDrawSurface out{};
                out.surface = &s.surfaceDef;
                out.modelMatrix = transform_;
                surfaces.push_back(out);
            }
        }
    }

} // namespace CTX
