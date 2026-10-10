#include "ViewportPanel.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include "ImGuizmo.h"
#include "Kans3D/Core/Math/Math.h"
#include "Kans3D/Input/Input.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/ProductModelComponent.h"
#include "UI/EditorIcons.h"
#include "UI/Widgets.h"

namespace Kans
{
    using Widgets::OverlayGroup;
    using Widgets::ToolButton;
    using Widgets::ToolSeparator;

    void ViewportPanel::SetContext(Ref<Scene> scene, Ref<RenderScene> renderScene, Ref<SceneRenderer> renderer)
    {
        m_Scene       = std::move(scene);
        m_RenderScene = std::move(renderScene);
        m_Renderer    = std::move(renderer);
        m_Size        = {0, 0}; // Re-applies the viewport size to the new scene.
    }

    void ViewportPanel::Draw()
    {
        m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        m_Hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        if (!m_Scene || !m_Renderer || !m_RenderScene)
        {
            ImGui::TextDisabled("No scene.");
            return;
        }
        const ImVec2 size = ImGui::GetContentRegionAvail();
        m_Min             = ImGui::GetCursorScreenPos();
        m_Max             = {m_Min.x + size.x, m_Min.y + size.y};
        // Resize and render in this frame, so the image always matches the panel.
        Resize(size);
        Render();

        const auto output = static_cast<uintptr_t>(m_Renderer->GetOutput(0));
        ImGui::Image(reinterpret_cast<ImTextureID>(output), size, ImVec2(0, 1), ImVec2(1, 0));
        m_ImageHovered = ImGui::IsItemHovered();
        AcceptDrop();
        DrawGizmo();
        DrawToolbar();
        // After the overlays, so clicks on their widgets do not navigate.
        HandleNavigation();
        HandleShortcuts();
    }

    void ViewportPanel::Resize(ImVec2 size)
    {
        const glm::uvec2 pixels(std::max(1.0f, size.x), std::max(1.0f, size.y));
        if (pixels == m_Size)
            return;
        m_Size = pixels;
        m_Renderer->SetViewportSize(pixels.x, pixels.y);
        m_Camera.SetViewportSize(pixels.x, pixels.y);
        m_RenderScene->OnViewportResize(pixels.x, pixels.y);
        m_Scene->OnViewportResize(pixels.x, pixels.y);
    }

    void ViewportPanel::Render()
    {
        const TimeStep step(ImGui::GetIO().DeltaTime);
        // Without a primary camera, play mode still shows the editor view.
        if (m_Playing && !m_UseEditorCameraInPlay && m_Scene->GetCameraEntity())
            m_RenderScene->OnRenderRuntime(m_Renderer, step);
        else
            m_RenderScene->OnRenderEditor(m_Renderer, step, m_Camera);
    }

    void ViewportPanel::AcceptDrop()
    {
        if (!ImGui::BeginDragDropTarget())
            return;
        if (const auto* data = ImGui::AcceptDragDropPayload("asset_payload"))
            if (m_FileDrop)
                m_FileDrop(std::filesystem::u8path(std::string(static_cast<const char*>(data->Data), size_t(data->DataSize))));
        ImGui::EndDragDropTarget();
    }

    // ---------------------------------------------------------------------- navigation

    void ViewportPanel::HandleNavigation()
    {
        const ImGuiIO& io        = ImGui::GetIO();
        const bool     overImage = m_ImageHovered && !ImGui::IsAnyItemHovered();
        if (m_Navigation == Navigation::None && overImage)
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                m_Navigation = Navigation::Look;
            else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                m_Navigation = Navigation::Pan;
            else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m_Navigation = Navigation::Orbit;
            if (m_Navigation == Navigation::Look)
                Input::SetCursorMode(CursorMode::Disable);
            if (m_Navigation != Navigation::None)
                ImGui::SetWindowFocus();
        }

        switch (m_Navigation)
        {
            case Navigation::Look:
            {
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
                    break;
                m_Camera.Look({io.MouseDelta.x, io.MouseDelta.y});
                const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
                    return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
                };
                const glm::vec3 direction(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S));
                if (direction != glm::vec3(0.0f))
                    m_Camera.Fly(direction * (io.KeyShift ? 3.0f : 1.0f), io.DeltaTime);
                if (io.MouseWheel != 0.0f)
                    m_Camera.SetFlySpeed(m_Camera.GetFlySpeed() * std::pow(1.2f, io.MouseWheel));
                return;
            }
            case Navigation::Orbit:
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    m_Camera.Orbit({io.MouseDelta.x, io.MouseDelta.y});
                    return;
                }
                break;
            case Navigation::Pan:
                if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                {
                    m_Camera.Pan({io.MouseDelta.x, io.MouseDelta.y});
                    return;
                }
                break;
            case Navigation::None:
                if (overImage && io.MouseWheel != 0.0f)
                    m_Camera.Dolly(io.MouseWheel);
                return;
        }
        // The navigating button was released.
        if (m_Navigation == Navigation::Look)
            Input::SetCursorMode(CursorMode::Normal);
        m_Navigation = Navigation::None;
    }

    void ViewportPanel::HandleShortcuts()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if ((!m_Hovered && !m_Focused) || io.WantTextInput || io.KeyCtrl || m_Navigation != Navigation::None)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
            m_GizmoType = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            m_GizmoType = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            m_GizmoType = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            m_GizmoType = ImGuizmo::SCALE;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            FocusSelection();
    }

    void ViewportPanel::FocusSelection()
    {
        Entity selected = m_Hierarchy.GetSelected();
        if (!selected || !m_Scene)
            return;
        // Frame the selected entity with its children.
        Bounds                      bounds;
        std::function<void(Entity)> visit = [&](Entity entity) {
            const glm::mat4 world = m_Scene->GetWorldTransform(entity);
            Bounds          model;
            if (entity.HasComponent<ProductModelComponent>() && m_Renderer->GetAssetCache())
                model = m_Renderer->GetAssetCache()->GetModelBounds(entity.GetComponent<ProductModelComponent>().Model);
            if (model.IsValid())
                bounds.Expand(model.Transformed(world));
            else
                bounds.Expand(glm::vec3(world[3]));
            for (Entity child : m_Scene->GetChildren(entity))
                visit(child);
        };
        visit(selected);
        m_Camera.Focus(bounds.Center(), std::max(bounds.Radius(), 0.5f));
    }

    // ---------------------------------------------------------------------- gizmo and toolbar

    void ViewportPanel::DrawGizmo()
    {
        Entity selected = m_Hierarchy.GetSelected();
        if (!selected || m_GizmoType < 0 || (m_Playing && !m_UseEditorCameraInPlay && m_Scene->GetCameraEntity()))
            return;
        ImGuizmo::SetOrthographic(m_Camera.IsOrthographic());
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_Min.x, m_Min.y, m_Max.x - m_Min.x, m_Max.y - m_Min.y);

        // The gizmo works on the world transform; results go back to the local one.
        glm::mat4   transform = m_Scene->GetWorldTransform(selected);
        const bool  snap      = m_GizmoSnap || ImGui::GetIO().KeyCtrl;
        const float step      = m_GizmoType == ImGuizmo::ROTATE ? m_SnapRotate :
                                m_GizmoType == ImGuizmo::SCALE  ? m_SnapScale :
                                                                  m_SnapMove;
        const float snapValues[3] = {step, step, step};
        ImGuizmo::Manipulate(glm::value_ptr(m_Camera.GetViewMatrix()),
                             glm::value_ptr(m_Camera.GetProjectionMatrix()),
                             static_cast<ImGuizmo::OPERATION>(m_GizmoType),
                             static_cast<ImGuizmo::MODE>(m_GizmoMode),
                             glm::value_ptr(transform),
                             nullptr,
                             snap ? snapValues : nullptr);
        if (!ImGuizmo::IsUsing())
            return;

        const Entity    parent = m_Scene->GetParent(selected);
        const glm::mat4 local  = parent ? glm::inverse(m_Scene->GetWorldTransform(parent)) * transform : transform;
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
                // Map the stored rotation to [-pi, pi], the range the decomposition returns.
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
                tc.Scale = scale;
                break;
        }
    }

    void ViewportPanel::DrawToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 4.0f));

        // Left: transform tools, space and snapping.
        OverlayGroup({m_Min.x + 10.0f, m_Min.y + 8.0f}, [&] {
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
            float*      snapStep = m_GizmoType == ImGuizmo::ROTATE ? &m_SnapRotate :
                                   m_GizmoType == ImGuizmo::SCALE  ? &m_SnapScale :
                                                                     &m_SnapMove;
            const char* format   = m_GizmoType == ImGuizmo::ROTATE ? "%.0f deg" : "%.2f";
            ImGui::SetNextItemWidth(70.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::DragFloat("##snap", snapStep, 0.05f, 0.01f, 360.0f, format);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Snap step for the current tool");
        });

        // Right: camera, view options.
        OverlayGroup({m_Max.x - 268.0f, m_Min.y + 8.0f}, [&] {
            if (ToolButton(m_Camera.IsOrthographic() ? ICON_FA_VECTOR_SQUARE : ICON_FA_VIDEO, "Camera view"))
                ImGui::OpenPopup("##CameraView");
            if (ImGui::BeginPopup("##CameraView"))
            {
                const std::pair<EditorCamera::View, const char*> views[] = {
                    {EditorCamera::View::Perspective, "Perspective"},
                    {EditorCamera::View::Top, "Top"},
                    {EditorCamera::View::Bottom, "Bottom"},
                    {EditorCamera::View::Front, "Front"},
                    {EditorCamera::View::Back, "Back"},
                    {EditorCamera::View::Right, "Right"},
                    {EditorCamera::View::Left, "Left"},
                };
                for (const auto& [view, name] : views)
                    if (ImGui::MenuItem(name))
                        m_Camera.SetView(view);
                ImGui::Separator();
                bool orthographic = m_Camera.IsOrthographic();
                if (ImGui::MenuItem("Orthographic", nullptr, &orthographic))
                    m_Camera.SetOrthographic(orthographic);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ToolButton(ICON_FA_CROSSHAIRS, "Frame the selection (F)", false, bool(m_Hierarchy.GetSelected())))
                FocusSelection();
            ImGui::SameLine();
            float speed = m_Camera.GetFlySpeed();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::AlignTextToFramePadding();
            if (ImGui::SliderFloat("##speed",
                                   &speed,
                                   EditorCamera::MinFlySpeed,
                                   EditorCamera::MaxFlySpeed,
                                   "%.1f m/s",
                                   ImGuiSliderFlags_Logarithmic))
                m_Camera.SetFlySpeed(speed);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fly speed (right mouse + WASD/QE, Shift for faster); the wheel adjusts it while flying");
            ImGui::SameLine();
            if (ToolButton(ICON_FA_EYE, "View options"))
                ImGui::OpenPopup("##ViewOptions");
            if (ImGui::BeginPopup("##ViewOptions"))
            {
                auto  resource = m_RenderScene->GetRenderResource();
                auto& state    = resource.Piplinestate;
                const auto label = [](const char* icon, const char* text) { return std::string(icon) + "  " + text; };
                ImGui::Checkbox(label(ICON_FA_BORDER_ALL, "Grid").c_str(), &state.EnableGridShader);
                ImGui::Checkbox(label(ICON_FA_DRAW_POLYGON, "Outline (all models)").c_str(), &state.EnableOutline);
                ImGui::Checkbox(label(ICON_FA_VECTOR_SQUARE, "Stencil (all models)").c_str(), &state.EnableStencil);
                ImGui::Checkbox(label(ICON_FA_CROSSHAIRS, "Debug normals").c_str(), &state.EnableDebugNormal);
                m_RenderScene->SetRenderResource(resource);
                if (m_Playing)
                {
                    ImGui::Separator();
                    ImGui::Checkbox("Editor camera while playing", &m_UseEditorCameraInPlay);
                }
                ImGui::EndPopup();
            }
        });
        ImGui::PopStyleVar();
    }
} // namespace Kans
