#include "Kans3D/Asset/Importer/MaterialSource.h"
#include <fstream>
#include <imgui.h>
#include <algorithm>
#include "Kans3D/Asset/Storage/AssetProvider.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Scene/SceneSerializer.h"
#include "EditorLayer.h"
#include <set>

namespace Kans
{
    void EditorLayer::OpenProject()
    {
        // Bootstrap from editor settings until project files are implemented.
        ProjectConfig config;
        config.Name = "KansEditor Project";
        std::error_code ec;
        config.RootDirectory = std::filesystem::current_path(ec);
        if (ec)
        {
            CORE_ERROR("Cannot resolve project root: {}", ec.message());
            return;
        }
        config.AssetDirectory = KansFileSystem::GetAssetFolder();
        // The source asset cache lives in the project's own cache directory, so
        // projects opened by the same editor never share (and overwrite) it.
        auto opened = Project::Open(config);
        if (const auto* error = std::get_if<AssetError>(&opened))
        {
            CORE_ERROR("Cannot open project: {}", error->Message);
            return;
        }
        auto project = std::move(std::get<std::unique_ptr<Project>>(opened));
        for (const auto& warning : project->GetAssetWarnings())
            CORE_WARN("Project asset cache: {}", warning.Message);
        auto controller = EditorAssetImportService::Create(std::move(project));
        if (const auto* error = std::get_if<AssetError>(&controller))
        {
            CORE_ERROR("Cannot create project import controller: {}", error->Message);
            return;
        }
        m_AssetImports = std::get<Scope<EditorAssetImportService>>(std::move(controller));
        m_ProductImportPanel.Bind(m_AssetImports.get());
        m_MaterialEditorPanel.Bind(m_AssetImports.get());
        m_ContentBrowserPanel.SetOpenFileCallback([this](const std::filesystem::path& path) {
            if (path.extension() == ".kans")
            {
                Record(OpenScene(path));
            }
            else if (path.extension() == ".material")
                OpenMaterial(path);
            else
                OpenImporter(path);
        });
        m_ContentBrowserPanel.SetCreateMaterialCallback(
            [this](const std::filesystem::path& directory) { CreateMaterial(directory); });
        m_ContentBrowserPanel.SetImportCallback([this](const std::filesystem::path& path) { ImportSource(path); });
        m_ContentBrowserPanel.SetImportSettingsCallback(
            [this](const std::filesystem::path& path) { OpenImporter(path); });
        m_ContentBrowserPanel.SetExtractMaterialsCallback(
            [this](const std::filesystem::path& path) { ExtractMaterials(path); });
        m_ContentBrowserPanel.SetCreateInstanceCallback(
            [this](const std::filesystem::path& path) { CreateMaterialInstance(path); });
        m_ContentBrowserPanel.SetErrorCallback(
            [this](const std::string& message) { Record(AssetError {AssetErrorCode::InvalidArgument, message}); });
        m_ContentBrowserPanel.Bind(m_AssetImports.get());
        m_HierarchyPanel.SetFileDropCallback(
            [this](const std::filesystem::path& file, Entity parent) { PlaceAsset(file, parent); });
        CORE_INFO("Opened project '{}'", m_AssetImports->GetConfig().Name);
        // Re-import products built by older importers (up-to-date sources are only
        // checked). Models appear once the sweep's registry is applied in Poll.
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        Record(m_AssetImports->StartOutdated(options));
    }

    std::variant<std::monostate, AssetError> EditorLayer::RefreshProductAssets(Ref<const AssetRegistry> snapshot)
    {
        if (!m_AssetImports || !m_Viewport)
            return AssetError {AssetErrorCode::NotFound, "Project/scene renderer is unavailable"};
        if (!snapshot)
            snapshot = m_AssetImports->GetRuntimeAssetRegistry();
        auto choices = CollectAssetChoices(*snapshot);
        m_MaterialEditorPanel.SetAssetChoices(choices);
        m_PropertiesPanel.SetAssetChoices(std::move(choices));
        if (m_Assets)
        {
            auto changed = m_Assets->SetRegistry(std::move(snapshot));
            if (const auto* error = std::get_if<AssetError>(&changed))
                return *error;
            return std::monostate {};
        }
        auto provider = LooseFileAssetProvider::Open(m_AssetImports->GetProductDirectory());
        if (const auto* error = std::get_if<AssetError>(&provider))
            return *error;
        AssetManagerSpecification spec;
        spec.Target = EditorAssetTarget;
        m_Assets    = CreateScope<AssetManager>(
            std::get<Ref<LooseFileAssetProvider>>(std::move(provider)), std::move(snapshot), std::move(spec));
        m_Viewport->SetAssetManager(m_Assets.get());
        m_MaterialPreview->SetResources(m_Viewport->GetAssetCache(), m_Viewport->GetEnvironment());
        m_ThumbnailRenderer->SetResources(m_Viewport->GetAssetCache(), m_Viewport->GetEnvironment());
        m_PropertiesPanel.SetAssetManager(m_Assets.get());
        m_MaterialEditorPanel.SetAssetManager(m_Assets.get());
        return std::monostate {};
    }

    std::vector<EditorAssetChoice> EditorLayer::CollectAssetChoices(const AssetRegistry& registry) const
    {
        std::unordered_map<SourceAssetID, std::filesystem::path> sources;
        bool                                                     listed = false;
        if (auto  all  = m_AssetImports->GetSources();
            auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&all))
        {
            listed = true;
            for (const auto& source : *list)
                if (source.Exists)
                    sources.emplace(source.sourceAssetID, source.Path.GetPath());
        }
        const auto& assetRoot = m_AssetImports->GetConfig().AssetDirectory;

        std::vector<EditorAssetChoice> choices;
        for (const auto& record : registry.GetAllProducts())
        {
            if (record.Target != EditorAssetTarget)
                continue;
            const AssetID     id = record.Metadata.assetID;
            EditorAssetChoice choice;
            choice.ID        = id;
            choice.Type      = record.Metadata.Type;
            const auto found = sources.find(id.GetSourceID());
            if (found != sources.end())
            {
                std::error_code error;
                choice.SourcePath = std::filesystem::weakly_canonical(assetRoot / found->second, error);
                choice.Name       = found->second.generic_u8string();
            }
            else if (listed)
                continue; // Products left behind by a deleted or moved source are not offered.
            else
                choice.Name = id.GetSourceID().ToString();
            // Products other than a source's main Model are distinguished by LocalID.
            if (choice.Type != AssetType::Model)
                choice.Name += " #" + std::to_string(id.GetLocalID());
            choices.push_back(std::move(choice));
        }
        std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.Name < b.Name; });
        return choices;
    }

    AssetID EditorLayer::FindPublishedModel(SourceAssetID source) const
    {
        for (const auto& record : m_AssetImports->GetRuntimeAssetRegistry()->GetAllProducts())
            if (record.Metadata.assetID.GetSourceID() == source && record.Metadata.Type == AssetType::Model &&
                record.Target == EditorAssetTarget)
                return record.Metadata.assetID;
        return {};
    }

    std::optional<SourceAssetID> EditorLayer::FindModelSource(const std::filesystem::path& file)
    {
        if (!m_AssetImports)
            return std::nullopt;
        auto sources = m_AssetImports->GetSources();
        if (const auto* error = std::get_if<AssetError>(&sources))
        {
            Record(*error);
            return std::nullopt;
        }
        std::error_code error;
        const auto      dropped = std::filesystem::weakly_canonical(file, error);
        const auto&     root    = m_AssetImports->GetConfig().AssetDirectory;
        for (const auto& source : std::get<std::vector<SourceAssetMetadata>>(sources))
        {
            if (!source.Exists || std::filesystem::weakly_canonical(root / source.Path.GetPath(), error) != dropped)
                continue;
            const auto importer = CreateDefaultImporterRegistry();
            const auto selected = std::get<ImporterRegistry>(importer).Resolve(file, source.Import.Importer);
            if (std::holds_alternative<AssetError>(selected) ||
                std::get<RegisteredImporter>(selected).Name != "ModelImporter")
                return std::nullopt;
            return source.sourceAssetID;
        }
        return std::nullopt;
    }

    void EditorLayer::PlaceAsset(const std::filesystem::path& file, Entity parent, std::optional<glm::vec3> position)
    {
        if (!m_AssetImports || !m_ActiveScene)
            return;
        const auto source = FindModelSource(file);
        if (!source)
        {
            OpenImporter(file); // Not a model: show it in the import panel instead.
            return;
        }
        // Always build: it is quick when up to date and picks up edited sources.
        m_PendingPlacement         = *source;
        m_PendingPlacementParent   = parent ? parent.GetUUID() : UUID(0);
        m_PendingPlacementPosition = position;
        m_PendingPlacementWaiting  = false;
        auto snapshot              = m_AssetImports->GetSnapshot();
        if (const auto* state = std::get_if<EditorAssetImportSnapshot>(&snapshot);
            state && state->State == EditorAssetImportState::Building)
        {
            m_PendingPlacementWaiting = true; // e.g. the project-open re-import sweep
            return;
        }
        StartPendingPlacementBuild();
    }

    void EditorLayer::StartPendingPlacementBuild()
    {
        m_PendingPlacementWaiting = false;
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        auto started   = m_AssetImports->Start(*m_PendingPlacement, options);
        if (const auto* failure = std::get_if<AssetError>(&started))
        {
            Record(*failure);
            m_PendingPlacement.reset();
        }
    }

    void EditorLayer::ExtractMaterials(const std::filesystem::path& file)
    {
        const auto source = FindModelSource(file);
        if (!source)
        {
            Record(AssetError {AssetErrorCode::InvalidArgument, "Not a model source: " + file.filename().u8string()});
            return;
        }
        std::error_code error;
        const auto      folder =
            std::filesystem::weakly_canonical(file, error).parent_path().lexically_relative(
                std::filesystem::weakly_canonical(m_AssetImports->GetConfig().AssetDirectory, error)) /
            "Materials";
        auto result = m_AssetImports->ExtractModelMaterials(*source, folder);
        if (const auto* failure = std::get_if<AssetError>(&result))
        {
            Record(*failure);
            return;
        }
        const auto& extracted = std::get<ModelMaterialExtractionResult>(result);
        for (const auto& created : extracted.Created)
            CLIENT_INFO_TAG("Editor", "Extracted material {}", created.File.u8string());
        if (extracted.Error)
            Record(*extracted.Error);
        else if (extracted.Created.empty())
            Record(AssetError {AssetErrorCode::InvalidArgument, "Every material of this model already uses a project material"});
        if (extracted.RemapsApplied && !extracted.Created.empty())
        {
            AssetBuildOptions options;
            options.Target = EditorAssetTarget;
            if (auto started = m_AssetImports->Start(*source, options); std::holds_alternative<AssetError>(started))
                Record(std::get<AssetError>(started));
        }
        m_ContentBrowserPanel.Refresh();
        OpenImporter(file); // Shows the slots and the build result.
    }

    Ref<Texture2D> EditorLayer::MaterialThumbnail(const std::filesystem::path& file, uint32_t size)
    {
        if (!m_AssetImports || !m_Assets || !m_ThumbnailRenderer)
            return nullptr;
        std::error_code error;
        const auto      key   = std::filesystem::weakly_canonical(file, error).u8string();
        auto&           entry = m_MaterialThumbnails[key];
        if (!entry.Material.IsValid())
        {
            if (m_ThumbnailBudget <= 0)
                return entry.Texture;
            --m_ThumbnailBudget; // Finding the source walks the source list.
            auto sources = m_AssetImports->GetSources();
            if (const auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
                for (const auto& source : *list)
                    if (std::filesystem::weakly_canonical(
                            m_AssetImports->GetConfig().AssetDirectory / source.Path.GetPath(), error)
                            .u8string() == key)
                        for (const auto& product : m_AssetImports->GetRuntimeAssetRegistry()->GetAllProducts())
                            if (product.Metadata.assetID.GetSourceID() == source.sourceAssetID &&
                                product.Metadata.Type == AssetType::Material && product.Target == EditorAssetTarget)
                                entry.Material = product.Metadata.assetID;
            if (!entry.Material.IsValid())
                return nullptr; // Not imported yet; looked up again after the next import.
        }
        // Re-render when the material is rebuilt (a new asset) or a larger image is wanted.
        const auto status = m_Assets->Request<MaterialDataAsset>(entry.Material);
        if (status.IsReady() && (status.Asset != entry.Rendered || size > entry.Size) && m_ThumbnailBudget > 0)
        {
            --m_ThumbnailBudget;
            MaterialPreviewOptions options;
            options.Size = {size, size};
            if (auto image = m_ThumbnailRenderer->RenderThumbnail(entry.Material, options))
            {
                TextureData data;
                data.Width      = image->Size.x;
                data.Height     = image->Size.y;
                data.Format     = TexturePixelFormat::RGBA8;
                data.ColorSpace = TextureColorSpace::Linear; // Already display-ready.
                std::vector<std::byte> pixels(image->Pixels.size());
                std::memcpy(pixels.data(), image->Pixels.data(), pixels.size());
                data.Mips = {{std::move(pixels)}};
                if (auto uploaded = Texture2D::CreateFromProduct(data);
                    std::holds_alternative<Ref<Texture2D>>(uploaded))
                {
                    entry.Texture  = std::get<Ref<Texture2D>>(uploaded);
                    entry.Rendered = status.Asset;
                    entry.Size     = size;
                }
            }
        }
        if (status.Error)
            entry.Material = {}; // Deleted or unpublished: find it again.
        return entry.Texture;
    }

    AssetID EditorLayer::FindPublishedMaterial(const std::filesystem::path& file)
    {
        if (!m_AssetImports)
            return {};
        std::error_code error;
        const auto      wanted  = std::filesystem::weakly_canonical(file, error);
        auto            sources = m_AssetImports->GetSources();
        if (const auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
            for (const auto& source : *list)
                if (std::filesystem::weakly_canonical(m_AssetImports->GetConfig().AssetDirectory / source.Path.GetPath(),
                                                      error) == wanted)
                    for (const auto& product : m_AssetImports->GetRuntimeAssetRegistry()->GetAllProducts())
                        if (product.Metadata.assetID.GetSourceID() == source.sourceAssetID &&
                            product.Metadata.Type == AssetType::Material && product.Target == EditorAssetTarget)
                            return product.Metadata.assetID;
        return {};
    }

    void EditorLayer::CreateMaterialInstance(const std::filesystem::path& file)
    {
        const AssetID parent = FindPublishedMaterial(file);
        if (!parent.IsValid())
        {
            Record(AssetError {AssetErrorCode::NotFound, "Import the material before creating an instance of it"});
            return;
        }
        std::filesystem::path target;
        for (int index = 0; index < 1000; ++index)
        {
            target = file.parent_path() / (file.stem().u8string() + "_Instance" +
                                           (index ? std::to_string(index) : std::string()) + ".material");
            std::error_code error;
            if (!std::filesystem::exists(target, error))
                break;
        }
        MaterialSource source;
        source.Name   = target.stem().u8string();
        source.Parent = parent;
        {
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            output << SerializeMaterialSource(source);
            if (!output)
            {
                Record(AssetError {AssetErrorCode::IoError, "Cannot write " + target.u8string()});
                return;
            }
        }
        Record(m_AssetImports->RefreshSources());
        auto sources = m_AssetImports->GetSources();
        if (const auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
        {
            std::error_code error;
            for (const auto& meta : *list)
                if (std::filesystem::weakly_canonical(m_AssetImports->GetConfig().AssetDirectory / meta.Path.GetPath(),
                                                      error) == std::filesystem::weakly_canonical(target, error))
                {
                    AssetBuildOptions options;
                    options.Target = EditorAssetTarget;
                    if (auto started = m_AssetImports->Start(meta.sourceAssetID, options);
                        std::holds_alternative<AssetError>(started))
                        Record(std::get<AssetError>(started));
                }
        }
        m_ContentBrowserPanel.Refresh();
        OpenMaterial(target);
    }

    void EditorLayer::ImportSource(const std::filesystem::path& file)
    {
        if (!m_AssetImports)
            return;
        auto sources = m_AssetImports->GetSources();
        if (const auto* error = std::get_if<AssetError>(&sources))
        {
            Record(*error);
            return;
        }
        std::error_code error;
        const auto      target = std::filesystem::weakly_canonical(file, error);
        const auto&     root   = m_AssetImports->GetConfig().AssetDirectory;
        for (const auto& source : std::get<std::vector<SourceAssetMetadata>>(sources))
            if (source.Exists && std::filesystem::weakly_canonical(root / source.Path.GetPath(), error) == target)
            {
                AssetBuildOptions options;
                options.Target = EditorAssetTarget;
                Record(m_AssetImports->Start(source.sourceAssetID, options));
                return;
            }
        Record(AssetError {AssetErrorCode::NotFound, file.filename().u8string() + " is not a project asset"});
    }

    void EditorLayer::PlacePendingModel()
    {
        if (!m_PendingPlacement || !m_ActiveScene)
            return;
        const auto source = *m_PendingPlacement;
        m_PendingPlacement.reset();
        const AssetID model = FindPublishedModel(source);
        if (!model.IsValid())
            return; // The import failed; the import panel shows why.
        std::string name = "Model";
        if (auto  sources = m_AssetImports->GetSources();
            auto* list    = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
            for (const auto& metadata : *list)
                if (metadata.sourceAssetID == source)
                    name = metadata.Path.GetPath().stem().u8string();
        // The instance's parts appear once the prefab system has the prefab's data.
        auto entity = m_ActiveScene->InstantiatePrefab(model, {}, m_PendingPlacementParent);
        entity.GetComponent<TagComponent>().Tag = name;
        if (m_PendingPlacementPosition)
        {
            // Keep the model's world position under the parent.
            glm::mat4 world = m_ActiveScene->GetWorldTransform(entity);
            world[3]        = glm::vec4(*m_PendingPlacementPosition, 1.0f);
            m_ActiveScene->SetWorldTransform(entity, world);
        }
        m_PendingPlacementParent = 0;
        m_PendingPlacementPosition.reset();
        m_HierarchyPanel.SetSelected(entity);
    }

    std::variant<std::monostate, AssetError> EditorLayer::ActivateScene(Ref<Scene> scene, std::filesystem::path path)
    {
        if (!scene || !m_Viewport)
            return AssetError {AssetErrorCode::InvalidArgument, "Scene/renderer unavailable"};
        const auto viewport = m_Viewport->GetSize();
        scene->OnViewportResize(std::max(1u, viewport.x), std::max(1u, viewport.y));
        // Leaving a playing scene drops its runtime state; the edit-time state is not kept.
        if (m_ActiveScene)
            m_Play.Discard(*m_ActiveScene);
        m_ActiveScene = std::move(scene);
        m_Viewport->SetScene(m_ActiveScene);
        m_HierarchyPanel.SetScene(m_ActiveScene);
        m_ViewportPanel.SetContext(m_ActiveScene, m_Viewport.get());
        m_CameraEntity       = {};
        m_SecondCameraEntity = {};
        m_ScenePath          = std::move(path);
        m_History.Reset(*m_ActiveScene);
        m_EditPending = false;
        return std::monostate {};
    }

    std::variant<std::monostate, AssetError> EditorLayer::NewScene() { return ActivateScene(CreateRef<Scene>(), {}); }

    namespace
    {
        std::variant<std::filesystem::path, AssetError> SceneFilePath(const EditorAssetImportService* project,
                                                                      const std::filesystem::path&    path)
        {
            if (path.empty() || path.extension() != ".kans")
                return AssetError {AssetErrorCode::InvalidArgument, "Choose a .kans scene file"};
            std::error_code error;
            auto            root = project ? project->GetConfig().RootDirectory : std::filesystem::current_path(error);
            if (error)
                return AssetError {AssetErrorCode::IoError, error.message()};
            return (root / path).lexically_normal();
        }
    } // namespace

    std::variant<std::monostate, AssetError> EditorLayer::OpenScene(const std::filesystem::path& path)
    {
        auto resolved = SceneFilePath(m_AssetImports.get(), path);
        if (const auto* error = std::get_if<AssetError>(&resolved))
            return *error;
        auto fullPath  = std::get<std::filesystem::path>(std::move(resolved));
        auto candidate = SceneSerializer::DeserializeChecked(fullPath);
        if (const auto* error = std::get_if<AssetError>(&candidate))
            return *error;
        auto activated = ActivateScene(std::get<Ref<Scene>>(std::move(candidate)), std::move(fullPath));
        if (!std::holds_alternative<std::monostate>(activated))
            return activated;
        // The editor view starts with the scene camera's exposure and bloom, so it looks
        // like the game; the viewport's camera menu still changes them independently.
        if (Entity camera = m_ActiveScene->GetCameraEntity())
        {
            const auto& sceneCamera = camera.GetComponent<CameraComponent>().SceneCamera;
            m_ViewportPanel.GetCamera().SetExposure(sceneCamera.GetExposure());
            m_ViewportPanel.GetCamera().SetBloomSettings(sceneCamera.GetBloomSettings());
            m_ViewportPanel.GetCamera().SetAntiAliasing(sceneCamera.GetAntiAliasing());
        }
        QueueMissingSceneAssets();
        return activated;
    }

    void EditorLayer::QueueMissingSceneAssets()
    {
        if (!m_AssetImports || !m_ActiveScene)
            return;
        std::set<SourceAssetID> referenced;
        const auto              add = [&](AssetID id) {
            if (id.IsValid())
                referenced.insert(id.GetSourceID());
        };
        for (Entity entity : m_ActiveScene->GetMeshEntities())
        {
            if (entity.HasComponent<EditableMeshComponent>())
                for (auto material : entity.GetComponent<EditableMeshComponent>().Materials)
                    add(material);
            if (entity.HasComponent<MeshComponent>())
                for (auto material : entity.GetComponent<MeshComponent>().Materials)
                    add(material);
        }
        std::function<void(Entity)> visit = [&](Entity entity) {
            if (entity.HasComponent<PrefabInstanceComponent>())
                add(entity.GetComponent<PrefabInstanceComponent>().Prefab);
            for (Entity child : m_ActiveScene->GetChildren(entity))
                visit(child);
        };
        for (Entity root : m_ActiveScene->GetRootEntities())
            visit(root);
        // Sources with published products are rebuilt by the open-time sweep instead.
        for (const auto& record : m_AssetImports->GetRuntimeAssetRegistry()->GetAllProducts())
            referenced.erase(record.Metadata.assetID.GetSourceID());
        auto sources = m_AssetImports->GetSources();
        if (const auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
            for (const auto& source : *list)
                if (source.Exists && referenced.count(source.sourceAssetID))
                    QueueAutoImport(source.sourceAssetID);
        StartNextAutoImport();
    }

    void EditorLayer::QueueUnimportedShaders()
    {
        if (!m_AssetImports)
            return;
        std::set<SourceAssetID> published;
        for (const auto& record : m_AssetImports->GetRuntimeAssetRegistry()->GetAllProducts())
            published.insert(record.Metadata.assetID.GetSourceID());
        auto sources = m_AssetImports->GetSources();
        if (const auto* list = std::get_if<std::vector<SourceAssetMetadata>>(&sources))
            for (const auto& source : *list)
                if (source.Exists && source.Path.GetPath().extension() == ".shaderprog" &&
                    !published.count(source.sourceAssetID) && !m_AutoImported.count(source.sourceAssetID))
                {
                    CORE_INFO("Importing shader {}", source.Path.GetPath().generic_u8string());
                    QueueAutoImport(source.sourceAssetID);
                }
        StartNextAutoImport();
    }

    void EditorLayer::QueueAutoImport(SourceAssetID source)
    {
        if (m_AutoImported.insert(source).second)
            m_AutoImports.push_back(source);
    }

    void EditorLayer::StartNextAutoImport()
    {
        if (m_AutoImports.empty() || m_PendingPlacement)
            return;
        auto snapshot = m_AssetImports->GetSnapshot();
        if (const auto* state = std::get_if<EditorAssetImportSnapshot>(&snapshot);
            !state || state->State == EditorAssetImportState::Building)
            return; // Retried when the running build completes.
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        const auto source = m_AutoImports.front();
        m_AutoImports.pop_front();
        Record(m_AssetImports->Start(source, options));
    }

    std::variant<std::monostate, AssetError> EditorLayer::SaveScene(const std::filesystem::path& path)
    {
        if (m_Play.IsActive())
            return AssetError {AssetErrorCode::InvalidArgument, "Stop play mode before saving the scene"};
        auto resolved = SceneFilePath(m_AssetImports.get(), path);
        if (const auto* error = std::get_if<AssetError>(&resolved))
            return *error;
        auto fullPath = std::get<std::filesystem::path>(std::move(resolved));
        auto saved    = SceneSerializer(m_ActiveScene).SerializeChecked(fullPath);
        if (const auto* error = std::get_if<AssetError>(&saved))
            return *error;
        m_ScenePath = std::move(fullPath);
        m_History.MarkSaved();
        return std::monostate {};
    }

    void EditorLayer::Record(const std::variant<std::monostate, AssetError>& result)
    {
        if (const auto* error = std::get_if<AssetError>(&result))
        {
            m_SceneFileError = error->Message;
            CORE_ERROR("Editor: {}", error->Message);
        }
    }

    void EditorLayer::SaveCurrentScene()
    {
        if (m_ScenePath.empty())
            m_SceneDialog = SceneDialog::Save;
        else
            Record(SaveScene(m_ScenePath));
    }

    void EditorLayer::DrawSceneFileDialogs()
    {
        // Opened here, not where requested, so the popup shares this ID stack.
        if (m_SceneDialog != SceneDialog::None)
        {
            std::error_code error;
            const auto      root = m_AssetImports ? m_AssetImports->GetConfig().RootDirectory : std::filesystem::current_path(error);
            m_SceneFileDialog.Open(m_SceneDialog == SceneDialog::Open ? SceneFileDialog::Mode::Open : SceneFileDialog::Mode::Save,
                                   root,
                                   m_ScenePath.empty() ? root / "assets" / "scenes" : m_ScenePath);
            m_SceneDialog = SceneDialog::None;
        }
        m_SceneFileDialog.Draw([this](const std::filesystem::path& file) -> std::optional<std::string> {
            auto result = m_SceneFileDialog.GetMode() == SceneFileDialog::Mode::Open ? OpenScene(file) : SaveScene(file);
            if (const auto* error = std::get_if<AssetError>(&result))
                return error->Message;
            return std::nullopt;
        });
    }
} // namespace Kans
