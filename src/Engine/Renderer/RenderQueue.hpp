#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <memory>

namespace Ayaya {

    class Mesh;
    class Material;

    // ==========================================
    // 1. 渲染桶 — 直接对应 RenderGraph 中的 Pass 阶段
    // ==========================================
    enum class RenderBucket : uint8_t {
        Opaque      = 0,  // 不透明 → GBuffer
        Masked      = 1,  // 镂空/AlphaTest → GBuffer (Opaque 之后)
        Skybox      = 2,  // 天空盒 → ForwardBlend
        Translucent = 3,  // WBOIT 半透明 → WBOIT Gather
        Overlay     = 4   // UI/Gizmo
    };

    // ==========================================
    // 2. 64位极速排序键
    // ==========================================
    union SortKey {
        uint64_t Value;
        struct {
            uint64_t EntityID     : 16;  // 兜底：唯一标识
            uint64_t Depth        : 32;  // IEEE754 float → uint32 定点深度
            uint64_t MaterialHash : 12;  // 材质 ID (0-4095)，相同材质排在一起
            uint64_t BucketID     : 4;   // 最高位：区分不透明/半透明
        } Bits;
    };

    // ==========================================
    // 3. 紧凑绘制包
    // ==========================================
    struct DrawPacket {
        uint64_t   SortKey = 0;
        glm::mat4  Transform{1.0f};
        uint64_t   EntityHandle = 0;       // raw entt::entity ID for selection checks
        bool       CastShadows = true;
        bool       ReceiveShadows = false;
        std::shared_ptr<Mesh>     MeshAsset;
        std::shared_ptr<Material> MaterialAsset;
    };

    // ==========================================
    // 4. 渲染队列
    // ==========================================
    struct RenderQueue {
        std::vector<DrawPacket> Packets;

        void Clear() { Packets.clear(); }

        void Sort() {
            std::sort(Packets.begin(), Packets.end(),
                [](const DrawPacket& a, const DrawPacket& b) {
                    return a.SortKey < b.SortKey;
                });
        }
    };

    // ==========================================
    // 5. 辅助函数：浮点数距离 → 可比较的整数
    // ==========================================
    inline uint32_t FloatToDepthInt(float f) {
        uint32_t ui;
        memcpy(&ui, &f, sizeof(uint32_t));
        // IEEE754: 负数符号位翻转，保证整数比较与浮点比较一致
        if (ui & 0x80000000) ui = ~ui;
        else                 ui |= 0x80000000;
        return ui;
    }

} // namespace Ayaya
