// CTX.h - high-level context for scenes, nodes, meshes, armatures and rendering
// Lightweight, header-only declarations for the CTX subsystem.
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "OVR_Math.h"
// Use OpenGL ES types for GPU handles
#include <GLES3/gl3.h>
#include "XrApp.h"
#include "OVR_FileSys.h"

namespace CTX
{

    struct Mesh
    {
        // Placeholder mesh data. Real renderer will consume these.
        std::vector<float> vertices; // interleaved positions/normals/uv if used
        std::vector<uint32_t> indices;
        // GPU handles (0 = not uploaded)
        GLuint vbo = 0;
        GLuint ebo = 0;
        GLuint vao = 0;
    };

    struct Transform
    {
        OVR::Vector3f translation{0.0f};
        OVR::Quatf rotation{0.0f, 0.0f, 0.0f, 1.0f};
        OVR::Vector3f scale{1.0f};
    };

    struct Node
    {
        using Ptr = std::shared_ptr<Node>;
        std::string name;
        Transform transform;
        std::vector<Ptr> children;
        std::shared_ptr<Mesh> mesh; // optional mesh attached
        // simple parenting
        Node(const std::string &n = "") : name(n) {}
        Ptr AddChild(const Ptr &child)
        {
            children.push_back(child);
            return child;
        }
    };

    struct Bone
    {
        std::string name;
        int parentIndex = -1;
        OVR::Matrix4f inverseBind;
    };

    struct Armature
    {
        std::vector<Bone> bones;
    };

    struct Scene
    {
        std::vector<Node::Ptr> roots;
        std::vector<std::shared_ptr<Mesh>> meshes;
        std::shared_ptr<Armature> armature;
        Node::Ptr CreateNode(const std::string &name)
        {
            auto n = std::make_shared<Node>(name);
            roots.push_back(n);
            return n;
        }
        std::shared_ptr<Mesh> LoadMeshFromMemory(const void *data, size_t size, const std::string &name = "");
    };

    // High-level GL/renderer interface used by the app. Implementations should
    // integrate with the app's render loop and OpenXR swapchains.
    class Renderer
    {
    public:
        virtual ~Renderer() {}
        // Initialize the renderer; must be called after XR session is ready if
        // renderer needs XR session handles. Returns true on success.
        virtual bool Initialize() = 0;
        // Shutdown and free GPU resources.
        virtual void Shutdown() = 0;
        // Render a scene into the provided view/projection matrices and layer.
        // The application is responsible for acquiring/releasing XR swapchains.
        virtual void RenderScene(const Scene &scene, const OVR::Matrix4f &view, const OVR::Matrix4f &proj) = 0;
    };

    // Factory to create a simple GL renderer compatible with OpenXR.
    std::unique_ptr<Renderer> CreateOpenGLRenderer();

    // Utility: simple OBJ-like loader (minimal support) that returns a Node with mesh attached.
    Node::Ptr LoadModelAsNode(Scene &scene, const std::string &objData, const std::string &name = "model");
    Node::Ptr LoadMeshFromFile(Scene &scene, OVRFW::ovrFileSys &fileSys, const std::string &filename);

} // namespace CTX
