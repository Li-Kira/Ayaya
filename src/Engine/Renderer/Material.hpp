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

    // SRP LightMode bitmask — a material can participate in multiple passes simultaneously.
    enum class LightModeFlags : uint32_t {
        None         = 0,
        GBuffer      = 1u << 0,  // 1
        ShadowCaster = 1u << 1,  // 2
        Forward      = 1u << 2,  // 4
        DepthPrePass = 1u << 3,  // 8
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

        // SRP LightMode bitmask — bitwise OR of LightModeFlags for multi-pass participation.
        // 0 = auto-detect from BlendMode (Opaque/Masked→GBuffer|ShadowCaster, Translucent→Forward|ShadowCaster)
        void SetLightModeMask(uint32_t mask) { m_LightModeMask = mask; }
        uint32_t GetLightModeMask() const;
        // UI/Serialization: string representation (comma-separated or YAML array)
        const std::string& GetLightModeStr() const { return m_LightModeStr; }
        void SetLightModeStr(const std::string& str) { m_LightModeStr = str; }

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
            uint32_t UseORMMap = 0;
            // Bindless texture indices (defaults point to fixed 1×1 textures)
            uint32_t AlbedoMapIndex = 1;      // white (multiplicative identity)
            uint32_t NormalMapIndex = 3;      // default flat normal (Z-up)
            uint32_t ORMMapIndex = 2;         // black (unused when UseORMMap=0)
            uint32_t MetallicMapIndex = 1;    // white → scalar * 1 = scalar
            uint32_t RoughnessMapIndex = 1;   // white
            uint32_t AOMapIndex = 1;          // white
            uint32_t AlphaMapIndex = 1;       // white (multiplicative identity)
            bool Dirty = true;

            // Get final scalar values for GPU rendering.
            // When a texture map is present (index ≠ default white=1), the scalar
            // is overridden to 1.0 so the shader's `scalar * texture` becomes
            // `1.0 * texture = texture` — matching the pre-bindless "texture replaces scalar" semantic.
            // Albedo is intentionally excluded: `albedo * texture` = tint behavior.
            void GetRenderScalars(float& outMetallic, float& outRoughness, float& outAO) const {
                outMetallic  = (MetallicMapIndex  != 1) ? 1.0f : Metallic;
                outRoughness = (RoughnessMapIndex != 1) ? 1.0f : Roughness;
                outAO        = (AOMapIndex        != 1) ? 1.0f : AO;
            }
        };
        const BakedPC& GetBakedPC();
        void BakeProperties();  // rebuild BakedPC from Properties vector

    private:
        MaterialBlendMode m_BlendMode = MaterialBlendMode::Opaque;
        uint32_t m_LightModeMask = 0;     // 0 = auto-detect from BlendMode
        std::string m_LightModeStr;        // UI/serialization: comma-separated "GBuffer,ShadowCaster"
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