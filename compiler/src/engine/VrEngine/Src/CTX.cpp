// CTX.cpp - implementation for CTX abstraction (ModelFile + GlProgram pipeline)
#include "CTX.h"
#include "Model/ModelFileLoading.h"
#include "Model/ModelDef.h"
#include "Render/GlProgram.h"
#include "Misc/Log.h"
#include "OVR_Math.h"

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
            // Sample environment depth from R channel
            highp float envDepthMeters = texture2D(EnvironmentDepthTex, oTexCoord).r;
            // Discard fragments farther than real geometry
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

        jointsBuffer_.reset(new OVRFW::GlBuffer());
        const size_t kMaxJoints = 16;
        jointsBuffer_->Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_UNIFORM, kMaxJoints * sizeof(OVR::Matrix4f), nullptr);

        // Fill joints buffer with default pose or identity
        std::vector<OVR::Matrix4f> poseTransforms;
        if (!modelFile_->Skins.empty())
        {
            poseTransforms.resize(modelFile_->Skins[0].jointIndexes.size());
            for (size_t joint = 0; joint < modelFile_->Skins[0].jointIndexes.size(); ++joint)
            {
                poseTransforms[joint] = (modelFile_->Nodes[modelFile_->Skins[0].jointIndexes[joint]].GetGlobalTransform() *
                                         modelFile_->Skins[0].inverseBindMatrices[joint])
                                            .Transposed();
            }
        }
        else
        {
            poseTransforms.resize(kMaxJoints, OVR::Matrix4f::Identity());
        }
        if (auto *ptr = (OVR::Matrix4f *)jointsBuffer_->MapBuffer())
        {
            for (size_t i = 0; i < poseTransforms.size() && i < kMaxJoints; ++i)
                ptr[i] = poseTransforms[i];
            jointsBuffer_->UnmapBuffer();
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
                gc.UniformData[7].Data = &ambientColor_;
                gc.UniformData[8].Data = &opacity_;
                gc.UniformData[9].Data = &alphaBlend_;
                gc.UniformData[10].Data = jointsBuffer_.get();
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

    void Model::updatePose()
    {
        if (!modelFile_ || !dirty_)
            return;
        // Compose TRS for this model
        OVR::Matrix4f transM = OVR::Matrix4f::Translation(pos_);
        OVR::Matrix4f rotM = OVR::Matrix4f(rot_);
        OVR::Matrix4f scaleM = OVR::Matrix4f::Scaling(scale_);
        transform_ = transM * rotM * scaleM;
        dirty_ = false;
    }

    void Model::emitSurfaces(std::vector<OVRFW::ovrDrawSurface> &surfaces)
    {
        if (!modelFile_)
            return;
        for (auto &model : modelFile_->Models)
        {
            for (auto &s : model.surfaces)
            {
                OVRFW::ovrDrawSurface out{};
                out.surface = &s.surfaceDef;
                out.modelMatrix = transform_;
                // Ensure alpha blending stays enabled for passthrough-friendly composition.
                // This uses premultiplied alpha in the fragment shader already.
                s.surfaceDef.graphicsCommand.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
                s.surfaceDef.graphicsCommand.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
                s.surfaceDef.graphicsCommand.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
                s.surfaceDef.graphicsCommand.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
                surfaces.push_back(out);
            }
        }
    }

} // namespace CTX
