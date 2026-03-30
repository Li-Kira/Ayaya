#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <any>
#include <glm/glm.hpp>

#include "Renderer/Texture.hpp"
#include "Renderer/Framebuffer.hpp"

namespace Ayaya {

    // 【核心修复】：使用前向声明，彻底斩断头文件循环依赖！
    class Scene;
    
    // ==========================================
    // 渲染上下文 (数据黑板)：贯穿整帧渲染的数据总线
    // ==========================================
    struct RenderContext {
        std::shared_ptr<Scene> ActiveScene;
        glm::mat4 ViewMatrix;
        glm::mat4 ProjectionMatrix;
        glm::vec3 CameraPosition;

        // 资源黑板：Pass 之间通过字符串名字存取贴图和缓冲
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> Textures;
        std::unordered_map<std::string, std::shared_ptr<Framebuffer>> Framebuffers;
        
        // 参数黑板：存储 UI 面板传过来的任意类型变量 (如开关、浮点数)
        std::unordered_map<std::string, std::any> Settings;

        template<typename T>
        void Set(const std::string& key, const T& value) { Settings[key] = value; }
        
        template<typename T>
        T Get(const std::string& key, T defaultValue = T()) {
            if (Settings.find(key) != Settings.end()) return std::any_cast<T>(Settings[key]);
            return defaultValue;
        }
    };

    // ==========================================
    // 可编程渲染通道基类 (Scriptable Render Pass)
    // ==========================================
    class RenderPass {
    public:
        virtual ~RenderPass() = default;

        virtual void OnAttach() = 0; 
        virtual void OnResize(uint32_t width, uint32_t height) {}
        virtual void Execute(RenderContext& context) = 0;

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
        void Init() {
            for (auto& pass : m_Passes) {
                pass->OnAttach();
            }
        }

        void OnResize(uint32_t width, uint32_t height) {
            for (auto& pass : m_Passes) {
                pass->OnResize(width, height);
            }
        }

        void AddPass(std::shared_ptr<RenderPass> pass) {
            m_Passes.push_back(pass);
        }

        void Execute(RenderContext& context) {
            for (auto& pass : m_Passes) {
                if (pass->IsEnabled()) {
                    pass->Execute(context);
                }
            }
        }

    private:
        std::vector<std::shared_ptr<RenderPass>> m_Passes;
    };
}