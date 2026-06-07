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

    // Exposed material blend mode — maps to RenderBucket for queue classification
    enum class MaterialBlendMode : uint8_t {
        Opaque      = 0,  // default, into GBuffer
        Masked      = 1,  // alpha-test, into GBuffer after opaque
        Translucent = 3,  // WBOIT translucent, skips GBuffer
    };

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
        std::string ShaderName = "Default";
        std::string AssetPath = "";

        // Blend mode → RenderBucket mapping for queue classification
        void SetBlendMode(MaterialBlendMode mode) { m_BlendMode = mode; }
        MaterialBlendMode GetBlendMode() const { return m_BlendMode; }
        uint8_t GetRenderBucket() const { return static_cast<uint8_t>(m_BlendMode); }

        // Built-in materials are shared read-only templates (e.g. DefaultPBR).
        // Editing them requires an explicit "Create Instance" via the editor.
        bool IsBuiltIn() const;

        // Alpha cutoff for Masked blend mode (alpha-test threshold)
        void SetAlphaCutoff(float cutoff) { m_AlphaCutoff = cutoff; }
        float GetAlphaCutoff() const { return m_AlphaCutoff; }

        std::vector<MaterialProperty> Properties;

        std::shared_ptr<Material> Clone() const {
            auto clone = std::make_shared<Material>();
            clone->Name = this->Name + " (Instance)";
            clone->ShaderName = this->ShaderName;
            clone->AssetPath = "";
            clone->m_BlendMode = this->m_BlendMode;
            clone->m_AlphaCutoff = this->m_AlphaCutoff;
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

        // ==========================================
        // Pre-baked push-constant cache (eliminates per-packet string parsing)
        // ==========================================
        struct BakedPC {
            // Scalar values (directly memcpy-able to push constants)
            glm::vec4 Albedo{1.0f};
            float Metallic = 0.0f, Roughness = 0.5f, AO = 1.0f, Alpha = 0.5f;
            int UseAlbedoMap = 0, UseNormalMap = 0, UseORMMap = 0;
            int UseMetallicMap = 0, UseRoughnessMap = 0, UseAOMap = 0;
            bool Dirty = true;
            // Pre-resolved texture pointers:
            //   slot 0=Albedo, 1=Normal, 2=ORM, 3=Metallic, 4=Roughness, 5=AO.
            // nullptr means "no texture, use white/blue fallback".
            std::shared_ptr<class Texture2D> Textures[6];
        };
        const BakedPC& GetBakedPC();
        void BakeProperties();  // rebuild BakedPC from Properties vector

    private:
        MaterialBlendMode m_BlendMode = MaterialBlendMode::Opaque;
        float m_AlphaCutoff = 0.5f;
        mutable BakedPC m_BakedPC;  // lazy-baked: rebuilt when Dirty

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