#pragma once

#include "Renderer/Texture.hpp"
#include <glad/glad.h>

namespace Ayaya {

    class OpenGLTexture2D : public Texture2D {
    public:
        OpenGLTexture2D(const std::string& path);
        // 新增构造函数声明
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(void* rendererID, uint32_t width, uint32_t height);
        OpenGLTexture2D(const RawTextureData& raw); // 异步加载

        virtual ~OpenGLTexture2D();

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetRendererID() const override { return m_RendererID; }
        virtual void* GetImGuiTextureID() const override;

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind() const override; // 【新增】

        // 新增 SetData 声明
        virtual void SetData(void* data, uint32_t size) override;
        virtual bool IsDataFlipped() const override { return true; }

    private:
        std::string m_Path;
        uint32_t m_Width, m_Height;
        uint32_t m_RendererID;
        GLenum m_InternalFormat, m_DataFormat;
    };

}