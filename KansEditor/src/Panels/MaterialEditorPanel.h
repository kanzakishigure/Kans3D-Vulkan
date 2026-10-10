#pragma once
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Editor/EditorAssetImportService.h"
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Editor/EditorAssetChoice.h"
#include "Kans3D/Renderer/Resource/Texture.h"

namespace Kans
{
    // Edits `.material` sources: the Shader (or engine PBR), its parameters and
    // textures. Saving writes the source and builds it so the scene updates.
    class MaterialEditorPanel
    {
    public:
        // Non-owning; the editor disconnects before destroying them.
        void Bind(EditorAssetImportService* service) { m_Service = service; }
        void SetAssetManager(AssetManager* assets) { m_Assets = assets; }
        void SetAssetChoices(std::vector<EditorAssetChoice> choices) { m_Choices = std::move(choices); }

        // Loads a material for editing; the editor opens the window.
        void Open(const std::filesystem::path& file);
        // Creates a new engine PBR material in `directory` and loads it.
        void Create(const std::filesystem::path& directory);
        // Contents of the window the editor's panel manager opened.
        void Draw();
        bool IsDirty() const { return m_Dirty; }

    private:
        // Schema of the selected Shader; nullptr while it loads or if it is invalid.
        const MaterialSchema* Schema();
        void                  DrawParameter(const MaterialParameterInfo& parameter, const MaterialSchema& schema);
        void                  DrawTexture(const std::string& name);
        // Thumbnail button for a texture (or an empty slot); shows a larger preview on hover.
        void DrawThumbnail(AssetID texture, float size);
        // Assigns a file dropped from the content browser, importing it first if needed.
        void AssignDroppedFile(const std::string& slot, const std::filesystem::path& file);
        void FinishPendingTexture();
        void SetTexture(const std::string& slot, AssetID texture);
        void                  Save();
        std::string           ChoiceName(AssetID id) const;

        EditorAssetImportService*      m_Service = nullptr;
        AssetManager*                  m_Assets  = nullptr;
        std::vector<EditorAssetChoice> m_Choices;

        bool                  m_Dirty = false;
        std::filesystem::path m_File;
        MaterialSource        m_Source;
        std::string           m_Status;
        bool                  m_Building = false;

        // GPU thumbnails of texture Products, rebuilt when the asset changes.
        struct Thumbnail
        {
            Ref<const TextureAsset> Source;
            Ref<Texture2D>          Texture;
        };
        std::unordered_map<AssetID, Thumbnail> m_Thumbnails;
        // Texture slot waiting for a dropped source to finish importing.
        std::optional<std::pair<std::string, SourceAssetID>> m_PendingTexture;
        std::string                                          m_TextureFilter;

        // Schema cache, keyed by the Shader asset it was built from.
        Ref<const ShaderDataAsset>    m_SchemaShader;
        std::optional<MaterialSchema> m_Schema;
        std::string                   m_SchemaError;
    };
} // namespace Kans
