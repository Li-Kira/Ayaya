#pragma once

#include <cstdint>
#include <memory>

namespace Ayaya {

    class UniformBuffer {
    public:
        virtual ~UniformBuffer() = default;

        // 向显存更新数据
        virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

        // 工厂方法：size 是分配的字节数，binding 是绑定的槽位号
        static std::shared_ptr<UniformBuffer> Create(uint32_t size, uint32_t binding);
    };

}