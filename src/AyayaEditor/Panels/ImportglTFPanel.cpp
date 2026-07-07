#include "ImportglTFPanel.hpp"
#include "Asset/glTF/glTFAssetImporter.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/Log.hpp"

#include <cgltf/cgltf.h>
#include <imgui.h>
#include <IconsFontAwesome5.h>
#include <thread>
#include <unordered_set>

namespace Ayaya {

// ==========================================
// URL Decode helper
// ==========================================
static std::string UrlDecode(const std::string& src) {
    std::string out; out.reserve(src.size());
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '%' && i+2 < src.size() && isxdigit(src[i+1]) && isxdigit(src[i+2])) {
            int hi = src[i+1] <= '9' ? src[i+1]-'0' : (src[i+1]|32)-'a'+10;
            int lo = src[i+2] <= '9' ? src[i+2]-'0' : (src[i+2]|32)-'a'+10;
            out += (char)((hi<<4)|lo); i += 2;
        } else out += src[i];
    }
    return out;
}

// ==========================================
// RequestOpen — start the panel flow
// ==========================================
void ImportglTFPanel::RequestOpen(const std::filesystem::path& filePath) {
    m_TargetFilePath = filePath;
    m_PendingOpen = true;
    m_ReferencedFiles.clear();
    m_StatsReady = false;
}

// ==========================================
// ScanReferencedFiles — parse glTF, collect URIs, check disk
// ==========================================
void ImportglTFPanel::ScanReferencedFiles() {
    m_ReferencedFiles.clear();
    m_StatsReady = false;

    cgltf_options opts{};
    cgltf_data* data = nullptr;
    std::string pathStr = m_TargetFilePath.string();
    if (cgltf_parse_file(&opts, pathStr.c_str(), &data) != cgltf_result_success) {
        AYAYA_CORE_ERROR("ImportglTFPanel: failed to parse {}", pathStr);
        return;
    }

    m_NodeCount = (int)data->nodes_count;
    m_MeshCount = (int)data->meshes_count;
    m_MaterialCount = (int)data->materials_count;
    m_TextureCount = (int)data->textures_count;
    m_LightCount = (int)data->lights_count;

    auto glTFDir = m_TargetFilePath.parent_path();
    std::unordered_set<std::string> seen;

    // External .bin files
    for (cgltf_size i = 0; i < data->buffers_count; i++) {
        if (data->buffers[i].uri && !strstr(data->buffers[i].uri, "data:")) {
            std::string decoded = UrlDecode(data->buffers[i].uri);
            auto resolved = std::filesystem::weakly_canonical(glTFDir / decoded);
            glTFFileEntry e;
            e.URI = decoded;
            e.ResolvedPath = resolved.string();
            e.Found = std::filesystem::exists(resolved);
            e.Selected = e.Found;
            m_ReferencedFiles.push_back(e);
            seen.insert(decoded);
        }
    }

    // Texture URIs
    for (cgltf_size i = 0; i < data->textures_count; i++) {
        auto* img = data->textures[i].image;
        std::string key;
        if (img->uri && !strstr(img->uri, "data:")) {
            key = img->uri;
        } else if (img->buffer_view) {
            // Embedded: generate a synthetic name
            key = "<embedded>_" + std::to_string(i)
                + (strstr(img->mime_type, "png") ? ".png" : ".jpg");
        } else continue;

        if (seen.count(key)) continue;
        seen.insert(key);

        glTFFileEntry e;
        e.URI = key;
        if (img->buffer_view) {
            e.ResolvedPath = "(embedded in GLB)";
            e.Found = true;
        } else {
            auto decoded = UrlDecode(img->uri);
            auto resolved = std::filesystem::weakly_canonical(glTFDir / decoded);
            e.ResolvedPath = resolved.string();
            e.Found = std::filesystem::exists(resolved);
        }
        e.Selected = e.Found;
        m_ReferencedFiles.push_back(e);
    }

    m_StatsReady = true;
    cgltf_free(data);
}

// ==========================================
// ExecuteImport — background thread entry
// ==========================================
void ImportglTFPanel::ExecuteImport() {
    m_State = ImportState::Hidden;
    std::string pathStr = m_TargetFilePath.string();

    glTFImportSettings settings;
    settings.ImportLights    = m_ImportLights;
    settings.ImportCameras   = m_ImportCameras;
    settings.GenerateMipmaps = m_GenerateMipmaps;

    // Suppress AssetWatcher during bulk import — prevents the file watcher from
    // triggering synchronous LoadAssetFromFile → stbi_load + GPU upload for every
    // newly-created texture/material/prefab on the main thread.
    AssetManager::SetBulkImportInProgress(true);

    std::thread([pathStr, settings] {
        auto result = ImportglTFSceneSync(pathStr, settings);
        if (result.Success)
            AssetManager::SubmitToMainThread([result]() mutable { FinalizeglTFImport(result); });
        else
            AYAYA_CORE_ERROR("glTF import failed: {}", result.ErrorMsg);
    }).detach();
}

// ==========================================
// Draw — ImGui panel
// ==========================================
void ImportglTFPanel::Draw() {
    if (m_PendingOpen) {
        m_PendingOpen = false;
        ScanReferencedFiles();
        m_State = ImportState::Configuring;
        ImGui::OpenPopup("Import glTF Scene");
    }

    if (m_State != ImportState::Configuring) return;

    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import glTF Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        m_State = ImportState::Hidden;
        return;
    }

    float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    ImGui::Text("Source: %s", m_TargetFilePath.string().c_str());
    ImGui::Separator();

    // ── Detected Files ──
    if (ImGui::CollapsingHeader("Detected Files", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%zu referenced files", m_ReferencedFiles.size());
        ImGui::BeginChild("Files", ImVec2(0, 120 * scale), true);
        for (auto& f : m_ReferencedFiles) {
            ImGui::Text("%s  %s  %s",
                f.Found ? ICON_FA_CHECK : ICON_FA_TIMES,
                f.URI.c_str(),
                f.Found ? "" : "(missing)");
        }
        ImGui::EndChild();
    }

    // ── Import Settings ──
    if (ImGui::CollapsingHeader("Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Combine into Prefab", &m_CombineIntoPrefab);
        ImGui::Checkbox("Import Lights (KHR_lights_punctual)", &m_ImportLights);
        ImGui::Checkbox("Import Cameras", &m_ImportCameras);
        ImGui::Checkbox("Generate Mipmaps", &m_GenerateMipmaps);
        ImGui::TextDisabled("(Mipmaps off avoids VUID layout errors on MoltenVK)");
    }

    // ── Scene Info ──
    if (m_StatsReady && ImGui::CollapsingHeader("Scene Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Nodes: %d   Meshes: %d", m_NodeCount, m_MeshCount);
        ImGui::Text("Materials: %d   Textures: %d", m_MaterialCount, m_TextureCount);
        ImGui::Text("Lights: %d", m_LightCount);
    }

    // ── Missing Files Warning ──
    int missing = 0;
    for (auto& f : m_ReferencedFiles) if (!f.Found) missing++;
    if (missing > 0) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "%s %d file(s) not found — textures may be missing",
            ICON_FA_EXCLAMATION_TRIANGLE, missing);
    }

    ImGui::Separator();
    ImGui::Text("Statistics: %d materials, %zu textures",
        m_MaterialCount, m_ReferencedFiles.size());

    if (ImGui::Button("Import", ImVec2(120, 0))) ExecuteImport();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) m_State = ImportState::Hidden;

    ImGui::EndPopup();
}

} // namespace Ayaya
