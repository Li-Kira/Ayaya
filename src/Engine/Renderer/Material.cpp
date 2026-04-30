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
        SetPropertyInternal(name, MaterialPropertyType::Float, [&](MaterialProperty& p) { p.FloatValue = value; });
    }

    void Material::SetInt(const std::string& name, int value) {
        SetPropertyInternal(name, MaterialPropertyType::Int, [&](MaterialProperty& p) { p.IntValue = value; });
    }

    void Material::SetBool(const std::string& name, bool value) {
        SetPropertyInternal(name, MaterialPropertyType::Bool, [&](MaterialProperty& p) { p.BoolValue = value; });
    }

    void Material::SetVec2(const std::string& name, const glm::vec2& value) {
        SetPropertyInternal(name, MaterialPropertyType::Vec2, [&](MaterialProperty& p) { p.Vec2Value = value; });
    }

    void Material::SetVec3(const std::string& name, const glm::vec3& value) {
        SetPropertyInternal(name, MaterialPropertyType::Vec3, [&](MaterialProperty& p) { p.Vec3Value = value; });
    }

    void Material::SetVec4(const std::string& name, const glm::vec4& value) {
        SetPropertyInternal(name, MaterialPropertyType::Vec4, [&](MaterialProperty& p) { p.Vec4Value = value; });
    }

    void Material::SetMat3(const std::string& name, const glm::mat3& value) {
        SetPropertyInternal(name, MaterialPropertyType::Mat3, [&](MaterialProperty& p) { p.Mat3Value = value; });
    }

    void Material::SetMat4(const std::string& name, const glm::mat4& value) {
        SetPropertyInternal(name, MaterialPropertyType::Mat4, [&](MaterialProperty& p) { p.Mat4Value = value; });
    }

    void Material::SetTexture(const std::string& name, UUID textureHandle) {
        SetPropertyInternal(name, MaterialPropertyType::Texture2D, [&](MaterialProperty& p) { 
            p.TextureHandle = textureHandle; 
            p.RuntimeTexture = nullptr; // 清除可能残留的运行时对象
        });
    }

    void Material::SetRuntimeTexture(const std::string& name, const std::shared_ptr<Texture2D>& texture) {
        SetPropertyInternal(name, MaterialPropertyType::Texture2D, [&](MaterialProperty& p) { 
            p.RuntimeTexture = texture; 
            p.TextureHandle = 0; // 清除可能残留的资产 ID
        });
    }
    
    void Material::SetRuntimeTextureCube(const std::string& name, const std::shared_ptr<TextureCube>& texture) {
    SetPropertyInternal(name, MaterialPropertyType::TextureCube, [&texture](MaterialProperty& prop) {
        prop.RuntimeTextureCube = texture;
    });
}
}