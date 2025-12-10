// CTX.cpp - implementation for CTX abstraction (ModelFile + GlProgram pipeline)
#include "CTX.h"
#include "Model/ModelFileLoading.h"
#include "Model/ModelDef.h"
#include "Model/ModelAnimationUtils.h"
#include "Render/GlProgram.h"
#include "Misc/Log.h"
#include "OVR_Math.h"
#include <algorithm>

namespace CTX
{

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
        specularDir_ = OVR::Vector3f(1.0f, 1.0f, 0.0f);
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
                                     float startTime)
    {
        if (!modelFile_ || !modelState_)
            return false;
        if (index < 0 || index >= static_cast<int>(modelFile_->Animations.size()))
            return false;

        activeAnimation_ = index;
        animationMode_ = mode;
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
                                    float startTime)
    {
        if (!modelFile_ || !modelState_)
            return false;

        auto it = std::find_if(modelFile_->Animations.begin(), modelFile_->Animations.end(),
                               [&](const OVRFW::ModelAnimation &anim)
                               { return anim.name == name; });
        if (it == modelFile_->Animations.end())
            return false;
        const int index = static_cast<int>(std::distance(modelFile_->Animations.begin(), it));
        return PlayAnimationByIndex(index, mode, speed, startTime);
    }

    int Model::GetAnimationCount() const
    {
        return modelFile_ ? static_cast<int>(modelFile_->Animations.size()) : 0;
    }

    bool Model::NextAnimation()
    {
        const int count = GetAnimationCount();
        if (count == 0)
            return false;
        const int nextIndex = (activeAnimation_ >= 0) ? (activeAnimation_ + 1) % count : 0;
        return PlayAnimationByIndex(nextIndex, animationMode_, animationSpeed_, 0.0f);
    }

    bool Model::PrevAnimation()
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
        return PlayAnimationByIndex(nextIndex, animationMode_, animationSpeed_, 0.0f);
    }

    void Model::StopAnimation()
    {
        animationPlaying_ = false;
        activeAnimation_ = -1;
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
                animationTime_ += deltaSeconds * animationSpeed_;
                modelState_->CalculateAnimationFrameAndFraction(animationMode_, animationTime_);
                OVRFW::ApplyAnimation(*modelState_, activeAnimation_);
                recalculateModelTransforms();
                advanced = true;
            }
            else
            {
                animationPlaying_ = false;
            }
        }

        updatePose(advanced);
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
