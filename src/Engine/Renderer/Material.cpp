#include "ayapch.h"
#include "Material.hpp"
#include "Renderer/RenderCommandBuffer.hpp"
#include "Renderer/Pipeline.hpp"
#include "Renderer/Shader.hpp"
#include "Asset/AssetManager.hpp"
#include "Renderer/Texture.hpp"

namespace Ayaya {

    void Material::Bind(RenderCommandBuffer& cmd, const std::shared_ptr<Pipeline>& pipeline, uint32_t fallbackWhiteTextureID) {
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
                
                // 【补全】：矩阵类型的绑定
                case MaterialPropertyType::Mat3:  shader->SetMat3(prop.UniformName, prop.Mat3Value); break;
                case MaterialPropertyType::Mat4:  shader->SetMat4(prop.UniformName, prop.Mat4Value); break;
                
                case MaterialPropertyType::Texture2D: {
                    uint32_t finalTexID = fallbackWhiteTextureID;
                    
                    // 1. 优先使用运行时临时分配的贴图 (例如 G-Buffer)
                    if (prop.RuntimeTextureID != 0) {
                        finalTexID = prop.RuntimeTextureID;
                    } 
                    // 2. 否则通过资产系统读取硬盘里的贴图
                    else if (prop.TextureHandle != 0 && AssetManager::IsAssetHandleValid(prop.TextureHandle)) {
                        auto tex = AssetManager::GetAsset<Texture2D>(prop.TextureHandle);
                        if (tex) finalTexID = tex->GetRendererID();
                    }
                    
                    // 利用 CommandBuffer 的运行时描述符模拟接口绑定
                    cmd.BindTexture2D(pipeline, prop.UniformName, textureSlot, finalTexID);
                    textureSlot++;
                    break;
                }
            }
        }
    }

    // ==========================================
    // 便捷的数据写入接口 (充当 Descriptor Set 的参数填充)
    // ==========================================
    
    template<typename AssignFunc>
    void Material::SetPropertyInternal(const std::string& name, MaterialPropertyType type, AssignFunc assignFunc) {
        // 1. 如果属性已经存在，直接更新它的值
        for (auto& prop : Properties) {
            if (prop.UniformName == name) {
                assignFunc(prop);
                return;
            }
        }
        
        // 2. 如果不存在，自动创建并压入列表
        MaterialProperty newProp;
        newProp.UniformName = name;
        newProp.DisplayName = name; 
        newProp.Type = type;
        assignFunc(newProp);
        Properties.push_back(newProp);
    }

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
            p.RuntimeTextureID = 0; // 清除可能残留的运行时 ID
        });
    }

    void Material::SetRuntimeTexture(const std::string& name, uint32_t rendererID) {
        SetPropertyInternal(name, MaterialPropertyType::Texture2D, [&](MaterialProperty& p) { 
            p.RuntimeTextureID = rendererID; 
            p.TextureHandle = 0; // 清除可能残留的资产 ID
        });
    }
}