#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "Engine/Core/UUID.hpp"

namespace Ayaya {

    // ==========================================
    // 完整的材质属性类型枚举
    // ==========================================
    enum class MaterialPropertyType {
        Float     = 0, 
        Int       = 1,
        Bool      = 2,
        Vec2      = 3, 
        Vec3      = 4, 
        Vec4      = 5, 
        Mat3      = 6,
        Mat4      = 7,
        Texture2D = 8
    };

    // ==========================================
    // 动态属性条目
    // ==========================================
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
        // ==========================================
        // 新增：记录贴图的硬盘路径，用于重启后重新加载！
        // ==========================================
        std::string TexturePath = ""; 
    };

    // ==========================================
    // 纯数据驱动的材质类 (不再包含任何硬编码模板)
    // ==========================================
    class Material {
    public:
        std::string Name = "Empty Material";
        std::string ShaderName = "Default"; 
        std::string AssetPath = ""; 

        // 核心：属性列表
        // 如果为空，UI 面板就不渲染任何内容
        std::vector<MaterialProperty> Properties;

        Material() = default;
        ~Material() = default;

        std::shared_ptr<Material> Clone() const {
            auto clone = std::make_shared<Material>();
            
            // 名字加上 Instance 后缀，模仿 Unity
            clone->Name = this->Name + " (Instance)"; 
            clone->ShaderName = this->ShaderName;
            
            // 核心：清空文件路径！这样一旦点击保存，就会存成新文件，而不是覆盖母材质
            clone->AssetPath = ""; 
            
            // C++ 的 std::vector 支持直接赋值来进行深拷贝
            clone->Properties = this->Properties; 
            
            return clone;
        }
    };

    

}