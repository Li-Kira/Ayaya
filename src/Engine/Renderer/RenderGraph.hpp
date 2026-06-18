#pragma once

#include "Renderer/Framebuffer.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/RenderPipeline.hpp"

#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Ayaya {

    // ==========================================
    // RGTexture — 一个命名的渲染目标
    //
    // 生命周期由 RenderGraph 管理。物理 FBO 在 Compile() 中创建（3 帧缓冲，
    // 与引擎的 triple-buffering 对齐），在 Execute() 中根据当前帧索引
    // 注入对应的 FBO 到 RenderContext::Framebuffers。
    // ==========================================
    static constexpr uint32_t kRenderGraphFramesInFlight = 3;

    struct RGTexture {
        std::string Name;
        FramebufferSpecification Spec;

        std::array<std::shared_ptr<Framebuffer>, kRenderGraphFramesInFlight> PhysicalFBOs = {};
        bool IsWritten = false;
        bool IsRead   = false;

        // Per-frame layout tracking — color and depth tracked independently since
        // mixed FBOs (color+depth) have attachments in different layouts.
        std::array<ImageLayout, kRenderGraphFramesInFlight> CurrentLayout = {
            ImageLayout::Undefined, ImageLayout::Undefined, ImageLayout::Undefined
        };
        std::array<ImageLayout, kRenderGraphFramesInFlight> DepthLayout = {
            ImageLayout::Undefined, ImageLayout::Undefined, ImageLayout::Undefined
        };

        bool HasDepthAttachment() const {
            for (auto& att : Spec.Attachments.Attachments) {
                if (att.TextureFormat == FramebufferTextureFormat::Depth ||
                    att.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8)
                    return true;
            }
            return false;
        }
    };

    class RenderGraph;
    class RGPass;

    using RGExecuteFn = std::function<void(RenderContext&, RenderCommandBuffer&)>;

    // ==========================================
    // RGBuilder — Pass 声明资源依赖的 DSL
    //
    // 在 graph.AddPass(name, setup, execute) 的 setup lambda 中使用：
    //   builder.ReadTexture("InputName");              // 只读
    //   builder.WriteTexture("OutputName", fboSpec);   // 独占写入
    //   builder.ReadWriteTexture("TargetName", spec);  // 读+写（累加/半透明）
    //
    // 规则：
    //   - 同一个纹理可被多个 Pass 读取
    //   - WriteTexture 首次注册时创建纹理，重复调用视为冲突（报错）
    //   - ReadWriteTexture 允许多个 Pass 读写同一纹理，自动建立隐式时序依赖
    //     （后写入者依赖先写入者，确保累加顺序正确）
    // ==========================================
    class RGBuilder {
    public:
        RGBuilder(RenderGraph& graph, RGPass& pass);

        void ReadTexture(const std::string& name);
        void WriteTexture(const std::string& name, const FramebufferSpecification& spec);
        void ReadWriteTexture(const std::string& name, const FramebufferSpecification& spec);

        // Mark this pass as culled — it will be excluded from the DAG at Compile time.
        // Consuming passes are responsible for their own fallback textures.
        void SetCulled(bool culled) { m_Culled = culled; }
        bool IsCulled() const { return m_Culled; }

    private:
        RenderGraph& m_Graph;
        RGPass&      m_Pass;
        bool m_Culled = false;
    };

    // ==========================================
    // RGPass — 图中的一个渲染节点
    //
    // Name          — 唯一标识符
    // TextureReads  — 此 Pass 消费的纹理名列表
    // TextureWrites — 此 Pass 产出的纹理名列表
    // HasSideEffect — 即使 Write 列表为空也强制执行（用于 UI Pass 等）
    // IsCulled      — 帧间剔除标记（当前未实现，预留）
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
    // RenderGraph — 声明式 DAG 帧图
    //
    // 用法：
    //   1. graph.Clear()                          // 清除上一帧的 Pass 列表和纹理
    //   2. graph.AddPass("Name", setup, execute) // 添加 Pass（声明 I/O + 回调）
    //   3. graph.Compile()                       // 拓扑排序 + 创建物理 FBO
    //   4. graph.Execute(ctx, cmd)               // 按序执行，插入 TBDR 屏障
    //
    // 设计约束：
    //   - 一个纹理只能有一个生产者（WriteTexture），但可以有多个消费者（ReadTexture）
    //   - 循环依赖会被检测并报错
    //   - 物理 FBO 三帧缓冲（triple-buffered），与引擎的 3-frames-in-flight 对齐，
    //     每帧独立 FBO 避免 GPU 读写竞争
    // ==========================================
    class RenderGraph {
    public:
        RenderGraph() = default;

        // 清除所有 Pass 和纹理，标记为未编译
        void Clear();

        // 添加一个 Pass。setup lambda 用 RGBuilder 声明资源依赖
        template<typename SetupFunc, typename ExecuteFunc>
        void AddPass(const std::string& name, SetupFunc setup, ExecuteFunc execute) {
            auto pass = std::make_shared<RGPass>(name, execute);
            RGBuilder builder(*this, *pass);
            setup(builder);
            pass->IsCulled = builder.IsCulled();
            m_Passes.push_back(pass);
            m_Compiled = false;
        }

        // 注册或获取一个纹理（同名纹理复用首次注册的 Spec）
        RGTexture& RegisterTexture(const std::string& name, const FramebufferSpecification& spec);
        RGTexture* GetTexture(const std::string& name);

        // 拓扑排序 Pass 队列 + 为每个纹理创建物理 FBO
        void Compile();

        // 按拓扑序执行所有 Pass，注入 FBO 到 context，插入 TBDR 屏障
        void Execute(RenderContext& context, RenderCommandBuffer& cmd);

        // 获取指定纹理和帧索引的物理 FBO
        std::shared_ptr<Framebuffer> GetPhysicalFBO(const std::string& name, uint32_t frameIndex = 0);

        // Access pass list for per-frame culling updates
        std::vector<std::shared_ptr<RGPass>>& GetPasses() { return m_Passes; }

        bool IsCompiled() const { return m_Compiled; }

        // 获取纹理的可读布局
        static ImageLayout GetReadLayout(const FramebufferSpecification& spec);

    private:
        // 确保纹理处于可被后续 Pass 读取的布局
        void EnsureReadable(RGTexture& tex, uint32_t frameIndex, RenderCommandBuffer& cmd);

        // 确保纹理处于 attachment 布局（vkCmdBeginRendering 的前置条件）
        void EnsureWritable(RGTexture& tex, uint32_t frameIndex, RenderCommandBuffer& cmd);

        // Attachment→ReadOnly 布局转换 + TBDR tile-resolve:
        // 将图像从 attachment 布局转为 shader 可采样布局，并插入 execution+memory
        // barrier 确保 TBDR on-chip tile 数据刷新到 system memory
        void InsertTileResolveBarrier(RGTexture& tex, uint32_t frameIndex);

        std::vector<std::shared_ptr<RGPass>> m_Passes;
        std::unordered_map<std::string, RGTexture> m_Textures;
        bool m_Compiled = false;
    };

} // namespace Ayaya
