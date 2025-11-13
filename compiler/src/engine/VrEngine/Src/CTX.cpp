// CTX.cpp - implementation for CTX.h
#include "CTX.h"
#include <sstream>
#include <cstring>
#include "OVR_Math.h"
// Use OpenGL ES / EGL headers
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <fstream>

namespace CTX
{

    // load mesh data from a file on the disk into the provided scene
    Node::Ptr LoadMeshFromFile(Scene &scene, const std::string &filename)
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in)
        {
            ALOG("Failed to open mesh file: %s", filename.c_str());
            return nullptr;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string contents = ss.str();
        return LoadModelAsNode(scene, contents, filename);
    }

    std::shared_ptr<Mesh> Scene::LoadMeshFromMemory(const void *data, size_t size, const std::string &name)
    {
        // Minimal placeholder: no real parsing. Create an empty mesh entry so apps can attach data later.
        auto m = std::make_shared<Mesh>();
        meshes.push_back(m);
        return m;
    }

    // Very small OBJ-like parser: supports v and f only (positions, triangular faces).
    Node::Ptr LoadModelAsNode(Scene &scene, const std::string &objData, const std::string &name)
    {
        std::istringstream ss(objData);
        std::string line;
        std::vector<OVR::Vector3f> positions;
        auto node = std::make_shared<Node>(name);
        auto mesh = std::make_shared<Mesh>();
        while (std::getline(ss, line))
        {
            if (line.size() < 2)
                continue;
            if (line[0] == 'v' && line[1] == ' ')
            {
                std::istringstream ls(line.substr(2));
                OVR::Vector3f p;
                ls >> p.x >> p.y >> p.z;
                positions.push_back(p);
            }
            else if (line[0] == 'f' && line[1] == ' ')
            {
                std::istringstream ls(line.substr(2));
                int a, b, c;
                ls >> a >> b >> c; // 1-based indices
                // push positions as simple vertex list (pos only)
                OVR::Vector3f pa = positions[a - 1];
                OVR::Vector3f pb = positions[b - 1];
                OVR::Vector3f pc = positions[c - 1];
                // each vertex: x,y,z
                mesh->vertices.push_back(pa.x);
                mesh->vertices.push_back(pa.y);
                mesh->vertices.push_back(pa.z);
                mesh->vertices.push_back(pb.x);
                mesh->vertices.push_back(pb.y);
                mesh->vertices.push_back(pb.z);
                mesh->vertices.push_back(pc.x);
                mesh->vertices.push_back(pc.y);
                mesh->vertices.push_back(pc.z);
                uint32_t base = static_cast<uint32_t>(mesh->vertices.size() / 3) - 3;
                mesh->indices.push_back(base);
                mesh->indices.push_back(base + 1);
                mesh->indices.push_back(base + 2);
            }
        }
        node->mesh = mesh;
        scene.meshes.push_back(mesh);
        return node;
    }

    // Simple OpenGL renderer implementation
    class GLRenderer : public Renderer
    {
    public:
        GLRenderer() : inited(false) {}
        virtual ~GLRenderer() {}
        bool Initialize() override
        {
            // In a real app, create GL context and load functions here.
            inited = true;
            return true;
        }
        void Shutdown() override
        {
            inited = false;
        }
        void RenderScene(const Scene &scene, const OVR::Matrix4f &view, const OVR::Matrix4f &proj) override
        {
            if (!inited)
                return;
            // Use GLES3 path: upload simple meshes to GPU and draw with a basic shader.
            if (!programReady)
            {
                if (!InitProgram())
                    return;
            }

            // Set projection+view uniforms
            glUseProgram(program);
            GLint locView = glGetUniformLocation(program, "uView");
            GLint locProj = glGetUniformLocation(program, "uProj");
            if (locView >= 0)
                glUniformMatrix4fv(locView, 1, GL_FALSE, &view.M[0][0]);
            if (locProj >= 0)
                glUniformMatrix4fv(locProj, 1, GL_FALSE, &proj.M[0][0]);

            for (const auto &root : scene.roots)
            {
                RenderNodeRecursive(root.get(), OVR::Matrix4f::Identity());
            }
        }

    private:
        bool inited;
        GLuint program = 0;
        bool programReady = false;

        bool InitProgram()
        {
            const char *vs = "#version 300 es\n"
                             "layout(location=0) in vec3 aPos;\n"
                             "uniform mat4 uView;\n"
                             "uniform mat4 uProj;\n"
                             "uniform mat4 uModel;\n"
                             "void main() { gl_Position = uProj * uView * uModel * vec4(aPos,1.0); }\n";
            const char *fs = "#version 300 es\nprecision mediump float;out vec4 fragColor;void main(){fragColor=vec4(1,1,1,1);}\n";
            GLuint vsId = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vsId, 1, &vs, nullptr);
            glCompileShader(vsId);
            GLuint fsId = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fsId, 1, &fs, nullptr);
            glCompileShader(fsId);
            program = glCreateProgram();
            glAttachShader(program, vsId);
            glAttachShader(program, fsId);
            glBindAttribLocation(program, 0, "aPos");
            glLinkProgram(program);
            glDeleteShader(vsId);
            glDeleteShader(fsId);
            programReady = true;
            return programReady;
        }

        void UploadMeshIfNeeded(Mesh *mesh)
        {
            if (!mesh || mesh->vertices.empty())
                return;
            if (mesh->vao != 0)
                return; // already uploaded
            glGenVertexArrays(1, &mesh->vao);
            glGenBuffers(1, &mesh->vbo);
            glGenBuffers(1, &mesh->ebo);
            glBindVertexArray(mesh->vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
            glBufferData(GL_ARRAY_BUFFER, mesh->vertices.size() * sizeof(float), mesh->vertices.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->indices.size() * sizeof(uint32_t), mesh->indices.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);
        }
        void RenderNodeRecursive(const Node *node, const OVR::Matrix4f &parent)
        {
            OVR::Matrix4f t = parent;
            // Apply translation
            OVR::Matrix4f trans(node->transform.translation);
            // Apply rotation
            OVR::Matrix4f rot(OVR::Matrix4f(node->transform.rotation));
            // Apply scale
            OVR::Matrix4f scale = OVR::Matrix4f::Scaling(node->transform.scale);
            t *= rot;
            t *= scale;
            t.SetTranslation(node->transform.translation);
            // Ensure mesh is uploaded
            if (node->mesh)
            {
                UploadMeshIfNeeded(node->mesh.get());
                if (node->mesh->vao != 0)
                {
                    glUseProgram(program);
                    GLint locModel = glGetUniformLocation(program, "uModel");
                    if (locModel >= 0)
                        glUniformMatrix4fv(locModel, 1, GL_FALSE, &t.M[0][0]);
                    glBindVertexArray(node->mesh->vao);
                    glDrawElements(GL_TRIANGLES, (GLsizei)node->mesh->indices.size(), GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                }
            }
            for (const auto &c : node->children)
                RenderNodeRecursive(c.get(), t);
        }
    };

    std::unique_ptr<Renderer> CreateOpenGLRenderer()
    {
        return std::unique_ptr<Renderer>(new GLRenderer());
    }

    // No-op: free function already implemented above.

} // namespace CTX
