#include "PanelManager.h"
#include <cstring>
#include <imgui_internal.h>

namespace Kans
{
    void PanelManager::Register(PanelSpec spec, std::function<void()> draw)
    {
        Panel panel;
        panel.Open = spec.DefaultOpen || !spec.Closable;
        if (const auto saved = m_SavedOpen.find(spec.ID); saved != m_SavedOpen.end() && spec.Closable)
            panel.Open = saved->second;
        panel.Spec = std::move(spec);
        panel.Draw = std::move(draw);
        m_Panels.push_back(std::move(panel));
    }

    PanelManager::Panel* PanelManager::Find(const std::string& id)
    {
        for (auto& panel : m_Panels)
            if (panel.Spec.ID == id)
                return &panel;
        return nullptr;
    }

    const PanelManager::Panel* PanelManager::Find(const std::string& id) const
    {
        return const_cast<PanelManager*>(this)->Find(id);
    }

    void PanelManager::Open(const std::string& id, bool focus)
    {
        if (auto* panel = Find(id))
        {
            panel->Open         = true;
            panel->FocusRequest = focus;
        }
    }

    void PanelManager::Close(const std::string& id)
    {
        if (auto* panel = Find(id); panel && panel->Spec.Closable)
            panel->Open = false;
    }

    void PanelManager::Toggle(const std::string& id)
    {
        if (IsOpen(id))
            Close(id);
        else
            Open(id);
    }

    bool PanelManager::IsOpen(const std::string& id) const
    {
        const auto* panel = Find(id);
        return panel && panel->Open;
    }

    std::string PanelManager::WindowName(const Panel& panel) const
    {
        // "###id" keeps the window identity (and its dock slot) independent of the title.
        std::string name = panel.Spec.Icon;
        if (!name.empty())
            name += "  ";
        return name + panel.Spec.Title + "###" + panel.Spec.ID;
    }

    void PanelManager::PrepareDockSpace(ImGuiID dockspace, ImVec2 size)
    {
        if (ImGui::DockBuilderGetNode(dockspace) && !m_ResetLayout)
            return;
        m_ResetLayout = false;
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, size);
        ImGuiID center = dockspace, left = 0, right = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, &right, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, &bottom, &center);
        for (const auto& panel : m_Panels)
        {
            const ImGuiID node = panel.Spec.Dock == PanelDock::Left   ? left :
                                 panel.Spec.Dock == PanelDock::Right  ? right :
                                 panel.Spec.Dock == PanelDock::Bottom ? bottom :
                                                                        center;
            ImGui::DockBuilderDockWindow(WindowName(panel).c_str(), node);
        }
        ImGui::DockBuilderFinish(dockspace);
    }

    void PanelManager::Draw()
    {
        // A dock node shows the tab of the window that appears last, so draw in
        // reverse: the first registered panel of each area is the visible tab.
        for (auto it = m_Panels.rbegin(); it != m_Panels.rend(); ++it)
        {
            auto& panel = *it;
            if (!panel.Open)
                continue;
            if (panel.FocusRequest)
            {
                ImGui::SetNextWindowFocus();
                panel.FocusRequest = false;
            }
            if (panel.Spec.NoPadding)
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            bool       open    = true;
            const bool visible = ImGui::Begin(
                WindowName(panel).c_str(), panel.Spec.Closable ? &open : nullptr, panel.Spec.Flags);
            if (panel.Spec.NoPadding)
                ImGui::PopStyleVar();
            if (visible && panel.Draw)
                panel.Draw();
            ImGui::End();
            if (!open)
                panel.Open = false;
        }
    }

    void PanelManager::DrawWindowMenu()
    {
        const std::pair<PanelCategory, const char*> categories[] = {
            {PanelCategory::Scene, "Scene"}, {PanelCategory::Assets, "Assets"}, {PanelCategory::Debug, "Debug"}};
        bool first = true;
        for (const auto& [category, name] : categories)
        {
            if (!first)
                ImGui::Separator();
            first = false;
            ImGui::TextDisabled("%s", name);
            for (auto& panel : m_Panels)
            {
                if (panel.Spec.Category != category)
                    continue;
                const std::string label = std::string(panel.Spec.Icon) + "  " + panel.Spec.Title;
                if (ImGui::MenuItem(label.c_str(), panel.Spec.Shortcut, panel.Open, panel.Spec.Closable))
                    Toggle(panel.Spec.ID);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            ResetLayout();
    }

    void PanelManager::InstallSettingsHandler()
    {
        ImGuiSettingsHandler handler;
        handler.TypeName   = "KansPanels";
        handler.TypeHash   = ImHashStr("KansPanels");
        handler.UserData   = this;
        handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
            return std::strcmp(name, "Open") == 0 ? reinterpret_cast<void*>(1) : nullptr;
        };
        handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, void*, const char* line) {
            auto*       self  = static_cast<PanelManager*>(handler->UserData);
            const char* split = std::strchr(line, '=');
            if (!split)
                return;
            const std::string id(line, split);
            const bool        open = std::strcmp(split + 1, "1") == 0;
            self->m_SavedOpen[id]  = open;
            if (auto* panel = self->Find(id); panel && panel->Spec.Closable)
                panel->Open = open;
        };
        handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer) {
            auto* self = static_cast<PanelManager*>(handler->UserData);
            buffer->appendf("[%s][Open]\n", handler->TypeName);
            for (const auto& panel : self->m_Panels)
                buffer->appendf("%s=%d\n", panel.Spec.ID.c_str(), panel.Open ? 1 : 0);
            buffer->append("\n");
        };
        ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);
        m_HandlerInstalled = true;
    }

    void PanelManager::UninstallSettingsHandler()
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (!m_HandlerInstalled || !context)
            return;
        m_HandlerInstalled = false;
        if (context->SettingsLoaded && ImGui::GetIO().IniFilename)
            ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
        auto& handlers = context->SettingsHandlers;
        for (int i = 0; i < handlers.Size; ++i)
            if (handlers[i].UserData == this)
            {
                handlers.erase(handlers.Data + i);
                break;
            }
    }
} // namespace Kans
