#pragma once

#include "Renderer/Texture.hpp"
#include "Core/UUID.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace Ayaya {

    class Shader;
    class Pipeline;
    class Framebuffer;
    class UniformBuffer;
    class Model;

    class AssetPreviewer {
    public:
        static void Init();
        static void Shutdown();

        static std::shared_ptr<Texture2D> RenderRealtimePreview(UUID modelHandle, glm::vec2 cameraAngle, uint32_t size = 256);
        static std::shared_ptr<Texture2D> GenerateThumbnail(UUID modelHandle, uint32_t size = 128);
        static std::shared_ptr<Texture2D> GenerateThumbnailForMaterial(UUID materialHandle, uint32_t size = 128);
        static std::shared_ptr<Texture2D> GenerateThumbnailForPrefab(UUID prefabHandle, uint32_t size = 128);
        static std::shared_ptr<Texture2D> RenderRealtimePreviewForPrefab(UUID prefabHandle, glm::vec2 cameraAngle, uint32_t size = 256);

        // GPU-resident thumbnail pipeline — renders one per frame using main frame CB
        static void RequestThumbnail(UUID handle, int assetType);
        static void ProcessOneThumbnail();  // called from OnUpdate, renders ONE thumbnail
        static std::shared_ptr<Texture2D> GetCachedThumbnail(UUID handle);
        static void InvalidateThumbnail(UUID handle);

    private:
        static void AutoFrameCamera(const std::shared_ptr<Model>& model, glm::vec2 orbitAngle, float fovY);
        static void RenderModel(const std::shared_ptr<Model>& model);
        static void RenderModel_OpenGL(const std::shared_ptr<Model>& model);
        static void RenderModel_Vulkan(const std::shared_ptr<Model>& model);
        static std::shared_ptr<Texture2D> ReadbackRealtime();
        static std::shared_ptr<Texture2D> ReadbackStandalone();

        // Shared resources
        static std::shared_ptr<Shader>      s_PreviewShader;
        static std::shared_ptr<Framebuffer> s_PreviewFBO;       // resolve / readback target (non-MSAA)
        static std::shared_ptr<Framebuffer> s_PreviewFBO_MSAA;  // MSAA render target
        static std::shared_ptr<Texture2D>   s_RealtimeWrapper;

        // Vulkan-only resources
        static std::shared_ptr<Pipeline>      s_PreviewPipeline;
        static std::shared_ptr<UniformBuffer> s_CameraUBO;

        static UUID     s_LastModelHandle;
        static uint32_t s_FboSize;
        static glm::mat4 s_ViewMatrix;
        static glm::mat4 s_ProjMatrix;
        static glm::vec3 s_CameraPos;
    };

}
