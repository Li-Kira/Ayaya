#include "ayapch.h"
#include "FrameDebuggerPanel.hpp"
#include <imgui.h>

namespace Ayaya {

    FrameDebuggerPanel::FrameDebuggerPanel() {
        BuildPassTextureMap();
    }

    // ==========================================
    // Static map of all RenderGraph texture keys → human-readable pass/attachment info.
    // This is the single source of truth for what the FrameDebugger can display.
    // When a new pass is added to the RenderGraph, add its outputs here.
    // ==========================================
    void FrameDebuggerPanel::BuildPassTextureMap() {
        m_PassTextures.clear();

        // --- Shadow ---
        m_PassTextures.push_back({"Shadow", "ShadowMap", {
            {0, "Shadow Depth", true}
        }});

        // --- GBuffer ---
        m_PassTextures.push_back({"GBuffer", "GBuffer", {
            {0, "Position (RGBA32F)"},
            {1, "Normal (RGBA16F)"},
            {2, "Albedo (RGBA8)"},
            {3, "PBR: M/R/AO/Unused (RGBA8)"},
            {4, "CustomData (RGBA8)"}
        }});

        // --- SSAO ---
        m_PassTextures.push_back({"SSAO", "SSAO_Final", {
            {0, "AO (R8)"}
        }});

        // --- Lighting (Deferred PBR) ---
        m_PassTextures.push_back({"Lighting", "Lighting", {
            {0, "HDR Color (RGBA16F)"}
        }});

        // --- Forward Blend (Skybox + Grid overlay) ---
        m_PassTextures.push_back({"ForwardBlend", "Lighting", {
            {0, "+Skybox/Grid (RGBA16F)"}
        }});

        // --- Outline / Selection Mask ---
        m_PassTextures.push_back({"Outline", "Selection", {
            {0, "Mask (RGBA8)"}
        }});

        // --- Bloom ---
        m_PassTextures.push_back({"Bloom", "Bloom", {
            {0, "Bloom (RGBA16F)"}
        }});

        // --- Post Process (ToneMapping + Bloom composite + Outline) ---
        m_PassTextures.push_back({"PostProcess", "PostProcess", {
            {0, "ToneMapped (RGBA8)"}
        }});

        // --- FXAA ---
        m_PassTextures.push_back({"FXAA", "FXAA", {
            {0, "Anti-Aliased (RGBA8)"}
        }});

        // --- UI ---
        m_PassTextures.push_back({"UI", "FXAA", {
            {0, "+UI (RGBA8)"}
        }});
    }

    void FrameDebuggerPanel::ShowTexturePreview(void* texID, const char* label) {
        if (!texID) {
            ImGui::TextDisabled("Texture not available (null descriptor).");
            return;
        }

        if (label) {
            ImGui::Text("%s", label);
            ImGui::Spacing();
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float aspect = 16.0f / 9.0f;
        ImVec2 imageSize;
        if (avail.x / avail.y > aspect) {
            imageSize.y = avail.y - 60.0f;
            imageSize.x = imageSize.y * aspect;
        } else {
            imageSize.x = avail.x - 20.0f;
            imageSize.y = imageSize.x / aspect;
        }
        if (imageSize.x < 64.0f) imageSize.x = 64.0f;
        if (imageSize.y < 36.0f) imageSize.y = 36.0f;

        // Center the image
        float cx = ImGui::GetCursorPosX() + (avail.x - imageSize.x) * 0.5f;
        if (cx > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(cx);

        ImGui::Image(texID, imageSize, ImVec2(0, 1), ImVec2(1, 0));
    }

    // ==========================================
    // TAB 1: Pass Outputs
    // ==========================================
    void FrameDebuggerPanel::DrawPassOutputsTab() {
        if (!m_Renderer) {
            ImGui::TextDisabled("No Renderer attached.");
            return;
        }

        auto& ctx = m_Renderer->GetRenderContext();

        // --- Top bar: freeze button ---
        ImGui::PushStyleColor(ImGuiCol_Button, m_Captured
            ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            m_Captured ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
        if (ImGui::Button(m_Captured ? "Live" : "Freeze", ImVec2(90, 32))) {
            m_Captured = !m_Captured;
            if (m_Captured) {
                // Snapshot current frame data
                m_SnapshotFramebuffers.clear();
                m_SnapshotDebugInfos.clear();
                for (auto& [k, v] : ctx.Framebuffers)
                    m_SnapshotFramebuffers[k] = v;  // shared_ptr, ref-counted
                for (auto& [k, v] : ctx.PassDebugInfos)
                    m_SnapshotDebugInfos[k] = v;    // deep copy
                m_HasSnapshot = true;
            } else {
                m_HasSnapshot = false;
            }
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (m_Captured)
            ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Frozen");
        else
            ImGui::TextDisabled("Live");
        ImGui::SameLine();
        auto& snapFBs = m_HasSnapshot ? m_SnapshotFramebuffers : ctx.Framebuffers;
        auto& snapDB  = m_HasSnapshot ? m_SnapshotDebugInfos  : ctx.PassDebugInfos;
        ImGui::TextDisabled(" | %zu textures  |  %zu passes",
            snapFBs.size(), snapDB.size());

        ImGui::Separator();
        ImGui::Spacing();

        // --- Resizable splitter layout ---
        // Use a persistent left-panel width that survives frame-to-frame.
        static float s_LeftPanelWidth = 420.0f;
        const float kMinLeft  = 200.0f;
        const float kMinRight = 200.0f;
        float availW = ImGui::GetContentRegionAvail().x;
        // Clamp to window size changes
        if (s_LeftPanelWidth > availW - kMinRight)
            s_LeftPanelWidth = availW - kMinRight;
        if (s_LeftPanelWidth < kMinLeft)
            s_LeftPanelWidth = kMinLeft;

        float availH = ImGui::GetContentRegionAvail().y;

        // --- Left panel: Pass tree ---
        ImGui::BeginChild("PassList", ImVec2(s_LeftPanelWidth, availH), true);
        {
            // Re-sync selection if current pass is no longer in range
            if (m_SelectedPass >= (int)m_PassTextures.size())
                m_SelectedPass = -1;

            int visibleOrder = 0;
            for (int p = 0; p < (int)m_PassTextures.size(); p++) {
                auto& info = m_PassTextures[p];

                // Check if this texture exists (in snapshot if frozen, else live)
                auto it = snapFBs.find(info.TextureKey);
                bool exists = (it != snapFBs.end() && it->second != nullptr);

                // Skip runtime-disabled passes (e.g. SSAO when EnableSSAO=false)
                bool runtimeActive = true;
                if (info.PassName == "SSAO")
                    runtimeActive = ctx.Get<bool>("EnableSSAO", false);

                if (!exists || !runtimeActive) continue;
                visibleOrder++;

                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.4f, 0.9f, 0.4f, 1.0f));

                // Label: pass name + texture key + resolution
                auto& spec = it->second->GetSpecification();
                char passLabel[128];
                snprintf(passLabel, sizeof(passLabel), "%s  \"%s\"  %dx%d",
                    info.PassName.c_str(), info.TextureKey.c_str(),
                    spec.Width, spec.Height);
                if (strlen(passLabel) > 48) {
                    passLabel[45] = '.'; passLabel[46] = '.'; passLabel[47] = '.'; passLabel[48] = 0;
                }

                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
                if (p == m_SelectedPass) flags |= ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                bool open = ImGui::TreeNodeEx(passLabel, flags);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    ImGui::SetTooltip("%s → %s", info.PassName.c_str(), info.TextureKey.c_str());
                if (ImGui::IsItemClicked()) { m_SelectedPass = p; m_SelectedAttach = 0; }

                if (open) {
                    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 16.0f);

                    for (int a = 0; a < (int)info.Attachments.size(); a++) {
                        auto& att = info.Attachments[a];
                        char buf[96];
                        snprintf(buf, sizeof(buf), "[%d] %s%s", att.Index,
                            att.Label.c_str(), att.IsDepth ? " (D)" : "");
                        bool sel = (m_SelectedPass == p && m_SelectedAttach == a);
                        if (ImGui::Selectable(buf, sel, ImGuiSelectableFlags_SpanAvailWidth)) {
                            m_SelectedPass = p; m_SelectedAttach = a;
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("Attachment %d: %s%s", att.Index,
                                att.Label.c_str(), att.IsDepth ? " (Depth)" : "");
                    }
                    ImGui::PopStyleVar();
                    ImGui::TreePop();
                }
            }
            if (visibleOrder == 0)
                ImGui::TextDisabled("No pass outputs available.");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // --- Draggable splitter ---
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.45f, 0.50f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
        ImGui::Button("##Splitter", ImVec2(6.0f, availH));
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemActive())
            s_LeftPanelWidth += ImGui::GetIO().MouseDelta.x;
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine();

        // --- Right panel: Preview + Debug Info ---
        float rightW = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild("PassPreview", ImVec2(rightW, 0), true);

        // Build list of visible pass indices for prev/next navigation
        std::vector<int> visiblePasses;
        for (int p = 0; p < (int)m_PassTextures.size(); p++) {
            auto& info = m_PassTextures[p];
            auto it = snapFBs.find(info.TextureKey);
            if (it == snapFBs.end() || !it->second) continue;
            // Runtime check: skip disabled passes (e.g. SSAO when EnableSSAO=false)
            if (info.PassName == "SSAO" && !ctx.Get<bool>("EnableSSAO", false)) continue;
            visiblePasses.push_back(p);
        }

        if (m_SelectedPass >= 0 && m_SelectedPass < (int)m_PassTextures.size()) {
            auto& info = m_PassTextures[m_SelectedPass];
            auto fboIt = snapFBs.find(info.TextureKey);
            if (fboIt != snapFBs.end() && fboIt->second) {
                auto& fbo = fboIt->second;
                auto& spec = fbo->GetSpecification();
                int attIdx = info.Attachments[m_SelectedAttach].Index;
                bool isDepth = info.Attachments[m_SelectedAttach].IsDepth;

                void* texID = isDepth
                    ? fbo->GetDepthAttachmentRendererID()
                    : fbo->GetColorAttachmentRendererID(attIdx);

                // Pass nav bar: ◀ prev | PassName | next ▶
                int curVisIdx = -1;
                for (int v = 0; v < (int)visiblePasses.size(); v++)
                    if (visiblePasses[v] == m_SelectedPass) { curVisIdx = v; break; }

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
                if (ImGui::ArrowButton("##prevPass", ImGuiDir_Left)) {
                    if (!visiblePasses.empty()) {
                        int prev = (curVisIdx > 0) ? curVisIdx - 1 : (int)visiblePasses.size() - 1;
                        m_SelectedPass = visiblePasses[prev];
                        m_SelectedAttach = 0;
                    }
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s  (%d/%d)", info.PassName.c_str(),
                    curVisIdx + 1, (int)visiblePasses.size());
                ImGui::PopStyleColor();
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
                if (ImGui::ArrowButton("##nextPass", ImGuiDir_Right)) {
                    if (!visiblePasses.empty()) {
                        int next = (curVisIdx + 1 < (int)visiblePasses.size()) ? curVisIdx + 1 : 0;
                        m_SelectedPass = visiblePasses[next];
                        m_SelectedAttach = 0;
                    }
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::TextDisabled("|  %s  |  %dx%d", info.TextureKey.c_str(),
                    spec.Width, spec.Height);
                ImGui::Separator();

                ImGui::Text("Attachment:  [%d] %s%s",
                    attIdx, info.Attachments[m_SelectedAttach].Label.c_str(),
                    isDepth ? " (Depth)" : "");

                auto dbgIt = snapDB.find(info.PassName);
                if (dbgIt != snapDB.end()) {
                    auto& dbg = dbgIt->second;
                    ImGui::Spacing();
                    ImGui::TextDisabled("--- Stats ---");
                    ImGui::Text("CPU: %.3f ms  |  Draws: %u  |  Tris: %u  |  Order: #%d",
                        dbg.CPUTime, dbg.DrawCalls, dbg.Triangles, dbg.Order);
                    if (!dbg.TexturesRead.empty() || !dbg.TexturesWritten.empty()) {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Reads: %s",
                            [&](){ std::string s; for(auto& r:dbg.TexturesRead){if(!s.empty())s+=", ";s+=r;} return s; }().c_str());
                        ImGui::TextDisabled("Writes: %s",
                            [&](){ std::string s; for(auto& w:dbg.TexturesWritten){if(!s.empty())s+=", ";s+=w;} return s; }().c_str());
                    }
                }

                ImGui::Separator();
                ShowTexturePreview(texID, nullptr);

                if (info.Attachments.size() > 1) {
                    ImGui::Spacing(); ImGui::Separator();
                    ImGui::Text("Attachment:");
                    ImGui::SameLine();
                    std::vector<const char*> attNames;
                    for (auto& a : info.Attachments) attNames.push_back(a.Label.c_str());
                    ImGui::SetNextItemWidth(280.0f);
                    ImGui::Combo("##AttachCombo", &m_SelectedAttach,
                        attNames.data(), (int)attNames.size());
                }
            } else {
                ImGui::TextDisabled("Texture \"%s\" not available.",
                    info.TextureKey.c_str());
            }
        } else {
            ImGui::TextDisabled("Select a pass output on the left to preview.");
        }
        ImGui::EndChild();
    }

    // ==========================================
    // TAB 2: Pipeline Profiler
    // ==========================================
    void FrameDebuggerPanel::DrawProfilerTab() {
        if (!m_Renderer) return;
        auto& ctx = m_Renderer->GetRenderContext();
        bool isVulkan = (RendererAPI::GetAPI() == RendererAPI::API::Vulkan);

        // Use snapshot if frozen, else live
        auto& snapDB = m_HasSnapshot ? m_SnapshotDebugInfos : ctx.PassDebugInfos;

        // Summary cards — aggregate from PassDebugInfos
        float totalCPU = 0.0f;
        uint32_t totalDC = 0, totalTris = 0;
        int nPasses = 0;
        for (auto& [_, p] : snapDB) {
            if (!p.Executed) continue;
            totalCPU += p.CPUTime; totalDC += p.DrawCalls; totalTris += p.Triangles; nPasses++;
        }

        // Fixed header: compact stats row + status (never scrolls away)
        ImGui::BeginChild("ProfHeader", ImVec2(0, 70), false);
        if (ImGui::BeginTable("ProfSum", 4, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("##h0", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##h1", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##h2", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##h3", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Executed"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,1,1,1), "%d", nPasses);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("CPU Total"); ImGui::SameLine();
            ImGui::TextColored(totalCPU > 16.6f ? ImVec4(1,0.4f,0.4f,1) : ImVec4(1,0.8f,0.2f,1),
                "%.2f ms", totalCPU);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("Draw Calls"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "%u", totalDC);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("Triangles"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "%u", totalTris);
            ImGui::EndTable();
        }
        if (isVulkan)
            ImGui::TextDisabled("Vulkan: GPU timing via VkQueryPool pending. CPU times shown.");
        ImGui::EndChild();
        ImGui::Separator();

        // Scrollable table fills remaining space
        if (!snapDB.empty()) {
            // Build sorted list by execution order
            std::vector<const PassDebugInfo*> sorted;
            for (auto& [name, info] : snapDB) sorted.push_back(&info);
            std::sort(sorted.begin(), sorted.end(),
                [](auto* a, auto* b) { return a->Order < b->Order; });

            if (ImGui::BeginTable("ProfTbl", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("#",       ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn("Pass",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU ms",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("GPU ms",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Draws",   ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Tris",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                for (auto* info : sorted) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%d", info->Order);
                    ImGui::TableSetColumnIndex(1);
                    ImVec4 col = info->Executed ? ImVec4(1,1,1,1) : ImVec4(0.5f,0.5f,0.5f,1);
                    ImGui::TextColored(col, "%s", info->PassName.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImVec4 tc = info->CPUTime > 2.0f ? ImVec4(1,0.3f,0.3f,1)
                        : info->CPUTime > 1.0f ? ImVec4(1,0.7f,0.2f,1)
                        : ImVec4(0.4f,1,0.4f,1);
                    ImGui::TextColored(tc, "%.3f", info->CPUTime);
                    ImGui::TableSetColumnIndex(3);
                    if (info->GPUTime > 0.0f) ImGui::Text("%.3f", info->GPUTime);
                    else ImGui::TextDisabled("-");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%u", info->DrawCalls);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%u", info->Triangles);
                }
                ImGui::EndTable();
            }
        } else {
            // Fallback: old PassProfiles (OpenGL path, or before RenderGraph populate)
            if (ImGui::BeginTable("ProfTbl2", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Pass",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("CPU ms",  ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("GPU ms",  ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Draws",   ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Tris",    ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();
                float tCPU=0, tGPU=0; uint32_t tDC=0, tT=0;
                for (auto& [n, p] : ctx.PassProfiles) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%s", n.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", p.CPUTime);
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", p.GPUTime);
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%u", p.DrawCalls);
                    ImGui::TableSetColumnIndex(4); ImGui::Text("%u", p.Triangles);
                    tCPU+=p.CPUTime; tGPU+=p.GPUTime; tDC+=p.DrawCalls; tT+=p.Triangles;
                }
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImVec4(1,1,0,1),"TOTAL");
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(1,1,0,1),"%.3f",tCPU);
                ImGui::TableSetColumnIndex(2); ImGui::TextColored(ImVec4(1,1,0,1),"%.3f",tGPU);
                ImGui::TableSetColumnIndex(3); ImGui::TextColored(ImVec4(1,1,0,1),"%u",tDC);
                ImGui::TableSetColumnIndex(4); ImGui::TextColored(ImVec4(1,1,0,1),"%u",tT);
                ImGui::EndTable();
            }
        }
    }

    // ==========================================
    // TAB 3: All Render Targets (raw list)
    // ==========================================
    void FrameDebuggerPanel::DrawAllTexturesTab() {
        if (!m_Renderer) {
            ImGui::TextDisabled("No Renderer attached.");
            return;
        }

        auto& ctx = m_Renderer->GetRenderContext();

        // Use snapshot if frozen, else live
        auto& snapFBs2 = m_HasSnapshot ? m_SnapshotFramebuffers : ctx.Framebuffers;
        auto& snapDB2  = m_HasSnapshot ? m_SnapshotDebugInfos  : ctx.PassDebugInfos;

        ImGui::Spacing();
        ImGui::Text("%zu framebuffers active", snapFBs2.size());
        ImGui::Spacing();

        if (snapFBs2.empty()) {
            ImGui::TextDisabled("No framebuffers registered this frame.");
            return;
        }

        // Build reverse map: texture key → producing pass
        std::unordered_map<std::string, std::string> producerMap;
        for (auto& [passName, dbg] : snapDB2)
            for (auto& w : dbg.TexturesWritten) producerMap[w] = passName;

        // Columns: Key(250) Producer(160) Res(140) Formats(340) Size(95) Preview(stretch)
        if (ImGui::BeginTable("AllTextures", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
            ImVec2(0, -1))) {
            ImGui::TableSetupColumn("Key",        ImGuiTableColumnFlags_WidthFixed, 250.0f);
            ImGui::TableSetupColumn("Producer",   ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Formats",    ImGuiTableColumnFlags_WidthFixed, 340.0f);
            ImGui::TableSetupColumn("Size",       ImGuiTableColumnFlags_WidthFixed, 95.0f);
            ImGui::TableSetupColumn("Preview",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            int id = 0;
            for (const auto& [name, fbo] : snapFBs2) {
                if (!fbo) continue;
                auto& spec = fbo->GetSpecification();
                ImGui::PushID(id++);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", name.c_str());

                ImGui::TableSetColumnIndex(1);
                auto prodIt = producerMap.find(name);
                if (prodIt != producerMap.end())
                    ImGui::Text("%s", prodIt->second.c_str());
                else
                    ImGui::TextDisabled("-");

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%dx%d", spec.Width, spec.Height);

                ImGui::TableSetColumnIndex(3);
                std::string fmtStr;
                for (auto& att : spec.Attachments.Attachments) {
                    if (att.TextureFormat == FramebufferTextureFormat::None) continue;
                    if (!fmtStr.empty()) fmtStr += ", ";
                    switch (att.TextureFormat) {
                        case FramebufferTextureFormat::RGBA8:    fmtStr += "RGBA8"; break;
                        case FramebufferTextureFormat::RGBA16F:  fmtStr += "RGBA16F"; break;
                        case FramebufferTextureFormat::RGBA32F:  fmtStr += "RGBA32F"; break;
                        case FramebufferTextureFormat::RG16F:    fmtStr += "RG16F"; break;
                        case FramebufferTextureFormat::R8:       fmtStr += "R8"; break;
                        case FramebufferTextureFormat::Depth:    fmtStr += "Depth"; break;
                        default: fmtStr += "?"; break;
                    }
                }
                ImGui::Text("%s", fmtStr.c_str());

                ImGui::TableSetColumnIndex(4);
                // Estimate VRAM size
                size_t bytes = 0;
                for (auto& att : spec.Attachments.Attachments) {
                    int bpp = 4;
                    switch (att.TextureFormat) {
                        case FramebufferTextureFormat::RGBA32F: bpp = 16; break;
                        case FramebufferTextureFormat::RGBA16F:
                        case FramebufferTextureFormat::RG16F:   bpp = 8; break;
                        case FramebufferTextureFormat::R8:      bpp = 1; break;
                        case FramebufferTextureFormat::Depth: bpp = 4; break;
                        default: break;
                    }
                    bytes += spec.Width * spec.Height * bpp;
                }
                if (bytes >= 1024*1024)
                    ImGui::Text("%.1f MB", bytes / (1024.0f*1024.0f));
                else if (bytes >= 1024)
                    ImGui::Text("%.1f KB", bytes / 1024.0f);
                else
                    ImGui::Text("%zu B", bytes);

                ImGui::TableSetColumnIndex(5);
                void* texID = fbo->GetColorAttachmentRendererID(0);
                if (texID) {
                    float aspect = spec.Height > 0 ? (float)spec.Width/(float)spec.Height : 16.0f/9.0f;
                    float th = 60.0f;  // compact thumbnail
                    float tw = std::min(th * aspect, 170.0f);
                    ImGui::Image(texID, ImVec2(tw, th), ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // ==========================================
    // Main entry point
    // ==========================================
    void FrameDebuggerPanel::OnImGuiRender() {
        if (!IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(1150, 780), ImGuiCond_FirstUseEver);
        ImGui::Begin("Frame Debugger", &IsOpen);

        if (!m_Renderer) {
            ImGui::TextDisabled("No Renderer attached.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("FrameDebuggerTabs")) {
            if (ImGui::BeginTabItem("Pass Outputs")) {
                DrawPassOutputsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pipeline Profiler")) {
                DrawProfilerTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("All Textures")) {
                DrawAllTexturesTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

}
