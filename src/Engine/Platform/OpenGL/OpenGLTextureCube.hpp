#pragma once
#include "Renderer/TextureCube.hpp"

namespace Ayaya {

    class OpenGLTextureCube : public TextureCube {
    public:
        OpenGLTextureCube(const std::vector<std::string>& faces);
        OpenGLTextureCube(uint32_t rendererID, int width, int height);
        virtual ~OpenGLTextureCube() override;

        virtual uint32_t GetWidth() const override { return m_Width; }
        virtual uint32_t GetHeight() const override { return m_Height; }
        virtual uint32_t GetRendererID() const override { return m_RendererID; }
        
        virtual void SetData(void* data, uint32_t size) override { /* 暂不实现动态更新数据 */ }

        virtual void Bind(uint32_t slot = 0) const override;
        virtual void Unbind() const override;

        virtual AssetType GetType() const override { return AssetType::TextureCube; }

    private:
        uint32_t m_RendererID;
        int m_Width = 0;
        int m_Height = 0;
    };

}