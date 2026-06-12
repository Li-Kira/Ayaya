#include "ImportModelPanel.hpp"
#include "Asset/AssetManager.hpp"
#include "Core/VFS.hpp"
#include "Core/Log.hpp"
#include "Utils/PlatformUtils.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <thread>
#include <algorithm>

namespace Ayaya {

    // ---- Fuzzy texture matching helpers ----

    static bool IsImageFile(const std::filesystem::path& path) {
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".hdr" || ext == ".bmp" || ext == ".tga";
    }

    static float MatchScore(const std::string& name, const std::string& candidate) {
        std::string a = name, b = candidate;
        for (auto& c : a) c = (char)std::tolower(c);
        for (auto& c : b) c = (char)std::tolower(c);
        if (a == b) return 1.0f;
        if (b.find(a) != std::string::npos) return 0.9f;
        if (a.find(b) != std::string::npos) return 0.8f;
        return 0.0f;
    }

    static std::string GuessTextureLabel(const std::string& filename) {
        std::string lower = filename;
        for (auto& c : lower) c = (char)std::tolower(c);
        if (lower.find("_orm") != std::string::npos ||
            lower.find("_arm") != std::string::npos ||
            lower.find("occlusionroughnessmetallic") != std::string::npos)
            return "ORM";
        if (lower.find("albedo")   != std::string::npos || lower.find("diffuse")   != std::string::npos || lower.find("basecolor") != std::string::npos) return "Albedo";
        if (lower.find("normal")   != std::string::npos || lower.find("bump")      != std::string::npos || lower.find("_nrm") != std::string::npos || lower.find("_nor") != std::string::npos) return "Normal";
        if (lower.find("metallic") != std::string::npos || lower.find("metalness") != std::string::npos ||
            lower.find("specular") != std::string::npos || lower.find("_spec")    != std::string::npos) return "Metallic";
        if (lower.find("roughness")!= std::string::npos) return "Roughness";
        if (lower.find("ao")       != std::string::npos || lower.find("ambient")   != std::string::npos || lower.find("_ao") != std::string::npos) return "AO";
        if (lower.find("height")   != std::string::npos || lower.find("displace")  != std::string::npos || lower.find("_disp") != std::string::npos) return "Height";
        if (lower.find("emissive") != std::string::npos || lower.find("_emit")     != std::string::npos) return "Emissive";
        if (lower.find("opacity")  != std::string::npos || lower.find("alpha")     != std::string::npos) return "Opacity";
        return "Texture";
    }

    void ImportModelPanel::ScanTextures() {
        m_TextureEntries.clear();
        std::filesystem::path srcDir = m_TargetFilePath.parent_path();
        std::string baseName = m_TargetFilePath.stem().string();
        if (!std::filesystem::exists(srcDir)) return;

        std::vector<std::filesystem::path> candidates;
        for (auto& entry : std::filesystem::directory_iterator(srcDir)) {
            if (entry.is_regular_file() && IsImageFile(entry.path()))
                candidates.push_back(entry.path());
        }

        for (auto& cand : candidates) {
            std::string candStem = cand.stem().string();
            std::string cleanStem = candStem;
            for (auto& c : cleanStem) c = (char)std::tolower(c);
            float score = MatchScore(baseName, candStem);
            if (score > 0.0f) {
                TextureImportEntry entry;
                entry.SourcePath = cand;
                entry.Label = GuessTextureLabel(candStem);
                entry.Selected = true;
                m_TextureEntries.push_back(entry);
            }
        }
        std::sort(m_TextureEntries.begin(), m_TextureEntries.end(),
            [](const TextureImportEntry& a, const TextureImportEntry& b) {
                return a.Label < b.Label;
            });
    }

    void ImportModelPanel::AddTextureManual() {
        auto files = FileDialogs::OpenFiles(
            "Images (*.png *.jpg *.jpeg *.hdr *.bmp *.tga)|*.png;*.jpg;*.jpeg;*.hdr;*.bmp;*.tga");
        for (auto& filepath : files) {
            if (filepath.empty()) continue;
            bool dup = false;
            for (auto& entry : m_TextureEntries) { if (entry.SourcePath == filepath) { dup = true; break; } }
            if (dup) continue;
            TextureImportEntry entry;
            entry.SourcePath = filepath;
            entry.Label = GuessTextureLabel(std::filesystem::path(filepath).stem().string());
            entry.Selected = true;
            m_TextureEntries.push_back(entry);
        }
    }

    void ImportModelPanel::RequestOpen(const std::filesystem::path& filePath) {
        m_TargetFilePath = filePath;
        m_Settings = ModelImportSettings{};
        m_TextureEntries.clear();
        m_PendingOpen = true;
    }

    void ImportModelPanel::Draw() {
        if (m_PendingOpen) {
            m_PendingOpen = false;
            m_State = ImportState::Configuring;
            ScanTextures();
            ImGui::OpenPopup("Import Model Options");
        }
        if (m_State == ImportState::Hidden)
            return;

        // ---- Config modal ----
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(720.0f, 0.0f), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Import Model Options", nullptr,
                ImGuiWindowFlags_NoResize)) {

            std::string filename = m_TargetFilePath.filename().string();
            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.9f, 1.0f), "Import Model");
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("—  %s", filename.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            const float kLabelWidth = 170.0f;
            auto Labeled = [&](const char* label, float w = 0.0f) {
                ImGui::Text("%s", label);
                ImGui::SameLine(kLabelWidth);
                if (w <= 0.0f) w = ImGui::GetContentRegionAvail().x - 16.0f;
                ImGui::SetNextItemWidth(w);
            };

            // --- Transform ---
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                Labeled("Global Scale");
                ImGui::DragFloat("##GlobalScale", &m_Settings.GlobalScale, 0.01f, 0.01f, 100.0f, "%.3f");
                ImGui::Spacing();
            }

            // --- Geometry ---
            if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                const char* normalNames[] = { "Import (from file)", "Calculate (generate)", "None (skip)" };
                int nIdx = (int)m_Settings.Normals;
                Labeled("Normals");
                ImGui::Combo("##Normals", &nIdx, normalNames, 3);
                m_Settings.Normals = (NormalMode)nIdx;

                const char* tangentNames[] = { "Import (from file)", "Calculate (generate)", "None (skip)" };
                int tIdx = (int)m_Settings.Tangents;
                Labeled("Tangents");
                ImGui::Combo("##Tangents", &tIdx, tangentNames, 3);
                m_Settings.Tangents = (TangentMode)tIdx;

                ImGui::Spacing();
                ImGui::Checkbox("Optimize Mesh", &m_Settings.OptimizeMesh);
                ImGui::Checkbox("Swap YZ  (Maya Y-up  ->  Engine Z-up)", &m_Settings.SwapYZ);
                ImGui::Checkbox("Merge Meshes (single mesh, one material)", &m_Settings.MergeMeshes);
                ImGui::Spacing();
            }

            // --- Materials ---
            if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                ImGui::Checkbox("Import Materials", &m_Settings.ImportMaterials);
                if (m_Settings.ImportMaterials) {
                    ImGui::Spacing();
                    ImGui::Indent(16.0f);
                    const char* ruleNames[] = {
                        "Local Directory  (search next to FBX file)",
                        "Recursive Search  (walk up to project root)",
                        "Do Not Create  (use built-in default)"
                    };
                    int rIdx = (int)m_Settings.MatSearchRule;
                    Labeled("Search Rule");
                    ImGui::Combo("##SearchRule", &rIdx, ruleNames, 3);
                    m_Settings.MatSearchRule = (MaterialSearchRule)rIdx;
                    ImGui::Unindent(16.0f);
                }
                ImGui::Spacing();
            }

            // --- Animation ---
            if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                ImGui::Checkbox("Import Animations", &m_Settings.ImportAnimations);
                if (m_Settings.ImportAnimations) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(reserved)");
                }
                ImGui::Spacing();
            }

            // --- Textures ---
            if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                if (m_TextureEntries.empty()) {
                    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
                        "  No textures auto-detected near the model file.");
                } else {
                    ImGui::Text("Auto-detected  (%zu found):", m_TextureEntries.size());
                    ImGui::Spacing();

                    auto LabelColor = [](const std::string& label) -> ImVec4 {
                        if (label == "Albedo")    return ImVec4(1.00f, 0.85f, 0.45f, 1.0f);
                        if (label == "Normal")    return ImVec4(0.50f, 0.50f, 1.00f, 1.0f);
                        if (label == "ORM")       return ImVec4(0.30f, 0.90f, 0.60f, 1.0f);
                        if (label == "Metallic")  return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
                        if (label == "Roughness") return ImVec4(0.90f, 0.75f, 0.50f, 1.0f);
                        if (label == "AO")        return ImVec4(0.80f, 0.40f, 0.40f, 1.0f);
                        if (label == "Height")    return ImVec4(0.50f, 0.80f, 0.50f, 1.0f);
                        if (label == "Emissive")  return ImVec4(1.00f, 0.60f, 0.20f, 1.0f);
                        if (label == "Opacity")   return ImVec4(0.70f, 0.70f, 0.90f, 1.0f);
                        return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
                    };

                    for (size_t i = 0; i < m_TextureEntries.size(); i++) {
                        auto& entry = m_TextureEntries[i];
                        std::string chkId = "##tex" + std::to_string(i);
                        ImGui::Checkbox(chkId.c_str(), &entry.Selected);
                        ImGui::SameLine();
                        ImVec4 color = LabelColor(entry.Label);
                        ImGui::TextColored(color, "%-10s", entry.Label.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("—  %s", entry.SourcePath.filename().string().c_str());
                    }
                }
                ImGui::Spacing();

                // Auto-sized button: text width + padding
                float addTexWidth = ImGui::CalcTextSize("  +  Add Texture File...").x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
                if (ImGui::Button("  +  Add Texture File...", ImVec2(addTexWidth, 0.0f))) {
                    AddTextureManual();
                }
                ImGui::Spacing();
            }

            // --- Output ---
            if (ImGui::CollapsingHeader("Output", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Spacing();
                ImGui::Checkbox("Combine into Prefab", &m_Settings.CombineIntoPrefab);
                ImGui::SameLine();
                ImGui::TextDisabled("(recommended)");
                ImGui::Spacing();
            }

            ImGui::Separator();
            ImGui::Spacing();

            // --- Footer buttons (auto-sized, centered) ---
            float importBtnW = ImGui::CalcTextSize("  Import  ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float cancelBtnW = ImGui::CalcTextSize("  Cancel  ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float totalW = importBtnW + cancelBtnW + 10.0f;
            float offsetX = (ImGui::GetContentRegionAvail().x - totalW) * 0.5f;
            if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.45f, 0.25f, 1.0f));
            if (ImGui::Button("  Import  ", ImVec2(importBtnW, 0.0f))) {
                ImGui::CloseCurrentPopup();
                ExecuteImport();
            }
            ImGui::PopStyleColor(3);
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button("  Cancel  ", ImVec2(cancelBtnW, 0.0f))) {
                ImGui::CloseCurrentPopup();
                m_State = ImportState::Hidden;
            }

            ImGui::EndPopup();
        }

        if (m_State == ImportState::Configuring && !ImGui::IsPopupOpen("Import Model Options")) {
            m_State = ImportState::Hidden;
        }
    }

    void ImportModelPanel::ExecuteImport() {
        m_Settings.TextureFiles.clear();
        for (auto& entry : m_TextureEntries) {
            if (entry.Selected)
                m_Settings.TextureFiles.push_back(entry.SourcePath);
        }
        m_State = ImportState::Hidden;

        auto path = m_TargetFilePath;
        auto settings = m_Settings;

        std::thread([path, settings]() {
            ImportResult result = AssetManager::ImportModelAssetSync(path, settings);
            if (!result.Success) {
                AYAYA_CORE_ERROR("ImportModelPanel: model import failed: {0}", result.ErrorMsg);
                return;
            }
            AssetManager::SubmitToMainThread([result]() {
                AssetManager::FinalizeModelImport(result);
            });
        }).detach();
    }

}
