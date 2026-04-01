#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <any>
#include <string_view>
#include <chrono> // 【新增】：用于 CPU 计时
#include <glm/glm.hpp>
#include <glad/glad.h> // 【新增】：用于底层的 OpenGL GPU 时间查询

#include "Renderer/Texture.hpp"
#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"

namespace Ayaya {

    class Scene;

    struct DrawCallStep {
        std::string PassName;
        std::string TargetName;
        std::string ShaderName;
        uint32_t TriangleCount;
    };
    
    // 【新增】：单个 Pass 的性能体检报告
    struct PassProfileData {
        float CPUTime = 0.0f;
        float GPUTime = 0.0f;
        uint32_t DrawCalls = 0;
        uint32_t Triangles = 0;
    };

    // ==========================================
    // 渲染上下文 (数据黑板)
    // ==========================================
    struct RenderContext {
        std::shared_ptr<Scene> ActiveScene;
        glm::mat4 ViewMatrix;
        glm::mat4 ProjectionMatrix;
        glm::vec3 CameraPosition;

        std::unordered_map<std::string_view, std::shared_ptr<Texture2D>> Textures;
        std::unordered_map<std::string_view, std::shared_ptr<Framebuffer>> Framebuffers;
        std::unordered_map<std::string_view, std::any> Settings;

        // 【新增】：存储每个 Pass 的独立体检报告
        std::unordered_map<std::string, PassProfileData> PassProfiles;

        struct {
            uint32_t DrawCalls = 0;
            uint32_t ShaderBinds = 0;
            uint32_t VertexCount = 0;
            uint32_t TriangleCount = 0;
        } Stats;

        std::vector<DrawCallStep> FrameSteps;
        int DebugStepLimit = -1; 

        bool RecordAndCheckDrawCall(std::string_view passName, std::string_view targetName, std::string_view shaderName, uint32_t triangles) {
            DrawCallStep step;
            step.PassName = std::string(passName);
            step.TargetName = std::string(targetName);
            step.ShaderName = std::string(shaderName);
            step.TriangleCount = triangles;
            FrameSteps.push_back(step); 

            uint32_t currentIndex = Stats.DrawCalls;
            Stats.DrawCalls++;
            Stats.TriangleCount += triangles;
            Stats.VertexCount += triangles * 3;

            if (DebugStepLimit >= 0 && (int)currentIndex > DebugStepLimit) {
                return false; 
            }
            return true; 
        }

        template<typename T>
        void Set(std::string_view key, const T& value) { Settings[key] = value; }
        
        template<typename T>
        T Get(std::string_view key, T defaultValue = T()) {
            auto it = Settings.find(key);
            if (it != Settings.end()) return std::any_cast<T>(it->second);
            return defaultValue;
        }

        void SetTexture(std::string_view key, const std::shared_ptr<Texture2D>& tex) { Textures[key] = tex; }
        
        std::shared_ptr<Texture2D> GetTexture(std::string_view key) {
            auto it = Textures.find(key);
            if (it != Textures.end()) return it->second;
            return nullptr;
        }
    };

    class RenderPass {
    public:
        virtual ~RenderPass() = default;
        virtual void OnAttach() = 0; 
        virtual void OnResize(uint32_t width, uint32_t height) {}
        virtual void Execute(RenderContext& context, RenderCommandBuffer& cmd) = 0;

        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }
        const std::string& GetName() const { return m_PassName; }

    protected:
        bool m_Enabled = true;
        std::string m_PassName = "Unknown Pass";
    };

    // ==========================================
    // 管线调度器 (Render Pipeline)
    // ==========================================
    class RenderPipeline {
    public:
        ~RenderPipeline() {
            // 释放所有查询器
            for (auto& kv : m_PassQueries) {
                glDeleteQueries(1, &kv.second);
            }
        }

        void Init() {
            for (auto& pass : m_Passes) {
                pass->OnAttach();
            }
        }

        void OnResize(uint32_t width, uint32_t height) {
            for (auto& pass : m_Passes) pass->OnResize(width, height);
        }

        void AddPass(std::shared_ptr<RenderPass> pass) {
            m_Passes.push_back(pass);
        }

        void Execute(RenderContext& context, RenderCommandBuffer& cmd) {
            for (auto& pass : m_Passes) {
                if (!pass->IsEnabled()) continue;

                const std::string& passName = pass->GetName();
                
                // 1. 动态生成专属的 GPU 查询器
                if (m_PassQueries.find(passName) == m_PassQueries.end()) {
                    uint32_t query;
                    glGenQueries(1, &query);
                    m_PassQueries[passName] = query;
                }
                uint32_t queryID = m_PassQueries[passName];

                // 2. 读取上一帧的 GPU 耗时 (异步非阻塞)
                uint32_t available = 0;
                glGetQueryObjectuiv(queryID, GL_QUERY_RESULT_AVAILABLE, &available);
                if (available) {
                    uint64_t gpuTimeNs = 0;
                    glGetQueryObjectui64v(queryID, GL_QUERY_RESULT, &gpuTimeNs);
                    context.PassProfiles[passName].GPUTime = (float)gpuTimeNs / 1000000.0f;
                }

                // 3. 记录初始的全局状态
                uint32_t startDC = context.Stats.DrawCalls;
                uint32_t startTris = context.Stats.TriangleCount;

                // 4. 开启前后夹击的性能拦截！
                auto cpuStart = std::chrono::high_resolution_clock::now();
                glBeginQuery(GL_TIME_ELAPSED, queryID);

                // ==========================================
                pass->Execute(context, cmd);
                // ==========================================

                glEndQuery(GL_TIME_ELAPSED);
                auto cpuEnd = std::chrono::high_resolution_clock::now();

                // 5. 结算数据并登记到黑板上
                context.PassProfiles[passName].CPUTime = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
                context.PassProfiles[passName].DrawCalls = context.Stats.DrawCalls - startDC;
                context.PassProfiles[passName].Triangles = context.Stats.TriangleCount - startTris;
            }
        }

    private:
        std::vector<std::shared_ptr<RenderPass>> m_Passes;
        std::unordered_map<std::string, uint32_t> m_PassQueries; // 管理所有 GPU 查询器
    };
}