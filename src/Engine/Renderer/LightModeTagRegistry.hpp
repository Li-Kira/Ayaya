#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace Ayaya {

// ==========================================
// LightModeTagRegistry — name ↔ bit mapping
//
// Built-in tags use bits 0-3 (matching the existing LightModeFlags enum).
// Custom tags start at bit 4 (kUserCustomLightModeStartBit).
//
// Usage from SRP Lua:
//   Pipeline:AddPass("Hologram", "GenericDrawPass", ..., { LightMode = "Hologram" })
// Usage from C++:
//   LightModeTagRegistry::Instance().RegisterTag("Hologram");
//   uint32_t mask = LightModeTagRegistry::Instance().ParseMask("GBuffer,Hologram");
// ==========================================
class LightModeTagRegistry {
public:
    static LightModeTagRegistry& Instance();

    // Register a new tag name. Returns the assigned bit mask (1u << bitPos).
    // If the tag is already registered, returns its existing mask.
    uint32_t RegisterTag(const std::string& name);

    // Look up a tag's bit mask. Returns 0 if unknown.
    uint32_t GetTagMask(const std::string& name) const;

    // Look up a tag name from a single-bit mask. Returns empty if not found.
    std::string GetTagName(uint32_t singleBitMask) const;

    // Parse a comma-separated string like "GBuffer,ShadowCaster" or a raw
    // integer string like "32" into a combined bitmask.
    uint32_t ParseMask(const std::string& str);  // non-const: auto-registers unknown tags

    // Convert a mask back to a comma-separated human-readable string.
    // Bits without registered names fall back to the raw integer string.
    std::string MaskToString(uint32_t mask) const;

    // Number of engine-reserved bits (GBuffer, ShadowCaster, Forward, DepthPrePass).
    static constexpr uint32_t kReservedBits = 4;

    // Pre-register a tag name mapped to a specific bit. Used by built-in tags
    // and by .ayashader files to reserve known bits.
    void RegisterTagAt(const std::string& name, uint32_t bit);

private:
    LightModeTagRegistry();
    void RegisterBuiltins();

    std::unordered_map<std::string, uint32_t> m_TagToMask;  // "GBuffer" → 1
    std::unordered_map<uint32_t, std::string> m_MaskToTag;  // 1 → "GBuffer"
    uint32_t m_NextBit = kReservedBits;  // first free bit for custom tags
};

} // namespace Ayaya
