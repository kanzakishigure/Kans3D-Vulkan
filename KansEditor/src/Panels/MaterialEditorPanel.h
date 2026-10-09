#pragma once
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Editor/EditorAssetImportService.h"
#include "Kans3D/Editor/Panel/SceneHierachyPanel.h"

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

        void Open(const std::filesystem::path& file);
        // Creates a new engine PBR material in `directory` and opens it.
        void Create(const std::filesystem::path& directory);
        void Draw();

    private:
        // Schema of the selected Shader; nullptr while it loads or if it is invalid.
        const MaterialSchema* Schema();
        void                  DrawParameter(const MaterialParameterInfo& parameter, const MaterialSchema& schema);
        void                  DrawTexture(const std::string& name);
        void                  Save();
        std::string           ChoiceName(AssetID id) const;

        EditorAssetImportService*      m_Service = nullptr;
        AssetManager*                  m_Assets  = nullptr;
        std::vector<EditorAssetChoice> m_Choices;

        bool                  m_Open  = false;
        bool                  m_Dirty = false;
        std::filesystem::path m_File;
        MaterialSource        m_Source;
        std::string           m_Status;
        bool                  m_Building = false;

        // Schema cache, keyed by the Shader asset it was built from.
        Ref<const ShaderDataAsset>    m_SchemaShader;
        std::optional<MaterialSchema> m_Schema;
        std::string                   m_SchemaError;
    };
} // namespace Kans
