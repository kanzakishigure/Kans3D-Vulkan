#include "EditorLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "ImGuizmo.h"
#include "UI/EditorIcons.h"
#include "UI/Widgets.h"

// Editor window layout: menu bar, main toolbar, dock space with the panels, and
// the viewport with its overlay toolbar.

namespace Kans
{
    using Widgets::ToolButton;
    using Widgets::ToolSeparator;

    namespace
    {
        constexpr ImVec4 ErrorColor {0.95f, 0.38f, 0.35f, 1.0f};
        constexpr ImVec4 AccentColor {0.26f, 0.59f, 0.98f, 1.0f};

        std::string MenuLabel(const char* icon, const char* text) { return std::string(icon) + "  " + text; }
    } // namespace

    // ------------------------------------------------------------------ panels

    void EditorLayer::RegisterPanels()
    {
        const auto spec = [](const char* id, const char* title, const char* icon, PanelCategory category,
                             PanelDock dock, bool open) {
            PanelSpec value;
            value.ID          = id;
            value.Title       = title;
            value.Icon        = icon;
            value.Category    = category;
            value.Dock        = dock;
            value.DefaultOpen = open;
            return value;
        };
        auto viewport      = spec(PanelID::Viewport, "Viewport", ICON_FA_VIDEO, PanelCategory::Scene, PanelDock::Center, true);
        viewport.Closable  = false;
        viewport.NoPadding = true;
        viewport.Flags     = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        m_Panels.Register(viewport, [this] { m_ViewportPanel.Draw(); });
        m_Panels.Register(spec(PanelID::Hierarchy, "Hierarchy", ICON_FA_LIST, PanelCategory::Scene, PanelDock::Left, true),
                          [this] { m_HierarchyPanel.Draw(); });
        m_Panels.Register(
            spec(PanelID::Properties, "Properties", ICON_FA_SLIDERS, PanelCategory::Scene, PanelDock::Right, true),
            [this] { m_PropertiesPanel.Draw(); });

        auto browser     = spec(PanelID::ContentBrowser, "Content Browser", ICON_FA_FOLDER_TREE, PanelCategory::Assets,
                            PanelDock::Bottom, true);
        browser.Flags    = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        browser.Shortcut = "Ctrl+Space";
        m_Panels.Register(browser, [this] { m_ContentBrowserPanel.onImGuiRender(); });
        m_Panels.Register(spec(PanelID::AssetImporter, "Asset Importer", ICON_FA_FILE_IMPORT, PanelCategory::Assets,
                               PanelDock::Right, false),
                          [this] { m_ProductImportPanel.Draw(); });
        m_Panels.Register(spec(PanelID::MaterialEditor, "Material Editor", ICON_FA_PALETTE, PanelCategory::Assets,
                               PanelDock::Right, false),
                          [this] { m_MaterialEditorPanel.Draw(); });

        m_Panels.Register(spec(PanelID::Console, "Console", ICON_FA_TERMINAL, PanelCategory::Debug, PanelDock::Bottom, true),
                          [this] { m_Console.Draw(); });
        m_Panels.Register(spec(PanelID::Stats, "Stats", ICON_FA_CHART_LINE, PanelCategory::Debug, PanelDock::Left, true),
                          [this] { DrawStats(); });
    }

    void EditorLayer::OpenImporter(const std::filesystem::path& file)
    {
        m_ProductImportPanel.OpenWithFile(file);
        m_Panels.Open(PanelID::AssetImporter);
    }

    void EditorLayer::OpenMaterial(const std::filesystem::path& file)
    {
        m_MaterialEditorPanel.Open(file);
        m_Panels.Open(PanelID::MaterialEditor);
    }

    void EditorLayer::CreateMaterial(const std::filesystem::path& directory)
    {
        m_MaterialEditorPanel.Create(directory);
        m_ContentBrowserPanel.Refresh();
        m_Panels.Open(PanelID::MaterialEditor);
    }

    // ------------------------------------------------------------------ frame

    void EditorLayer::OnImGuiRender()
    {
        PROFILE_FUCTION();
        HandleShortcuts();

        // Full-window host for the menu bar, the main toolbar and the dock space.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
                                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##EditorDockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);
        DrawMenuBar();
        DrawMainToolbar();
        const ImGuiID dockspace = ImGui::GetID("EditorDockSpace");
        m_Panels.PrepareDockSpace(dockspace, ImGui::GetContentRegionAvail());
        ImGui::DockSpace(dockspace);
        DrawSceneFileDialogs();
        ImGui::End();

        m_ViewportPanel.SetPlaying(m_Play.IsActive());
        m_Panels.Draw();
        Application::Get().GetImGuiLayer()->BlockEvents(m_ViewportPanel.IsFocused() || m_ViewportPanel.IsHovered());
        TrackSceneEdits();
    }

    void EditorLayer::TrackSceneEdits()
    {
        if (!m_ActiveScene)
            return;
        // Changes made while playing are discarded on stop, not recorded.
        if (m_Play.IsActive())
        {
            m_EditPending = false;
            return;
        }
        // Edits come from user input; record once the interaction has ended
        // (drags released, text fields left, gizmo let go), so one drag is one step.
        const ImGuiIO& io = ImGui::GetIO();
        for (int button = 0; button < IM_ARRAYSIZE(io.MouseReleased); ++button)
            m_EditPending |= io.MouseReleased[button];
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !m_EditPending; ++key)
            m_EditPending |= ImGui::IsKeyReleased(ImGuiKey(key));
        if (m_EditPending && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() && !ImGui::IsAnyMouseDown())
        {
            m_History.Record(*m_ActiveScene);
            m_EditPending = false;
        }
    }

    void EditorLayer::UndoScene(int steps)
    {
        if (!m_ActiveScene || m_Play.IsActive())
            return;
        // Pending changes become a step first, so they can be undone too.
        m_History.Record(*m_ActiveScene);
        m_EditPending = false;
        for (int i = 0; i < steps && m_History.Undo(*m_ActiveScene); ++i)
            ;
    }

    void EditorLayer::RedoScene(int steps)
    {
        if (!m_ActiveScene || m_Play.IsActive())
            return;
        m_History.Record(*m_ActiveScene);
        m_EditPending = false;
        for (int i = 0; i < steps && m_History.Redo(*m_ActiveScene); ++i)
            ;
    }

    void EditorLayer::HandleShortcuts()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
            return;
        if (io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_N, false))
                Record(NewScene());
            if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                m_SceneDialog = SceneDialog::Open;
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            {
                if (io.KeyShift)
                    m_SceneDialog = SceneDialog::Save;
                else
                    SaveCurrentScene();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
                m_Panels.Toggle(PanelID::ContentBrowser);
            if (ImGui::IsKeyPressed(ImGuiKey_Z, true))
            {
                if (io.KeyShift)
                    RedoScene();
                else
                    UndoScene();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Y, true))
                RedoScene();
            if (ImGui::IsKeyPressed(ImGuiKey_P, false))
            {
                if (io.KeyShift)
                    TogglePause();
                else if (m_Play.IsActive())
                    StopPlay();
                else
                    StartPlay();
            }
        }
    }

    // ------------------------------------------------------------------ play mode

    void EditorLayer::StartPlay()
    {
        if (!m_ActiveScene)
            return;
        if (!m_Play.IsActive())
        {
            // Pending edits become a step first; play itself is not recorded.
            m_History.Record(*m_ActiveScene);
            m_EditPending = false;
        }
        m_Play.Play(*m_ActiveScene);
    }

    void EditorLayer::StopPlay()
    {
        if (!m_ActiveScene || !m_Play.IsActive())
            return;
        // Restoring keeps UUIDs, so the selection can be found again.
        const UUID selected = m_HierarchyPanel.GetSelected() ? m_HierarchyPanel.GetSelected().GetUUID() : UUID(0);
        m_Play.Stop(*m_ActiveScene);
        m_HierarchyPanel.SetSelected(selected ? m_ActiveScene->GetEntityByUUID(selected) : Entity {});
    }

    void EditorLayer::TogglePause()
    {
        if (!m_ActiveScene || !m_Play.IsActive())
            return;
        if (m_Play.GetState() == ScenePlaySession::State::Paused)
            m_Play.Play(*m_ActiveScene);
        else
            m_Play.Pause();
    }

    void EditorLayer::DrawPlayControls()
    {
        using State         = ScenePlaySession::State;
        const State   state = m_Play.GetState();
        // Centered in the toolbar.
        const float width = 3.0f * 30.0f + 4.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - width) * 0.5f));
        if (state == State::Edit)
        {
            if (ToolButton(ICON_FA_PLAY, "Play (Ctrl+P)"))
                StartPlay();
        }
        else if (ToolButton(ICON_FA_STOP, "Stop and restore the scene (Ctrl+P)", true))
            StopPlay();
        ImGui::SameLine();
        if (ToolButton(ICON_FA_PAUSE, "Pause (Ctrl+Shift+P)", state == State::Paused, state != State::Edit))
            TogglePause();
        ImGui::SameLine();
        if (ToolButton(ICON_FA_FORWARD_STEP, "Advance one frame", false, state == State::Paused))
            m_Play.Step();
    }

    // ------------------------------------------------------------------ menu bar and main toolbar

    void EditorLayer::DrawMenuBar()
    {
        if (!ImGui::BeginMenuBar())
            return;
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FILE, "New Scene").c_str(), "Ctrl+N"))
                Record(NewScene());
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FOLDER_OPEN, "Open Scene...").c_str(), "Ctrl+O"))
                m_SceneDialog = SceneDialog::Open;
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FLOPPY_DISK, "Save Scene").c_str(), "Ctrl+S", false, !m_Play.IsActive()))
                SaveCurrentScene();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FLOPPY_DISK, "Save Scene As...").c_str(), "Ctrl+Shift+S", false, !m_Play.IsActive()))
                m_SceneDialog = SceneDialog::Save;
            ImGui::Separator();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_XMARK, "Exit").c_str()))
                Application::Get().Close();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            const auto undo = m_History.UndoLabels();
            const auto redo = m_History.RedoLabels();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_UNDO, undo.empty() ? "Undo" : ("Undo " + undo.front()).c_str()).c_str(),
                                "Ctrl+Z",
                                false,
                                !undo.empty() && !m_Play.IsActive()))
                UndoScene();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_REDO, redo.empty() ? "Redo" : ("Redo " + redo.front()).c_str()).c_str(),
                                "Ctrl+Y",
                                false,
                                !redo.empty() && !m_Play.IsActive()))
                RedoScene();
            ImGui::Separator();
            // Jump several steps at once.
            if (ImGui::BeginMenu(MenuLabel(ICON_FA_CLOCK_ROTATE_LEFT, "History").c_str(),
                                 (!undo.empty() || !redo.empty()) && !m_Play.IsActive()))
            {
                for (size_t i = redo.size(); i-- > 0;)
                {
                    ImGui::PushID(int(1000 + i));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::MenuItem(redo[i].c_str()))
                        RedoScene(int(i + 1));
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::MenuItem(ICON_FA_CHECK "  Current state", nullptr, true, false);
                for (size_t i = 0; i < undo.size(); ++i)
                {
                    ImGui::PushID(int(i));
                    if (ImGui::MenuItem(undo[i].c_str()))
                        UndoScene(int(i + 1));
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Play"))
        {
            const bool active = m_Play.IsActive();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_PLAY, active ? "Resume" : "Play").c_str(),
                                active ? nullptr : "Ctrl+P",
                                false,
                                !active || m_Play.GetState() == ScenePlaySession::State::Paused))
                StartPlay();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_PAUSE, "Pause").c_str(),
                                "Ctrl+Shift+P",
                                false,
                                m_Play.GetState() == ScenePlaySession::State::Playing))
                m_Play.Pause();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FORWARD_STEP, "Step Frame").c_str(),
                                nullptr,
                                false,
                                m_Play.GetState() == ScenePlaySession::State::Paused))
                m_Play.Step();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_STOP, "Stop").c_str(), active ? "Ctrl+P" : nullptr, false, active))
                StopPlay();
            ImGui::EndMenu();
        }
                if (ImGui::BeginMenu("Assets"))
        {
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FILE_IMPORT, "Asset Importer").c_str()))
                m_Panels.Open(PanelID::AssetImporter);
            if (ImGui::MenuItem(MenuLabel(ICON_FA_PALETTE, "Create Material").c_str()))
                CreateMaterial(m_ContentBrowserPanel.GetCurrentDirectory());
            ImGui::Separator();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_ROTATE_RIGHT, "Refresh Published Products").c_str()))
                Record(RefreshProductAssets());
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Window"))
        {
            m_Panels.DrawWindowMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    void EditorLayer::DrawMainToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));
        ImGui::BeginChild("##MainToolbar",
                          {0.0f, 34.0f},
                          false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

        // Scene
        if (ToolButton(ICON_FA_FILE, "New scene (Ctrl+N)"))
            Record(NewScene());
        ImGui::SameLine();
        if (ToolButton(ICON_FA_FOLDER_OPEN, "Open scene (Ctrl+O)"))
            m_SceneDialog = SceneDialog::Open;
        ImGui::SameLine();
        if (ToolButton(ICON_FA_FLOPPY_DISK, "Save scene (Ctrl+S)", false, !m_Play.IsActive()))
            SaveCurrentScene();
        ToolSeparator();
        {
            const auto undo = m_History.UndoLabels();
            const auto redo = m_History.RedoLabels();
            const auto undoTip = undo.empty() ? std::string("Nothing to undo") : "Undo " + undo.front() + " (Ctrl+Z)";
            const auto redoTip = redo.empty() ? std::string("Nothing to redo") : "Redo " + redo.front() + " (Ctrl+Y)";
            const bool editing = !m_Play.IsActive();
            if (ToolButton(ICON_FA_UNDO, undoTip.c_str(), false, editing && !undo.empty()))
                UndoScene();
            ImGui::SameLine();
            if (ToolButton(ICON_FA_REDO, redoTip.c_str(), false, editing && !redo.empty()))
                RedoScene();
        }
        ToolSeparator();
        // Assets
        if (ToolButton(ICON_FA_FILE_IMPORT, "Asset importer", m_Panels.IsOpen(PanelID::AssetImporter)))
            m_Panels.Toggle(PanelID::AssetImporter);
        ImGui::SameLine();
        if (ToolButton(ICON_FA_PALETTE, "Create a material in the content browser's folder"))
            CreateMaterial(m_ContentBrowserPanel.GetCurrentDirectory());
        ImGui::SameLine();
        if (ToolButton(ICON_FA_ROTATE_RIGHT, "Refresh published products"))
            Record(RefreshProductAssets());
        ToolSeparator();
        // Panels
        if (ToolButton(ICON_FA_FOLDER_TREE, "Content browser (Ctrl+Space)", m_Panels.IsOpen(PanelID::ContentBrowser)))
            m_Panels.Toggle(PanelID::ContentBrowser);
        ImGui::SameLine();
        const size_t errors = m_Console.ErrorCount();
        if (ToolButton(ICON_FA_TERMINAL, "Console", m_Panels.IsOpen(PanelID::Console)))
            m_Panels.Toggle(PanelID::Console);
        if (errors)
        {
            // Error badge on the console button.
            const ImVec2 corner = ImGui::GetItemRectMax();
            const auto   text   = std::to_string(std::min<size_t>(errors, 99));
            ImGui::GetWindowDrawList()->AddCircleFilled({corner.x - 4.0f, corner.y - 20.0f},
                                                        7.0f,
                                                        ImGui::GetColorU32(ErrorColor));
            const ImVec2 size = ImGui::CalcTextSize(text.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImGui::GetFont(),
                ImGui::GetFontSize() * 0.75f,
                {corner.x - 4.0f - size.x * 0.375f, corner.y - 20.0f - size.y * 0.375f},
                IM_COL32_WHITE,
                text.c_str());
        }
        ImGui::SameLine();
        if (ToolButton(ICON_FA_CHART_LINE, "Stats and render settings", m_Panels.IsOpen(PanelID::Stats)))
            m_Panels.Toggle(PanelID::Stats);
        DrawPlayControls();

        // Right side: scene name, import status and the last error.
        std::string status = ICON_FA_CUBE "  " + (m_ScenePath.empty() ? std::string("Untitled scene") :
                                                                         m_ScenePath.filename().u8string());
        if (m_History.IsModified())
            status += " *";
        if (m_Play.IsActive())
        {
            char clock[64];
            std::snprintf(clock,
                          sizeof(clock),
                          "  |  %s %.1f s",
                          m_Play.GetState() == ScenePlaySession::State::Paused ? "Paused" : "Playing",
                          m_Play.GetPlayTime());
            status += clock;
        }
        bool importing = false;
        if (m_AssetImports)
            if (auto snapshot = m_AssetImports->GetSnapshot();
                std::holds_alternative<EditorAssetImportSnapshot>(snapshot) &&
                std::get<EditorAssetImportSnapshot>(snapshot).State == EditorAssetImportState::Building)
            {
                const auto source = std::get<EditorAssetImportSnapshot>(snapshot).Source;
                if (source != m_StatusSource)
                {
                    m_StatusSource = source;
                    m_StatusName   = source.ToString();
                    if (auto  sources = m_AssetImports->GetSources();
                        auto* list    = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
                        for (const auto& metadata : *list)
                            if (metadata.sourceAssetID == source)
                                m_StatusName = metadata.Path.GetPath().filename().u8string();
                }
                importing = true;
            }
        const std::string importText = importing ? ICON_FA_SPINNER "  Importing " + m_StatusName + "..." : "";
        const std::string errorText =
            m_SceneFileError.empty() ? "" : ICON_FA_CIRCLE_XMARK "  " + m_SceneFileError.substr(0, 80);
        float width = ImGui::CalcTextSize(status.c_str()).x + 16.0f;
        if (!importText.empty())
            width += ImGui::CalcTextSize(importText.c_str()).x + 16.0f;
        if (!errorText.empty())
            width += ImGui::CalcTextSize(errorText.c_str()).x + 16.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetWindowWidth() - width - 8.0f));
        ImGui::AlignTextToFramePadding();
        if (!errorText.empty())
        {
            ImGui::TextColored(ErrorColor, "%s", errorText.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\nClick to open the console and dismiss.", m_SceneFileError.c_str());
            if (ImGui::IsItemClicked())
            {
                m_SceneFileError.clear();
                m_Panels.Open(PanelID::Console);
            }
            ImGui::SameLine(0, 16.0f);
        }
        if (!importText.empty())
        {
            ImGui::TextColored(AccentColor, "%s", importText.c_str());
            ImGui::SameLine(0, 16.0f);
        }
        ImGui::TextDisabled("%s", status.c_str());
        if (ImGui::IsItemHovered() && !m_ScenePath.empty())
            ImGui::SetTooltip("%s", m_ScenePath.u8string().c_str());

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ------------------------------------------------------------------ stats

    void EditorLayer::DrawStats()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
            const auto viewport = m_ViewportRenderer->GetViewportSize();
            ImGui::Text("Viewport: %u x %u", viewport[0], viewport[1]);
            ImGui::Text("UI: %d vertices, %d indices", io.MetricsRenderVertices, io.MetricsRenderIndices);
        }
        if (ImGui::CollapsingHeader("Renderer 2D", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Draw calls: %d", Renderer2D::GetStats().DrawCalls);
            ImGui::Text("Quads: %d", Renderer2D::GetStats().QuadCount);
            ImGui::Text("Vertices: %d", Renderer2D::GetStats().GetTotalVertexCount());
        }
        if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Resident CPU assets: %zu", m_Assets ? m_Assets->GetResidentCount() : size_t(0));
            if (m_AssetImports)
                if (auto snapshot = m_AssetImports->GetSnapshot();
                    std::holds_alternative<EditorAssetImportSnapshot>(snapshot))
                {
                    const char* states[] = {"Idle", "Building", "Succeeded", "Failed", "Cancelled"};
                    ImGui::Text("Last import: %s",
                                states[static_cast<int>(std::get<EditorAssetImportSnapshot>(snapshot).State)]);
                }
        }
        if (m_ActiveRenderScene && ImGui::CollapsingHeader("Render Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto  resource = m_ActiveRenderScene->GetRenderResource();
            auto& state    = resource.Piplinestate;
            ImGui::Checkbox("Grid", &state.EnableGridShader);
            ImGui::Checkbox("Outline", &state.EnableOutline);
            ImGui::Checkbox("Stencil", &state.EnableStencil);
            ImGui::Checkbox("Debug normals", &state.EnableDebugNormal);
            ImGui::Checkbox("EnableToneShader", &state.EnableToneShader);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Not used by the current renderer: use a ToneCharacter material instead.");
            ImGui::Checkbox("EnableDefaultShader", &state.EnableDefaultShader);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Not used by the current renderer: models are always drawn.");
            m_ActiveRenderScene->SetRenderResource(resource);
        }
    }
} // namespace Kans
