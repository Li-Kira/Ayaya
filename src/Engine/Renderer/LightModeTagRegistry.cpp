#include "ayapch.h"
#include "LightModeTagRegistry.hpp"

namespace Ayaya {

LightModeTagRegistry& LightModeTagRegistry::Instance() {
    static LightModeTagRegistry inst;
    return inst;
}

LightModeTagRegistry::LightModeTagRegistry() {
    RegisterBuiltins();
}

void LightModeTagRegistry::RegisterBuiltins() {
    m_TagToMask["GBuffer"]       = 1u << 0;
    m_TagToMask["ShadowCaster"]  = 1u << 1;
    m_TagToMask["Forward"]       = 1u << 2;
    m_TagToMask["DepthPrePass"]  = 1u << 3;
    for (auto& [name, mask] : m_TagToMask)
        m_MaskToTag[mask] = name;
    m_NextBit = kReservedBits;
}

void LightModeTagRegistry::RegisterTagAt(const std::string& name, uint32_t bit) {
    uint32_t mask = 1u << bit;
    m_TagToMask[name] = mask;
    m_MaskToTag[mask] = name;
    if (bit >= m_NextBit)
        m_NextBit = bit + 1;
}

uint32_t LightModeTagRegistry::RegisterTag(const std::string& name) {
    auto it = m_TagToMask.find(name);
    if (it != m_TagToMask.end())
        return it->second;
    uint32_t mask = 1u << m_NextBit++;
    m_TagToMask[name] = mask;
    m_MaskToTag[mask] = name;
    return mask;
}

uint32_t LightModeTagRegistry::GetTagMask(const std::string& name) const {
    auto it = m_TagToMask.find(name);
    return (it != m_TagToMask.end()) ? it->second : 0;
}

std::string LightModeTagRegistry::GetTagName(uint32_t singleBitMask) const {
    auto it = m_MaskToTag.find(singleBitMask);
    return (it != m_MaskToTag.end()) ? it->second : "";
}

uint32_t LightModeTagRegistry::ParseMask(const std::string& str) {
    if (str.empty()) return 0;

    // First: try as a raw integer (e.g. "32" from old serialized data)
    bool allDigits = true;
    for (char c : str) {
        if (c < '0' || c > '9') { allDigits = false; break; }
    }
    if (allDigits) {
        uint32_t parsed = static_cast<uint32_t>(std::stoul(str));
        if (parsed != 0) {
            // Conflict detection: warn if this raw bit overlaps a named tag's bit.
            for (auto& [name, mask] : m_TagToMask) {
                if ((parsed & mask) != 0)
                    AYAYA_CORE_WARN("LightMode raw bit '{}' overlaps named tag '{}' (bit {})", str, name, mask);
            }
            return parsed;
        }
        return 0;
    }

    // Otherwise: comma-separated tag names, auto-register unknowns
    uint32_t mask = 0;
    std::string remaining = str;
    size_t pos;
    while ((pos = remaining.find(',')) != std::string::npos) {
        std::string tag = remaining.substr(0, pos);
        uint32_t m = GetTagMask(tag);
        if (m == 0) m = RegisterTag(tag);
        mask |= m;
        remaining = remaining.substr(pos + 1);
    }
    if (!remaining.empty()) {
        uint32_t m = GetTagMask(remaining);
        if (m == 0) m = RegisterTag(remaining);
        mask |= m;
    }
    return mask;
}

std::string LightModeTagRegistry::MaskToString(uint32_t mask) const {
    if (mask == 0) return "";

    std::string result;
    uint32_t engineMask = mask & ((1u << kReservedBits) - 1);
    uint32_t customMask = mask & ~((1u << kReservedBits) - 1);

    // Built-in tags (bits 0-3)
    for (uint32_t bit = 0; bit < kReservedBits; ++bit) {
        uint32_t bitMask = 1u << bit;
        if (engineMask & bitMask) {
            auto it = m_MaskToTag.find(bitMask);
            if (it != m_MaskToTag.end()) {
                if (!result.empty()) result += ",";
                result += it->second;
            }
        }
    }

    // Custom tags (bits 4+)
    bool hasCustomNames = false;
    for (uint32_t i = kReservedBits; i < 32; ++i) {
        uint32_t bitMask = 1u << i;
        if (customMask & bitMask) {
            auto it = m_MaskToTag.find(bitMask);
            if (it != m_MaskToTag.end()) {
                if (!result.empty()) result += ",";
                result += it->second;
                hasCustomNames = true;
            }
        }
    }

    // Fallback: if custom bits have no registered names, use raw integer
    if (customMask && !hasCustomNames) {
        if (!result.empty()) result += ",";
        result += std::to_string(customMask);
    }

    return result;
}

} // namespace Ayaya
