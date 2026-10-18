#pragma once
#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetType.h"
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
    class AssetManager;
    class Scene;

    // Services a component editor draws with; implemented by the Properties panel.
    class ComponentUI
    {
    public:
        virtual ~ComponentUI() = default;

        virtual Scene*        GetScene()  = 0;
        virtual AssetManager* GetAssets() = 0;
        virtual void          Select(Entity entity) = 0;
        // Picks a published asset of `type` (also by dropping its source file); true when `id` changed.
        virtual bool AssetPicker(const char* label, AssetType type, AssetID& id) = 0;
        // Picks a replacement material; true with an invalid ID to restore the imported one.
        virtual bool        MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen) = 0;
        virtual std::string MaterialName(AssetID material) = 0;
    };

    // Everything the editor knows about one component type: how it is named, shown,
    // added, reset, copied and drawn. The Properties panel, its Add Component menu,
    // the Hierarchy's icons and its Create menu are all generated from these entries,
    // so a new component is registered in one place.
    struct ComponentEntry
    {
        std::type_index Type = typeid(void);
        std::string     Name;
        const char*     Icon = nullptr;
        std::string     Category;
        // Lower ranks decide the Hierarchy icon when an entity has several components.
        int IconRank = 1000;

        std::function<bool(Entity)> Has;
        // Add Component: adds the component with its editor defaults.
        std::function<void(Entity)> Add;
        // Empty when the component can always be added; otherwise whether it can be to this entity.
        std::function<bool(Entity)> CanAdd;
        // Empty when the component cannot be removed; otherwise whether it can be for this entity.
        std::function<bool(Entity)> CanRemove;
        std::function<void(Entity)> Remove;
        // Empty when Reset is not offered.
        std::function<void(Entity)> Reset;
        std::function<void(Entity, ComponentUI&)> Draw;
        // Copy/Paste values; empty when not offered.
        std::function<std::any(Entity)>              Copy;
        std::function<void(Entity, const std::any&)> Paste;
        // Prefab parts: whether this component carries an override, and how to revert it.
        std::function<bool(Entity, Scene&)> Overridden;
        std::function<void(Entity, Scene&)> Revert;
        // Hierarchy Create menu: the new entity's name (empty: not offered) and its setup.
        std::string                 CreateName;
        std::function<void(Entity)> Create;
        int                         CreateOrder = 0;
        // A Create submenu of several variants (e.g. "3D Object" with one item per shape),
        // used instead of CreateName/Create when not empty.
        struct CreateItem
        {
            std::string                 Name;
            const char*                 Icon = nullptr;
            std::function<void(Entity)> Create;
        };
        std::string             CreateMenu;
        std::vector<CreateItem> CreateItems;
    };

    class ComponentRegistry
    {
    public:
        // The editor's components, in the order the Properties panel shows them.
        static const ComponentRegistry& Get();

        const std::vector<ComponentEntry>& Entries() const { return m_Entries; }
        const ComponentEntry*              Find(std::type_index type) const;
        // The most telling component's icon (see IconRank); a plain entity icon otherwise.
        const char* IconFor(Entity entity) const;

        // Registers T with the default operations (add, remove, reset to T{}, copy and
        // paste through PatchComponent); the caller overrides what differs.
        template<typename T>
        ComponentEntry& Register(std::string name, const char* icon, std::string category)
        {
            ComponentEntry entry;
            entry.Type      = typeid(T);
            entry.Name      = std::move(name);
            entry.Icon      = icon;
            entry.Category  = std::move(category);
            entry.Has       = [](Entity e) { return e.HasComponent<T>(); };
            entry.Add       = [](Entity e) { e.AddComponent<T>(); };
            entry.CanRemove = [](Entity) { return true; };
            entry.Remove    = [](Entity e) { e.RemoveComponent<T>(); };
            entry.Reset     = [](Entity e) { e.PatchComponent<T>([](T& c) { c = T(); }); };
            entry.Copy      = [](Entity e) { return std::any(e.GetComponent<T>()); };
            entry.Paste     = [](Entity e, const std::any& value) {
                if (const auto* copied = std::any_cast<T>(&value))
                    e.PatchComponent<T>([&](T& c) { c = *copied; });
            };
            m_Entries.push_back(std::move(entry));
            return m_Entries.back();
        }

    private:
        std::vector<ComponentEntry> m_Entries;
    };

    // Registers the built-in components (ComponentEditors.cpp).
    void RegisterBuiltinComponents(ComponentRegistry& registry);
} // namespace Kans
