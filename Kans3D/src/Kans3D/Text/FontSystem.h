#pragma once
#include <functional>
#include <map>
#include "FontAsset.h"
namespace Kans
{
    // Engine defaults bypass AssetManager: read Roboto and the whole Noto TTC from
    // KansFileSystem::GetFontsFolder(), share collection bytes and select regional
    // faces by family names. A supplied folder supports standalone tools/tests.
    // Project fallback IDs use the resolver, normally bound to AssetManager::Load<FontAsset>.
    // This system and its atlases are owner-thread objects. Fonts/quads retain assets.
    class FontSystem
    {
    public:
        explicit FontSystem(std::filesystem::path fontsFolder = {});
        static FontSystem& Default();
        using Resolver = std::function<Ref<const FontAsset>(AssetID)>;
        void                              SetResolver(Resolver resolver) { m_Resolver = std::move(resolver); }
        std::vector<Ref<const FontAsset>> Chain(const Ref<const FontAsset>& primary, const std::string& language);
        Ref<const FontAsset>              Latin();
        Ref<const FontAsset>              CJK(const std::string& language);
        void                              BeginFrame(uint64_t frame);
        size_t                            Rasterize(size_t glyphBudget = 16);
        static std::string                Region(const std::string& language);

    private:
        Ref<const FontAsset>                          Load(const std::filesystem::path&, uint32_t face = 0);
        std::filesystem::path                         m_Folder;
        Resolver                                      m_Resolver;
        Ref<const FontAsset>                          m_Latin;
        std::shared_ptr<const std::vector<std::byte>> m_CJKBytes;
        std::map<std::string, Ref<const FontAsset>>   m_CJK;
        std::vector<WeakRef<const FontAsset>>         m_Tracked;
        uint64_t                                      m_Frame = 0;
    };
} // namespace Kans
