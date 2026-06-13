#pragma once
#include "Renderer/SceneRenderer.hpp"
#include <memory>
#include <vector>
#include <string>

namespace Ayaya {

    // Human-readable metadata for each RenderGraph attachment.
    struct AttachmentInfo {
        int         Index;
        std::string Label;       // e.g. "Normal (RG16F)"
        bool        IsDepth = false;
    };

    struct PassTextureInfo {
        std::string PassName;          // e.g. "SSAOPass"
        std::string TextureKey;        // RenderGraph key, e.g. "SSAO_Final"
    };

    class FrameDebuggerPanel {
    public:
        FrameDebuggerPanel();

        void SetContext(const std::shared_ptr<SceneRenderer>& renderer) { m_Renderer = renderer; }
        void OnImGuiRender();

        void Reset() {
            m_Captured = false;
            m_HasSnapshot = false;
            m_SnapshotFramebuffers.clear();
            m_SnapshotDebugInfos.clear();
            m_SelectedPass = -1;
            m_SelectedAttach = 0;
            if (m_Renderer) m_Renderer->MarkViewportDirty();
        }

        bool IsOpen = false;

    private:
        void DrawPassOutputsTab();
        void DrawProfilerTab();
        void DrawAllTexturesTab();

        // Build the pass→texture mapping (static keys, runtime attachments).
        void BuildPassTextureMap();
        // Return dynamic attachment info for a framebuffer texture.
        static std::vector<AttachmentInfo> GetAttachmentInfo(
            const std::shared_ptr<Framebuffer>& fbo,
            const std::string& passName);
        // Show a texture preview (uses ImGui::Image with correct aspect ratio).
        void ShowTexturePreview(void* texID, const char* label = nullptr);

        std::shared_ptr<SceneRenderer> m_Renderer;
        std::vector<PassTextureInfo> m_PassTextures;

        // Snapshot state (freeze frame)
        bool m_Captured = false;
        int  m_SelectedPass  = -1;
        int  m_SelectedAttach = 0;

        // Frozen snapshot
        std::unordered_map<std::string, std::shared_ptr<Framebuffer>> m_SnapshotFramebuffers;
        std::unordered_map<std::string, PassDebugInfo>              m_SnapshotDebugInfos;
        bool m_HasSnapshot = false;
    };

}
