#pragma once
#include <array>
#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
    // Scene entity tree: parent/child nesting, drag and drop to reparent or
    // reorder, search, inline rename, create/duplicate/delete. Owns the editor's
    // entity selection.
    class HierarchyPanel
    {
    public:
        using FileDrop = std::function<void(const std::filesystem::path& file, Entity parent)>;

        void SetScene(Ref<Scene> scene);
        // The selected entity, or a null Entity (also after it was deleted).
        Entity GetSelected() const;
        // Selects and reveals the entity in the tree.
        void SetSelected(Entity entity);
        // A file dropped from the content browser; `parent` is the entity it was
        // dropped on (null for the empty area).
        void SetFileDropCallback(FileDrop callback) { m_FileDrop = std::move(callback); }

        void Draw();

    private:
        enum class DropPlace
        {
            Before,
            Into,
            After
        };

        void DrawToolbar();
        void DrawEntity(Entity entity);
        void DrawCreateItems(Entity parent);
        void DrawEntityMenu(Entity entity);
        // Drop target over the last item: reparent or reorder entities, or place files.
        void AcceptDrop(Entity target);
        void HandleKeyboard();
        // Marks entities whose name or descendants match the search.
        bool UpdateMatches(Entity entity);

        Entity Create(const char* name, Entity parent);
        void   Delete(Entity entity);
        void   Duplicate(Entity entity);
        void   StartRename(Entity entity);
        void   CommitRename();
        void   Move(Entity entity, Entity target, DropPlace place);

        Ref<Scene> m_Scene;
        UUID       m_Selected = 0;
        UUID       m_Reveal   = 0; // Expand the tree down to this entity once.
        FileDrop   m_FileDrop;

        UUID                  m_Renaming = 0;
        std::array<char, 256> m_RenameBuffer {};
        bool                  m_RenameFocus = false;

        std::array<char, 128>              m_Search {};
        std::unordered_map<uint64_t, bool> m_Matches; // Subtree matches the search.
        std::vector<UUID>                  m_Order;   // Entities as drawn, for arrow keys.
        UUID                               m_MenuEntity     = 0;
        bool                               m_OpenEntityMenu = false;
        bool                               m_OpenCreateMenu = false;
    };
} // namespace Kans
