#include "ayapch.h"
#include "Material.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"

namespace Ayaya {

    void Material::Bind(RenderCommandBuffer& cmd, const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Texture2D>& fallbackWhiteTexture) {
        if (!pipeline) return;
        auto shader = pipeline->GetSpecification().Shader;
        if (!shader) return;

        int textureSlot = 0; 
        
        // 遍历打包好的属性，一次性推向显卡！
        for (auto& prop : Properties) {
            switch (prop.Type) {
                case MaterialPropertyType::Float: shader->SetFloat(prop.UniformName, prop.FloatValue); break;
                case MaterialPropertyType::Int:   shader->SetInt(prop.UniformName, prop.IntValue); break;
                case MaterialPropertyType::Bool:  shader->SetBool(prop.UniformName, prop.BoolValue); break;
                case MaterialPropertyType::Vec2:  shader->SetFloat2(prop.UniformName, prop.Vec2Value); break;
                case MaterialPropertyType::Vec3:  shader->SetFloat3(prop.UniformName, prop.Vec3Value); break;
                case MaterialPropertyType::Vec4:  shader->SetFloat4(prop.UniformName, prop.Vec4Value); break;
                case MaterialPropertyType::Mat3:  shader->SetMat3(prop.UniformName, prop.Mat3Value); break;
                case MaterialPropertyType::Mat4:  shader->SetMat4(prop.UniformName, prop.Mat4Value); break;
                
                case MaterialPropertyType::Texture2D: {
                    std::shared_ptr<Texture2D> finalTex = fallbackWhiteTexture;
                    
                    // 1. 优先使用运行时临时分配的贴图 (例如 G-Buffer 注入)
                    if (prop.RuntimeTexture) {
                        finalTex = prop.RuntimeTexture;
                    } 
                    // 2. 否则通过资产系统读取硬盘里的贴图
                    else if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                        auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                        if (tex) finalTex = tex;
                    }
                    
                    // 利用 CommandBuffer 的运行时描述符接口绑定 (直接传递智能指针)
                    cmd.BindTexture2D(pipeline, prop.UniformName, textureSlot, finalTex);
                    textureSlot++;
                    break;
                }
            }
        }
    }

    // ==========================================
    // 便捷的数据写入接口 (充当 Descriptor Set 的参数填充)
    // ==========================================
    
    void Material::SetFloat(const std::string& name, float value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Float, [&](MaterialProperty& p) { p.FloatValue = value; });
    }
    void Material::SetInt(const std::string& name, int value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Int, [&](MaterialProperty& p) { p.IntValue = value; });
    }
    void Material::SetBool(const std::string& name, bool value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Bool, [&](MaterialProperty& p) { p.BoolValue = value; });
    }
    void Material::SetVec2(const std::string& name, const glm::vec2& value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Vec2, [&](MaterialProperty& p) { p.Vec2Value = value; });
    }
    void Material::SetVec3(const std::string& name, const glm::vec3& value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Vec3, [&](MaterialProperty& p) { p.Vec3Value = value; });
    }
    void Material::SetVec4(const std::string& name, const glm::vec4& value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Vec4, [&](MaterialProperty& p) { p.Vec4Value = value; });
    }
    void Material::SetMat3(const std::string& name, const glm::mat3& value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Mat3, [&](MaterialProperty& p) { p.Mat3Value = value; });
    }
    void Material::SetMat4(const std::string& name, const glm::mat4& value) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Mat4, [&](MaterialProperty& p) { p.Mat4Value = value; });
    }
    void Material::SetTexture(const std::string& name, UUID textureHandle) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Texture2D, [&](MaterialProperty& p) {
            p.TextureHandle = textureHandle;
            p.RuntimeTexture = nullptr;
        });
    }
    void Material::SetRuntimeTexture(const std::string& name, const std::shared_ptr<Texture2D>& texture) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::Texture2D, [&](MaterialProperty& p) {
            p.RuntimeTexture = texture;
            p.TextureHandle = 0;
        });
    }
    
    void Material::SetRuntimeTextureCube(const std::string& name, const std::shared_ptr<TextureCube>& texture) {
        m_BakedPC.Dirty = true;
        SetPropertyInternal(name, MaterialPropertyType::TextureCube, [&texture](MaterialProperty& prop) {
            prop.RuntimeTextureCube = texture;
        });
    }

    void Material::BakeProperties() {
        auto savedPacking = m_BakedPC.Packing;  // preserve across reset
        m_BakedPC = BakedPC{};  // reset to defaults (indices point to default 1×1 textures)
        m_BakedPC.Packing = savedPacking;
        bool hasPending = false;
        for (auto& prop : Properties) {
            if (prop.UniformName == "u_Albedo" && prop.Type == MaterialPropertyType::Vec3) {
                m_BakedPC.Albedo = glm::vec4(prop.Vec3Value, 1.0f);
            } else if (prop.UniformName == "u_Metallic" && prop.Type == MaterialPropertyType::Float) {
                m_BakedPC.Metallic = prop.FloatValue;
            } else if (prop.UniformName == "u_Roughness" && prop.Type == MaterialPropertyType::Float) {
                m_BakedPC.Roughness = prop.FloatValue;
            } else if (prop.UniformName == "u_AO" && prop.Type == MaterialPropertyType::Float) {
                m_BakedPC.AO = prop.FloatValue;
            } else if (prop.UniformName == "u_Alpha" && prop.Type == MaterialPropertyType::Float) {
                m_BakedPC.Alpha = prop.FloatValue;
            } else if (prop.Type == MaterialPropertyType::Texture2D) {
                // Resolve texture and get its bindless index
                std::shared_ptr<Texture2D> tex = prop.RuntimeTexture;
                if (!tex && prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle))
                    tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                if (tex) {
                    uint32_t idx = tex->GetBindlessIndex();
                    if (idx == 0) { hasPending = true; continue; }  // not yet uploaded
                    if (prop.UniformName == "u_AlbedoMap") {
                        m_BakedPC.AlbedoMapIndex = idx;
                    } else if (prop.UniformName == "u_NormalMap") {
                        m_BakedPC.NormalMapIndex = idx;
                    } else if (prop.UniformName == "u_ORMMap") {
                        m_BakedPC.UseORMMap = 1;
                        m_BakedPC.ORMMapIndex = idx;
                    } else if (prop.UniformName == "u_MetallicMap") {
                        m_BakedPC.MetallicMapIndex = idx;
                    } else if (prop.UniformName == "u_RoughnessMap") {
                        m_BakedPC.RoughnessMapIndex = idx;
                    } else if (prop.UniformName == "u_AOMap") {
                        m_BakedPC.AOMapIndex = idx;
                    } else if (prop.UniformName == "u_AlphaMap") {
                        m_BakedPC.AlphaMapIndex = idx;
                    }
                } else if (prop.TextureHandle != 0) {
                    // Texture UUID is registered but not yet GPU-loaded (async load in flight).
                    // Mark pending so GetBakedPC re-bakes once the texture arrives.
                    hasPending = true;
                }
            }
        }
        m_BakedPC.Dirty = false;
        m_HasPendingTextures = hasPending;  // true until all texture bindless indices available
    }

    const Material::BakedPC& Material::GetBakedPC() {
        if (m_BakedPC.Dirty || m_HasPendingTextures) BakeProperties();
        return m_BakedPC;
    }

    uint32_t Material::GetLightModeMask() const {
        if (m_LightModeMask != 0) return m_LightModeMask;

        // Parse named ("GBuffer,Hologram") or raw integer ("32") string via registry
        if (!m_LightModeStr.empty()) {
            uint32_t parsed = LightModeTagRegistry::Instance().ParseMask(m_LightModeStr);
            if (parsed != 0) return parsed;
        }

        // Backward compat: auto-detect from BlendMode
        // All materials implicitly participate in ShadowCaster by default
        uint32_t mask = (uint32_t)LightModeFlags::ShadowCaster;
        switch (m_BlendMode) {
            case MaterialBlendMode::Opaque:
            case MaterialBlendMode::Masked:  mask |= (uint32_t)LightModeFlags::GBuffer; break;
            case MaterialBlendMode::Translucent: mask |= (uint32_t)LightModeFlags::Forward; break;
            default: mask |= (uint32_t)LightModeFlags::GBuffer; break;
        }
        return mask;
    }

    bool Material::IsBuiltIn() const {
        // Built-in materials share the canonical handle and live under engine://
        return AssetPath.empty() || AssetPath.find("assets/Editor/") != std::string::npos;
    }
}