#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "Engine/Core/UUID.hpp"

namespace Ayaya {

    class RenderCommandBuffer;
    class Pipeline;
    class Texture2D;
    class TextureCube;

    enum class MaterialPropertyType {
        Float     = 0, 
        Int       = 1,
        Bool      = 2,
        Vec2      = 3, 
        Vec3      = 4, 
        Vec4      = 5, 
        Mat3      = 6,
        Mat4      = 7,
        Texture2D   = 8,
        TextureCube = 9
    };

    struct MaterialProperty {
        std::string UniformName;   
        std::string DisplayName;   
        MaterialPropertyType Type;

        float FloatValue = 0.0f;
        int IntValue = 0;
        bool BoolValue = false;
        glm::vec2 Vec2Value{0.0f, 0.0f};
        glm::vec3 Vec3Value{1.0f, 1.0f, 1.0f};
        glm::vec4 Vec4Value{1.0f, 1.0f, 1.0f, 1.0f};
        glm::mat3 Mat3Value{1.0f};
        glm::mat4 Mat4Value{1.0f};
        
        UUID TextureHandle = 0;
        
        // 【核心修改】：将数字 ID 替换为纹理对象的智能指针
        std::shared_ptr<Texture2D> RuntimeTexture = nullptr;
        std::shared_ptr<TextureCube> RuntimeTextureCube = nullptr;

        std::string TexturePath = ""; // 完美保留！
    };

    class Material {
    public:
        Material() = default;
        ~Material() = default;
        
        std::string Name = "Empty Material";
        std::string ShaderName = "Default";   // 完美保留！
        std::string AssetPath = "";           // 完美保留！

        std::vector<MaterialProperty> Properties;

        std::shared_ptr<Material> Clone() const {
            auto clone = std::make_shared<Material>();
            clone->Name = this->Name + " (Instance)"; 
            clone->ShaderName = this->ShaderName;
            clone->AssetPath = ""; 
            clone->Properties = this->Properties; 
            return clone;
        }

        // ==========================================
        // 核心执行：将所有属性打包提交给显卡
        // ==========================================
        // 【核心修改】：回退白模纹理现在也接收对象指针
        void Bind(RenderCommandBuffer& cmd, const std::shared_ptr<Pipeline>& pipeline, const std::shared_ptr<Texture2D>& fallbackWhiteTexture = nullptr);

        // ==========================================
        // 描述符更新接口 (代替 Shader->SetX)
        // ==========================================
        void SetFloat(const std::string& name, float value);
        void SetInt(const std::string& name, int value);
        void SetBool(const std::string& name, bool value);
        void SetVec2(const std::string& name, const glm::vec2& value);
        void SetVec3(const std::string& name, const glm::vec3& value);
        void SetVec4(const std::string& name, const glm::vec4& value);
        void SetMat3(const std::string& name, const glm::mat3& value);
        void SetMat4(const std::string& name, const glm::mat4& value);
        
        // 资产系统纹理绑定
        void SetTexture(const std::string& name, UUID textureHandle);

        // 动态 FBO 纹理绑定 (给 LightingPass / PostProcessPass 专用)
        // 【核心修改】：动态纹理绑定 (给特定运行时效果注入贴图)
        void SetRuntimeTexture(const std::string& name, const std::shared_ptr<Texture2D>& texture);
        // 【新增】：动态 TextureCube 绑定 (例如注入预滤波环境贴图 PrefilterMap)
        void SetRuntimeTextureCube(const std::string& name, const std::shared_ptr<TextureCube>& texture);

    private:
        template<typename T>
        void SetPropertyInternal(const std::string& name, MaterialPropertyType type, T setter) {
            for (auto& prop : Properties) {
                if (prop.UniformName == name) {
                    setter(prop);
                    return;
                }
            }
            MaterialProperty newProp;
            newProp.UniformName = name;
            newProp.DisplayName = name;
            newProp.Type = type;
            setter(newProp);
            Properties.push_back(newProp);
        }
    };

}