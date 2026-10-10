#include "EditorLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "ImGuizmo.h"
#include "UI/EditorIcons.h"

// Editor window layout: menu bar, main toolbar, dock space with the panels, and
// the viewport with its overlay toolbar.

namespace Kans
{
    namespace
    {
        constexpr ImVec4 ErrorColor {0.95f, 0.38f, 0.35f, 1.0f};
        constexpr ImVec4 AccentColor {0.26f, 0.59f, 0.98f, 1.0f};

        // Icon button with a tooltip; `active` highlights toggles that are on.
        bool ToolButton(const char* icon, const char* tooltip, bool active = false, bool enabled = true)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.26f, 0.59f, 0.98f, 0.45f) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.40f, 0.40f, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.59f, 0.98f, 0.65f));
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(icon, {28.0f, 26.0f});
            ImGui::EndDisabled();
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tooltip);
            return clicked;
        }

        void ToolSeparator()
        {
            ImGui::SameLine(0, 8.0f);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                {at.x, at.y + 4.0f}, {at.x, at.y + 22.0f}, ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::Dummy({1.0f, 26.0f});
            ImGui::SameLine(0, 8.0f);
        }

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
        m_Panels.Register(viewport, [this] { DrawViewport(); });
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

        // The viewport panel sets these when it is drawn.
        m_ViewportFocused = false;
        m_ViewportHovered = false;
        m_Panels.Draw();
        Application::Get().GetImGuiLayer()->BlockEvents(m_ViewportFocused || m_ViewportHovered);
        TrackSceneEdits();
    }

    void EditorLayer::TrackSceneEdits()
    {
        if (!m_ActiveScene)
            return;
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
        if (!m_ActiveScene)
            return;
        // Pending changes become a step first, so they can be undone too.
        m_History.Record(*m_ActiveScene);
        m_EditPending = false;
        for (int i = 0; i < steps && m_History.Undo(*m_ActiveScene); ++i)
            ;
    }

    void EditorLayer::RedoScene(int steps)
    {
        if (!m_ActiveScene)
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
            return;
        }
        // Gizmo tools while working in the viewport (not while flying the camera).
        if ((m_ViewportHovered || m_ViewportFocused) && !m_CameraNavigating)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
                m_GizmoType = -1;
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                m_GizmoType = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false))
                m_GizmoType = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                m_GizmoType = ImGuizmo::SCALE;
        }
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
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FLOPPY_DISK, "Save Scene").c_str(), "Ctrl+S"))
                SaveCurrentScene();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_FLOPPY_DISK, "Save Scene As...").c_str(), "Ctrl+Shift+S"))
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
                                !undo.empty()))
                UndoScene();
            if (ImGui::MenuItem(MenuLabel(ICON_FA_REDO, redo.empty() ? "Redo" : ("Redo " + redo.front()).c_str()).c_str(),
                                "Ctrl+Y",
                                false,
                                !redo.empty()))
                RedoScene();
            ImGui::Separator();
            // Jump several steps at once.
            if (ImGui::BeginMenu(MenuLabel(ICON_FA_CLOCK_ROTATE_LEFT, "History").c_str(), !undo.empty() || !redo.empty()))
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
        if (ToolButton(ICON_FA_FLOPPY_DISK, "Save scene (Ctrl+S)"))
            SaveCurrentScene();
        ToolSeparator();
        {
            const auto undo = m_History.UndoLabels();
            const auto redo = m_History.RedoLabels();
            const auto undoTip = undo.empty() ? std::string("Nothing to undo") : "Undo " + undo.front() + " (Ctrl+Z)";
            const auto redoTip = redo.empty() ? std::string("Nothing to redo") : "Redo " + redo.front() + " (Ctrl+Y)";
            if (ToolButton(ICON_FA_UNDO, undoTip.c_str(), false, !undo.empty()))
                UndoScene();
            ImGui::SameLine();
            if (ToolButton(ICON_FA_REDO, redoTip.c_str(), false, !redo.empty()))
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

        // Right side: scene name, import status and the last error.
        std::string status = ICON_FA_CUBE "  " + (m_ScenePath.empty() ? std::string("Untitled scene") :
                                                                         m_ScenePath.filename().u8string());
        if (m_History.IsModified())
            status += " *";
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

    // ------------------------------------------------------------------ viewport

    void EditorLayer::DrawViewport()
    {
        m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_ViewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        // Use the wheel and hover state from the same ImGui frame.
        if (m_ViewportHovered && ImGui::GetIO().MouseWheel != 0.0f)
        {
            MouseScrolledEvent scrollEvent(ImGui::GetIO().MouseWheelH, ImGui::GetIO().MouseWheel);
            m_EditorCamera.onEvent(scrollEvent);
        }

        const ImVec2 size = ImGui::GetContentRegionAvail();
        m_ViewportSize    = {size.x, size.y};
        m_ViewportMin     = ImGui::GetCursorScreenPos();
        m_ViewportMax     = {m_ViewportMin.x + size.x, m_ViewportMin.y + size.y};
        const auto output = static_cast<uintptr_t>(m_ViewportRenderer->GetOutput(0));
        ImGui::Image(reinterpret_cast<ImTextureID>(output), size, ImVec2(0, 1), ImVec2(1, 0));

        // Drop scenes to open them and models to place them.
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* data = ImGui::AcceptDragDropPayload("asset_payload"))
            {
                const std::filesystem::path file(std::string(static_cast<const char*>(data->Data), data->DataSize));
                if (file.extension() == ".kans")
                    Record(OpenScene(file));
                else if (file.has_extension())
                    PlaceAsset(file);
            }
            ImGui::EndDragDropTarget();
        }

        DrawGizmo();
        DrawViewportToolbar();
    }

    void EditorLayer::DrawViewportToolbar()
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const auto  panel    = [&](ImVec2 at, auto&& contents) {
            // Translucent rounded background behind a group of widgets.
            ImGui::SetCursorScreenPos(at);
            drawList->ChannelsSplit(2);
            drawList->ChannelsSetCurrent(1);
            ImGui::BeginGroup();
            contents();
            ImGui::EndGroup();
            drawList->ChannelsSetCurrent(0);
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            drawList->AddRectFilled(
                {min.x - 4.0f, min.y - 3.0f}, {max.x + 4.0f, max.y + 3.0f}, IM_COL32(18, 18, 18, 190), 6.0f);
            drawList->ChannelsMerge();
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));

        // Left: transform tools, space and snapping.
        panel({m_ViewportMin.x + 10.0f, m_ViewportMin.y + 8.0f}, [&] {
            const std::pair<int, std::pair<const char*, const char*>> tools[] = {
                {-1, {ICON_FA_POINTER, "Select (Q)"}},
                {ImGuizmo::TRANSLATE, {ICON_FA_ARROWS_MOVE, "Move (W)"}},
                {ImGuizmo::ROTATE, {ICON_FA_ROTATE, "Rotate (E)"}},
                {ImGuizmo::SCALE, {ICON_FA_SCALE, "Scale (R)"}},
            };
            bool first = true;
            for (const auto& [type, info] : tools)
            {
                if (!first)
                    ImGui::SameLine();
                first = false;
                if (ToolButton(info.first, info.second, m_GizmoType == type))
                    m_GizmoType = type;
            }
            ToolSeparator();
            if (ToolButton(m_GizmoMode == ImGuizmo::LOCAL ? ICON_FA_CUBE : ICON_FA_GLOBE,
                           m_GizmoMode == ImGuizmo::LOCAL ? "Local space (click for world)" :
                                                            "World space (click for local)"))
                m_GizmoMode = m_GizmoMode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
            ImGui::SameLine();
            if (ToolButton(ICON_FA_MAGNET, "Snap (or hold Ctrl while dragging)", m_GizmoSnap))
                m_GizmoSnap = !m_GizmoSnap;
            ImGui::SameLine();
            float*      step   = m_GizmoType == ImGuizmo::ROTATE ? &m_SnapRotate :
                                 m_GizmoType == ImGuizmo::SCALE  ? &m_SnapScale :
                                                                   &m_SnapMove;
            const char* format = m_GizmoType == ImGuizmo::ROTATE ? "%.0f deg" : "%.2f";
            ImGui::SetNextItemWidth(70.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::DragFloat("##snap", step, 0.05f, 0.01f, 360.0f, format);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Snap step for the current tool");
        });

        // Right: camera speed and view options.
        panel({m_ViewportMax.x - 236.0f, m_ViewportMin.y + 8.0f}, [&] {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(ICON_FA_VIDEO);
            ImGui::SameLine(0, 6.0f);
            float speed = m_EditorCamera.getCameraSpeed();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::SliderFloat("##speed",
                                   &speed,
                                   EditorCamera::getMinCameraSpeed(),
                                   EditorCamera::getMaxCameraSpeed() * 0.1f,
                                   "speed %.4f",
                                   ImGuiSliderFlags_Logarithmic))
                m_EditorCamera.setCameraSpeed(speed);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Camera fly speed (right mouse + WASD); the wheel also adjusts it");
            ImGui::SameLine();
            if (ToolButton(ICON_FA_EYE, "View options"))
                ImGui::OpenPopup("ViewOptions");
            if (ImGui::BeginPopup("ViewOptions"))
            {
                auto resource = m_ActiveRenderScene->GetRenderResource();
                auto& state   = resource.Piplinestate;
                ImGui::Checkbox(MenuLabel(ICON_FA_BORDER_ALL, "Grid").c_str(), &state.EnableGridShader);
                ImGui::Checkbox(MenuLabel(ICON_FA_DRAW_POLYGON, "Outline").c_str(), &state.EnableOutline);
                ImGui::Checkbox(MenuLabel(ICON_FA_VECTOR_SQUARE, "Stencil").c_str(), &state.EnableStencil);
                ImGui::Checkbox(MenuLabel(ICON_FA_CROSSHAIRS, "Debug normals").c_str(), &state.EnableDebugNormal);
                m_ActiveRenderScene->SetRenderResource(resource);
                ImGui::EndPopup();
            }
        });
        ImGui::PopStyleVar();
    }

    void EditorLayer::DrawGizmo()
    {
        Entity selected = m_HierarchyPanel.GetSelected();
        if (!selected || m_GizmoType < 0)
            return;
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(
            m_ViewportMin.x, m_ViewportMin.y, m_ViewportMax.x - m_ViewportMin.x, m_ViewportMax.y - m_ViewportMin.y);

        const glm::mat4 cameraProjection = m_EditorCamera.GetProjectionMatrix();
        const glm::mat4 cameraView       = m_EditorCamera.getViewMatrix();
        // The gizmo works on the world transform; results go back to the local one.
        glm::mat4       transform        = m_ActiveScene->GetWorldTransform(selected);
        const bool      snap             = m_GizmoSnap || ImGui::GetIO().KeyCtrl;
        const float     step             = m_GizmoType == ImGuizmo::ROTATE ? m_SnapRotate :
                                           m_GizmoType == ImGuizmo::SCALE  ? m_SnapScale :
                                                                             m_SnapMove;
        const float snapValues[3] = {step, step, step};
        ImGuizmo::Manipulate(glm::value_ptr(cameraView),
                             glm::value_ptr(cameraProjection),
                             static_cast<ImGuizmo::OPERATION>(m_GizmoType),
                             static_cast<ImGuizmo::MODE>(m_GizmoMode),
                             glm::value_ptr(transform),
                             nullptr,
                             snap ? snapValues : nullptr);
        if (!ImGuizmo::IsUsing())
            return;

        const Entity    parent = m_ActiveScene->GetParent(selected);
        const glm::mat4 local  = parent ? glm::inverse(m_ActiveScene->GetWorldTransform(parent)) * transform : transform;
        glm::vec3       translation, rotation, scale;
        Math::DecomposeTransform(local, translation, rotation, scale);
        auto& tc = selected.GetComponent<TransformComponent>();
        switch (m_GizmoType)
        {
            case ImGuizmo::TRANSLATE:
                tc.Position = translation;
                break;
            case ImGuizmo::ROTATE:
            {
                // Map the stored rotation to [-pi, pi], the range ImGuizmo returns.
                glm::vec3 original = tc.Rotation;
                for (int i = 0; i < 3; ++i)
                    original[i] = fmodf(original[i] + glm::pi<float>(), glm::two_pi<float>()) - glm::pi<float>();
                glm::vec3 delta = rotation - original;
                // Avoid drift from numeric precision.
                for (int i = 0; i < 3; ++i)
                    if (fabs(delta[i]) < 0.001f)
                        delta[i] = 0.0f;
                tc.Rotation += delta;
                break;
            }
            case ImGuizmo::SCALE:
                if (scale != glm::vec3(1.0f))
                    tc.Scale = scale;
                break;
        }
    }

    // ------------------------------------------------------------------ stats

    void EditorLayer::DrawStats()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("%.1f FPS  (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
            ImGui::Text("Viewport: %.0f x %.0f", m_ViewportSize.x, m_ViewportSize.y);
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
