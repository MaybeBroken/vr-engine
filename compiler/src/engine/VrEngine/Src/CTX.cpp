// CTX.cpp - implementation for CTX abstraction (ModelFile + GlProgram pipeline)
#include "CTX.h"
#include "Model/ModelFileLoading.h"
#include "Model/ModelDef.h"
#include "Render/GlProgram.h"
#include "Misc/Log.h"
#include "OVR_Math.h"
#include <openxr/openxr.h>

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
                gc.UniformData[3].Data = &ambientColor_;
                gc.UniformData[4].Data = &opacity_;
                gc.UniformData[5].Data = &alphaBlend_;
                gc.UniformData[6].Data = jointsBuffer_.get();
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
        ambientColor_ = OVR::Vector3f(1.0f, 1.0f, 1.0f) * 0.15f;

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
                if (roomViewsEnabled_ && !sceneMeshes_.empty())
                {
                    EmitRoomMeshes(sceneMeshes_, surfaces);
                }
                // Emit hand lines if enabled
                if (handViewsEnabled_ && handVbo_ && handIbo_)
                {
                    OVRFW::ovrDrawSurface out{};
                    handSurfaceDef_.graphicsCommand.indexCount = handIndexCount_;
                    out.surface = &handSurfaceDef_;
                    out.modelMatrix = OVR::Matrix4f::Identity();
                    surfaces.push_back(out);
                }
                surfaces.push_back(out);
            }
        }
    }

    // XR_FB_passthrough typedefs
    typedef XrResult(XRAPI_PTR *PFN_xrCreatePassthroughFB)(XrSession session, const XrPassthroughCreateInfoFB *createInfo, XrPassthroughFB *passthrough);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyPassthroughFB)(XrPassthroughFB passthrough);
    typedef XrResult(XRAPI_PTR *PFN_xrPassthroughStartFB)(XrPassthroughFB passthrough);
    typedef XrResult(XRAPI_PTR *PFN_xrCreatePassthroughLayerFB)(XrSession session, const XrPassthroughLayerCreateInfoFB *createInfo, XrPassthroughLayerFB *outLayer);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyPassthroughLayerFB)(XrPassthroughLayerFB layer);
    typedef XrResult(XRAPI_PTR *PFN_xrPassthroughLayerResumeFB)(XrPassthroughLayerFB layer);
    typedef XrResult(XRAPI_PTR *PFN_xrPassthroughLayerPauseFB)(XrPassthroughLayerFB layer);

    void Context::InitPassthrough(XrInstance instance, XrSession session, const std::vector<const char *> &enabledExts)
    {
        bool hasFbPT = false;
        for (auto e : enabledExts)
        {
            if (strcmp(e, XR_FB_PASSTHROUGH_EXTENSION_NAME) == 0)
                hasFbPT = true;
        }
        if (!hasFbPT)
        {
            ALOG("XR_FB_passthrough extension not enabled; skipping init");
            return;
        }

        // FB procs
        PFN_xrCreatePassthroughFB pfnCreatePassthroughFB = nullptr;
        PFN_xrDestroyPassthroughFB pfnDestroyPassthroughFB = nullptr;
        PFN_xrPassthroughStartFB pfnPassthroughStartFB = nullptr;
        PFN_xrCreatePassthroughLayerFB pfnCreatePassthroughLayerFB = nullptr;
        PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayerFB = nullptr;
        PFN_xrPassthroughLayerResumeFB pfnPassthroughLayerResumeFB = nullptr;

        auto loadProc = [&](const char *name, void **fn)
        {
            return xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)fn);
        };

        // Load FB functions only
        XrResult rpCreate = loadProc("xrCreatePassthroughFB", (void **)&pfnCreatePassthroughFB);
        loadProc("xrDestroyPassthroughFB", (void **)&pfnDestroyPassthroughFB);
        loadProc("xrPassthroughStartFB", (void **)&pfnPassthroughStartFB);
        XrResult rpLayerCreate = loadProc("xrCreatePassthroughLayerFB", (void **)&pfnCreatePassthroughLayerFB);
        loadProc("xrDestroyPassthroughLayerFB", (void **)&pfnDestroyPassthroughLayerFB);
        loadProc("xrPassthroughLayerResumeFB", (void **)&pfnPassthroughLayerResumeFB);
        ALOG("CTX FB PT procs: create=%d layerCreate=%d", rpCreate, rpLayerCreate);
        if (rpCreate != XR_SUCCESS || rpLayerCreate != XR_SUCCESS)
        {
            ALOGW("CTX Passthrough procs missing (FB); PT disabled");
            return;
        }

        XrPassthroughCreateInfoFB ptCreate{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
        ptCreate.flags = 0;
        XrResult rCreate = pfnCreatePassthroughFB(session, &ptCreate, &xrPassthrough_);
        ALOG("CTX xrCreatePassthroughFB -> %d handle=%p", rCreate, (void *)xrPassthrough_);
        if (rCreate != XR_SUCCESS)
            return;
        XrResult rStart = pfnPassthroughStartFB(xrPassthrough_);
        ALOG("CTX xrPassthroughStartFB -> %d", rStart);

        XrPassthroughLayerCreateInfoFB layerCreate{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
        layerCreate.passthrough = xrPassthrough_;
        layerCreate.flags = 0;
        layerCreate.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
        XrResult rLayerCreate = pfnCreatePassthroughLayerFB(session, &layerCreate, &xrPassthroughLayer_);
        ALOG("CTX xrCreatePassthroughLayerFB -> %d handle=%p", rLayerCreate, (void *)xrPassthroughLayer_);
        if (rLayerCreate != XR_SUCCESS)
            return;
        XrResult rLayerResume = pfnPassthroughLayerResumeFB(xrPassthroughLayer_);
        ALOG("CTX xrPassthroughLayerResumeFB -> %d", rLayerResume);
    }

    void Context::ShutdownPassthrough(XrInstance instance)
    {
        PFN_xrDestroyPassthroughLayerFB pfnDestroyPassthroughLayer = nullptr;
        PFN_xrDestroyPassthroughFB pfnDestroyPassthrough = nullptr;
        xrGetInstanceProcAddr(instance, "xrDestroyPassthroughLayerFB", (PFN_xrVoidFunction *)&pfnDestroyPassthroughLayer);
        xrGetInstanceProcAddr(instance, "xrDestroyPassthroughFB", (PFN_xrVoidFunction *)&pfnDestroyPassthrough);
        if (pfnDestroyPassthroughLayer && xrPassthroughLayer_ != XR_NULL_HANDLE)
        {
            pfnDestroyPassthroughLayer(xrPassthroughLayer_);
        }
        if (pfnDestroyPassthrough && xrPassthrough_ != XR_NULL_HANDLE)
        {
            pfnDestroyPassthrough(xrPassthrough_);
        }
        xrPassthroughLayer_ = XR_NULL_HANDLE;
        xrPassthrough_ = XR_NULL_HANDLE;
    }

    // XR_FB_scene typedefs
    typedef XrResult(XRAPI_PTR *PFN_xrCreateSceneObserverFB)(XrSession session, const XrSceneObserverCreateInfoFB *info, XrSceneObserverFB *observer);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroySceneObserverFB)(XrSceneObserverFB observer);
    typedef XrResult(XRAPI_PTR *PFN_xrCreateSceneFB)(XrSession session, const XrSceneCreateInfoFB *info, XrSceneFB *scene);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroySceneFB)(XrSceneFB scene);
    typedef XrResult(XRAPI_PTR *PFN_xrComputeNewSceneFB)(XrSceneObserverFB observer, const XrSceneComputeInfoFB *info);
    typedef XrResult(XRAPI_PTR *PFN_xrGetSceneComponentsFB)(XrSceneFB scene, const XrSceneComponentsGetInfoFB *info, uint32_t capacityInput, uint32_t *countOutput, XrSceneComponentFB *components);
    typedef XrResult(XRAPI_PTR *PFN_xrGetSceneMeshBuffersFB)(XrSceneFB scene, const XrSceneMeshBuffersGetInfoFB *info, XrSceneMeshBuffersFB *buffers);

    void Context::EnableRoomViews(XrInstance instance, XrSession session, bool enable)
    {
        roomViewsEnabled_ = enable;
        sceneMeshes_.clear();
        if (!enable)
            return;

        // Check extension
        PFN_xrCreateSceneObserverFB pfnCreateSceneObserver = nullptr;
        PFN_xrDestroySceneObserverFB pfnDestroySceneObserver = nullptr;
        PFN_xrCreateSceneFB pfnCreateScene = nullptr;
        PFN_xrDestroySceneFB pfnDestroyScene = nullptr;
        PFN_xrComputeNewSceneFB pfnComputeNewScene = nullptr;
        PFN_xrGetSceneComponentsFB pfnGetSceneComponents = nullptr;
        PFN_xrGetSceneMeshBuffersFB pfnGetSceneMeshBuffers = nullptr;
        auto loadProc = [&](const char *name, void **fn)
        {
            return xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)fn);
        };
        XrResult r0 = loadProc("xrCreateSceneObserverFB", (void **)&pfnCreateSceneObserver);
        loadProc("xrDestroySceneObserverFB", (void **)&pfnDestroySceneObserver);
        XrResult r1 = loadProc("xrCreateSceneFB", (void **)&pfnCreateScene);
        loadProc("xrDestroySceneFB", (void **)&pfnDestroyScene);
        XrResult r2 = loadProc("xrComputeNewSceneFB", (void **)&pfnComputeNewScene);
        XrResult r3 = loadProc("xrGetSceneComponentsFB", (void **)&pfnGetSceneComponents);
        XrResult r4 = loadProc("xrGetSceneMeshBuffersFB", (void **)&pfnGetSceneMeshBuffers);
        if (r0 != XR_SUCCESS || r1 != XR_SUCCESS || r2 != XR_SUCCESS || r3 != XR_SUCCESS || r4 != XR_SUCCESS)
        {
            ALOGW("Scene Understanding procs missing; room views disabled");
            return;
        }

        // Create observer and scene
        XrSceneObserverFB observer = XR_NULL_HANDLE;
        XrSceneObserverCreateInfoFB obsInfo{XR_TYPE_SCENE_OBSERVER_CREATE_INFO_FB};
        if (pfnCreateSceneObserver(session, &obsInfo, &observer) != XR_SUCCESS)
            return;
        XrSceneFB scene = XR_NULL_HANDLE;
        XrSceneCreateInfoFB sceneInfo{XR_TYPE_SCENE_CREATE_INFO_FB};
        if (pfnCreateScene(session, &sceneInfo, &scene) != XR_SUCCESS)
            return;

        // Compute scene synchronously
        XrSceneComputeInfoFB compute{XR_TYPE_SCENE_COMPUTE_INFO_FB};
        compute.requestedFeatures = XR_SCENE_COMPUTE_FEATURE_PLANE_MESH_FB | XR_SCENE_COMPUTE_FEATURE_VISUAL_MESH_FB;
        compute.consistency = XR_SCENE_COMPUTE_CONSISTENCY_SNAPSHOT_COMPLETE_FB;
        XrResult rc = pfnComputeNewScene(observer, &compute);
        ALOG("Scene compute -> %d", rc);
        if (rc != XR_SUCCESS)
            return;

        // Enumerate components to find meshes
        uint32_t count = 0;
        XrSceneComponentsGetInfoFB getInfo{XR_TYPE_SCENE_COMPONENTS_GET_INFO_FB};
        getInfo.componentType = XR_SCENE_COMPONENT_TYPE_VISUAL_MESH_FB;
        pfnGetSceneComponents(scene, &getInfo, 0, &count, nullptr);
        std::vector<XrSceneComponentFB> comps(count);
        if (count > 0)
        {
            pfnGetSceneComponents(scene, &getInfo, count, &count, comps.data());
        }

        // For each visual mesh, fetch buffers and create draw buffers
        for (auto &c : comps)
        {
            XrSceneMeshBuffersGetInfoFB mbInfo{XR_TYPE_SCENE_MESH_BUFFERS_GET_INFO_FB};
            mbInfo.meshId = c.id;
            XrSceneMeshBuffersFB mb{XR_TYPE_SCENE_MESH_BUFFERS_FB};
            if (pfnGetSceneMeshBuffers(scene, &mbInfo, &mb) != XR_SUCCESS)
                continue;
            // Create GPU buffers
            SceneMeshBuffers smb{};
            smb.vbo.reset(new OVRFW::GlBuffer());
            smb.ibo.reset(new OVRFW::GlBuffer());
            // Assume positions only; upload raw
            smb.vbo->Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_GENERIC, mb.vertices.count * sizeof(float), mb.vertices.data);
            smb.ibo->Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_INDEX, mb.indices.count * sizeof(uint32_t), mb.indices.data);
            smb.indexCount = (int)mb.indices.count;

            // Minimal shader: reuse model program
            const char *vSrc = Shaders::Vertex;
            const char *fSrc = Shaders::Fragment;
            OVRFW::ovrProgramParm UniformParms[] = {
                {"Texture0", OVRFW::ovrProgramParmType::TEXTURE_SAMPLED},
                {"SpecularLightDirection", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
                {"SpecularLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
                {"AmbientLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
                {"Opacity", OVRFW::ovrProgramParmType::FLOAT},
                {"AlphaBlend", OVRFW::ovrProgramParmType::FLOAT},
                {"JointMatrices", OVRFW::ovrProgramParmType::BUFFER_UNIFORM},
            };
            smb.prog = OVRFW::GlProgram::Build("", vSrc, "", fSrc, UniformParms, sizeof(UniformParms) / sizeof(OVRFW::ovrProgramParm));

            // Build a surface definition using raw buffers
            smb.surfaceDef.graphicsProgram = &smb.prog;
            auto &gc = smb.surfaceDef.graphicsCommand;
            gc.vertexBuffer = smb.vbo.get();
            gc.indexBuffer = smb.ibo.get();
            gc.indexCount = smb.indexCount;
            gc.primitiveType = OVRFW::ovrPrimitiveType::OVR_PRIMITIVE_TRIANGLES;
            // Bind uniforms for lighting and alpha (reuse same defaults)
            gc.UniformData[1].Data = &specularDir_;
            gc.UniformData[2].Data = &specularColor_;
            gc.UniformData[3].Data = &ambientColor_;
            gc.UniformData[4].Data = &opacity_;
            gc.UniformData[5].Data = &alphaBlend_;
            // Enable premultiplied alpha blending for passthrough-friendly composition
            gc.GpuState.depthEnable = gc.GpuState.depthMaskEnable = true;
            gc.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
            gc.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
            gc.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
            gc.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
            sceneMeshes_.push_back(std::move(smb));
        }

        // Cleanup local scene objects
        if (pfnDestroyScene)
            pfnDestroyScene(scene);
        if (pfnDestroySceneObserver)
            pfnDestroySceneObserver(observer);
    }

    // XR_EXT_hand_tracking typedefs
    typedef XrResult(XRAPI_PTR *PFN_xrCreateHandTrackerEXT)(XrSession session, const XrHandTrackerCreateInfoEXT *createInfo, XrHandTrackerEXT *handTracker);
    typedef XrResult(XRAPI_PTR *PFN_xrDestroyHandTrackerEXT)(XrHandTrackerEXT handTracker);
    typedef XrResult(XRAPI_PTR *PFN_xrLocateHandJointsEXT)(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT *locateInfo, XrHandJointLocationsEXT *locations);

    void Context::EnableHandViews(XrInstance instance, XrSession session, bool enable)
    {
        handViewsEnabled_ = enable;
        auto loadProc = [&](const char *name, void **fn)
        {
            return xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)fn);
        };
        if (!enable)
        {
            // Cleanup
            PFN_xrDestroyHandTrackerEXT pfnDestroyHT = nullptr;
            loadProc("xrDestroyHandTrackerEXT", (void **)&pfnDestroyHT);
            if (pfnDestroyHT)
            {
                if (handTrackerL_ != XR_NULL_HANDLE)
                    pfnDestroyHT(handTrackerL_);
                if (handTrackerR_ != XR_NULL_HANDLE)
                    pfnDestroyHT(handTrackerR_);
            }
            handTrackerL_ = handTrackerR_ = XR_NULL_HANDLE;
            handVbo_.reset();
            handIbo_.reset();
            handIndexCount_ = 0;
            return;
        }

        // Create trackers
        PFN_xrCreateHandTrackerEXT pfnCreateHT = nullptr;
        PFN_xrDestroyHandTrackerEXT pfnDestroyHT = nullptr;
        PFN_xrLocateHandJointsEXT pfnLocateJoints = nullptr;
        if (loadProc("xrCreateHandTrackerEXT", (void **)&pfnCreateHT) != XR_SUCCESS ||
            loadProc("xrDestroyHandTrackerEXT", (void **)&pfnDestroyHT) != XR_SUCCESS ||
            loadProc("xrLocateHandJointsEXT", (void **)&pfnLocateJoints) != XR_SUCCESS)
        {
            ALOGW("Hand tracking procs missing; hand views disabled");
            handViewsEnabled_ = false;
            return;
        }

        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        ci.hand = XR_HAND_LEFT_EXT;
        if (pfnCreateHT(session, &ci, &handTrackerL_) != XR_SUCCESS)
        {
            ALOGW("Create left hand tracker failed");
        }
        ci.hand = XR_HAND_RIGHT_EXT;
        if (pfnCreateHT(session, &ci, &handTrackerR_) != XR_SUCCESS)
        {
            ALOGW("Create right hand tracker failed");
        }

        // Build a simple line surface to render joints (updated per-frame)
        handVbo_.reset(new OVRFW::GlBuffer());
        handIbo_.reset(new OVRFW::GlBuffer());
        const int maxVerts = 2 * 26; // simple lines between wrist and fingertips
        const int maxIndices = maxVerts;
        handVbo_->Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_GENERIC, maxVerts * sizeof(OVR::Vector3f), nullptr);
        handIbo_->Create(OVRFW::GlBufferType_t::GLBUFFER_TYPE_INDEX, maxIndices * sizeof(uint16_t), nullptr);
        handIndexCount_ = 0;

        // Shader reuse
        const char *vSrc = Shaders::Vertex;
        const char *fSrc = Shaders::Fragment;
        OVRFW::ovrProgramParm UniformParms[] = {
            {"SpecularLightDirection", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"SpecularLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"AmbientLightColor", OVRFW::ovrProgramParmType::FLOAT_VECTOR3},
            {"Opacity", OVRFW::ovrProgramParmType::FLOAT},
            {"AlphaBlend", OVRFW::ovrProgramParmType::FLOAT},
            {"JointMatrices", OVRFW::ovrProgramParmType::BUFFER_UNIFORM},
        };
        handProg_ = OVRFW::GlProgram::Build("", vSrc, "", fSrc, UniformParms, sizeof(UniformParms) / sizeof(OVRFW::ovrProgramParm));
        handSurfaceDef_.graphicsProgram = &handProg_;
        auto &gc = handSurfaceDef_.graphicsCommand;
        gc.vertexBuffer = handVbo_.get();
        gc.indexBuffer = handIbo_.get();
        gc.indexCount = 0;
        gc.primitiveType = OVRFW::ovrPrimitiveType::OVR_PRIMITIVE_LINES;
        gc.UniformData[1].Data = &specularDir_;
        gc.UniformData[2].Data = &specularColor_;
        gc.UniformData[3].Data = &ambientColor_;
        gc.UniformData[4].Data = &opacity_;
        gc.UniformData[5].Data = &alphaBlend_;
        gc.GpuState.depthEnable = gc.GpuState.depthMaskEnable = true;
        gc.GpuState.blendEnable = OVRFW::ovrGpuState::BLEND_ENABLE;
        gc.GpuState.blendMode = OVRFW::ovrGpuState::kGL_FUNC_ADD;
        gc.GpuState.blendSrc = OVRFW::ovrGpuState::kGL_ONE;
        gc.GpuState.blendDst = OVRFW::ovrGpuState::kGL_ONE_MINUS_SRC_ALPHA;
    }

    static void UpdateHandBuffers(OVRFW::GlBuffer *vbo, OVRFW::GlBuffer *ibo, int &indexCount,
                                  const std::vector<OVR::Vector3f> &points)
    {
        if (!vbo || !ibo)
            return;
        indexCount = (int)points.size();
        if (auto *ptr = (OVR::Vector3f *)vbo->MapBuffer())
        {
            for (size_t i = 0; i < points.size(); ++i)
                ptr[i] = points[i];
            vbo->UnmapBuffer();
        }
        if (auto *iptr = (uint16_t *)ibo->MapBuffer())
        {
            for (uint16_t i = 0; i < (uint16_t)points.size(); ++i)
                iptr[i] = i;
            ibo->UnmapBuffer();
        }
    }

    void Context::UpdateHands(XrInstance instance, XrSession session, XrSpace baseSpace, XrTime time)
    {
        if (!handViewsEnabled_ || handTrackerL_ == XR_NULL_HANDLE || handTrackerR_ == XR_NULL_HANDLE)
            return;
        PFN_xrLocateHandJointsEXT pfnLocateJoints = nullptr;
        xrGetInstanceProcAddr(instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction *)&pfnLocateJoints);
        if (!pfnLocateJoints)
            return;
        XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
        locateInfo.baseSpace = baseSpace;
        locateInfo.time = time;

        XrHandJointLocationsEXT locL{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        XrHandJointLocationsEXT locR{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
        XrHandJointLocationEXT jointsL[XR_HAND_JOINT_COUNT_EXT]{};
        XrHandJointLocationEXT jointsR[XR_HAND_JOINT_COUNT_EXT]{};
        locL.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locL.jointLocations = jointsL;
        locR.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locR.jointLocations = jointsR;
        if (pfnLocateJoints(handTrackerL_, &locateInfo, &locL) != XR_SUCCESS ||
            pfnLocateJoints(handTrackerR_, &locateInfo, &locR) != XR_SUCCESS)
        {
            return;
        }
        // Build simple line list: wrist to fingertip joints
        std::vector<OVR::Vector3f> points;
        auto addPoint = [&](const XrHandJointLocationEXT &jl)
        {
            if (jl.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
            {
                points.emplace_back((float)jl.pose.position.x, (float)jl.pose.position.y, (float)jl.pose.position.z);
            }
        };
        // Left wrist and tips
        addPoint(jointsL[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_INDEX_TIP_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_MIDDLE_TIP_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_RING_TIP_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_LITTLE_TIP_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsL[XR_HAND_JOINT_THUMB_TIP_EXT]);
        // Right similarly
        addPoint(jointsR[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_INDEX_TIP_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_MIDDLE_TIP_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_RING_TIP_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_LITTLE_TIP_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_WRIST_EXT]);
        addPoint(jointsR[XR_HAND_JOINT_THUMB_TIP_EXT]);

        UpdateHandBuffers(handVbo_.get(), handIbo_.get(), handIndexCount_, points);
    }

    // Emit room mesh surfaces when enabled
    static void EmitRoomMeshes(const std::vector<SceneMeshBuffers> &meshes, std::vector<OVRFW::ovrDrawSurface> &surfaces)
    {
        for (auto &m : meshes)
        {
            OVRFW::ovrDrawSurface out{};
            out.surface = &m.surfaceDef;
            out.modelMatrix = OVR::Matrix4f::Identity();
            surfaces.push_back(out);
        }
    }

} // namespace CTX
