#pragma once
#include <cstdint>
#include <functional>

namespace Ayaya {
    class UUID {
    public:
        UUID();
        UUID(uint64_t uuid);
        UUID(const UUID&) = default;

        operator uint64_t() const { return m_UUID; }
    private:
        uint64_t m_UUID;
    };
}

namespace std {
    template<>
    struct hash<Ayaya::UUID> {
        std::size_t operator()(const Ayaya::UUID& uuid) const {
            return (uint64_t)uuid;
        }
    };
}