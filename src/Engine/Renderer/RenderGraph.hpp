#pragma once

#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/RenderPipeline.hpp" // RenderContext

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Ayaya {

    // ==========================================
    // 虚拟纹理资源节点
    // ==========================================
    struct RGTexture {
        std::string Name;
        FramebufferSpecification Spec;

        std::shared_ptr<Framebuffer> PhysicalFBO[3] = {nullptr, nullptr, nullptr};
        bool IsWritten = false;
        bool IsRead   = false;
        bool IsNew[3] = {false, false, false};

        ImageLayout CurrentLayout[3] = {ImageLayout::Undefined, ImageLayout::Undefined, ImageLayout::Undefined};
    };

    // ==========================================
    // 前向声明
    // ==========================================
    class RenderGraph;
    class RGPass;

    using RGExecuteFn = std::function<void(RenderContext&, RenderCommandBuffer&)>;

    // ==========================================
    // RGBuilder - Setup 阶段暴露给 Pass 的依赖声明接口
    // ==========================================
    class RGBuilder {
    public:
        RGBuilder(RenderGraph& graph, RGPass& pass);

        void ReadTexture(const std::string& name);
        void WriteTexture(const std::string& name, const FramebufferSpecification& spec);

    private:
        RenderGraph& m_Graph;
        RGPass& m_Pass;
    };

    // ==========================================
    // RGPass - 图中的一个渲染节点
    // ==========================================
    class RGPass {
    public:
        std::string Name;
        RGExecuteFn ExecuteCallback;

        std::vector<std::string> TextureReads;
        std::vector<std::string> TextureWrites;

        bool HasSideEffect = false;
        bool IsCulled      = false;

        RGPass(const std::string& name, RGExecuteFn execute)
            : Name(name), ExecuteCallback(std::move(execute)) {}
    };

    // ==========================================
    // RenderGraph - 有向无环图中央调度器
    // ==========================================
    class RenderGraph {
    public:
        RenderGraph() = default;

        void Clear();

        // 核心 API：声明式添加一个 Pass（模板必须在头文件定义）
        template<typename SetupFunc, typename ExecuteFunc>
        void AddPass(const std::string& name, SetupFunc setup, ExecuteFunc execute) {
            auto pass = std::make_shared<RGPass>(name, execute);
            RGBuilder builder(*this, *pass);
            setup(builder);
            m_Passes.push_back(pass);
            m_Compiled = false;
        }

        RGTexture& RegisterTexture(const std::string& name, const FramebufferSpecification& spec);
        RGTexture* GetTexture(const std::string& name);

        void Compile();
        void Execute(RenderContext& context, RenderCommandBuffer& cmd);

        std::shared_ptr<Framebuffer> GetPhysicalFBO(const std::string& name, uint32_t frameIndex = 0);
        bool IsCompiled() const { return m_Compiled; }

    private:
        std::vector<std::shared_ptr<RGPass>> m_Passes;
        std::unordered_map<std::string, RGTexture> m_Textures;
        bool m_Compiled = false;
    };

} // namespace Ayaya
