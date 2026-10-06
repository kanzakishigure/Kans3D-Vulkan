#include <imgui.h>
#include <algorithm>
#include "Kans3D/Asset/Storage/AssetProvider.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Scene/SceneSerializer.h"
#include "EditorAssetCachePath.h"
#include "EditorLayer.h"

namespace Kans
{
    void EditorLayer::OpenProject()
    {
        const auto cacheLocation = GetEditorAssetCachePath();
        if (const auto* error = std::get_if<AssetError>(&cacheLocation))
        {
            CORE_ERROR("Project cache location: {}", error->Message);
            return;
        }
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
        ProjectOpenOptions options;
        options.SourceAssetCachePath = std::get<std::filesystem::path>(cacheLocation);
        auto opened                  = Project::Open(config, options);
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
        m_ContentBrowserPanel.SetOpenFileCallback([this](const std::filesystem::path& path) {
            if (path.extension() == ".kans")
            {
                auto opened = OpenScene(path);
                if (const auto* error = std::get_if<AssetError>(&opened))
                    m_SceneFileError = error->Message;
            }
            else
                m_ProductImportPanel.OpenWithFile(path);
        });
        CORE_INFO("Opened project '{}'", m_AssetImports->GetConfig().Name);
    }

    std::variant<std::monostate, AssetError> EditorLayer::RefreshProductAssets(Ref<const AssetRegistry> snapshot)
    {
        if (!m_AssetImports || !m_ViewportRenderer)
            return AssetError {AssetErrorCode::NotFound, "Project/scene renderer is unavailable"};
        if (!snapshot)
            snapshot = m_AssetImports->GetRuntimeAssetRegistry();
        m_SceneHierachyPanel.setAssetChoices(CollectAssetChoices(*snapshot));
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
        m_ViewportRenderer->SetAssetManager(m_Assets.get());
        return std::monostate {};
    }

    std::vector<EditorAssetChoice> EditorLayer::CollectAssetChoices(const AssetRegistry& registry) const
    {
        std::unordered_map<SourceAssetID, std::filesystem::path> sources;
        if (auto  listed = m_AssetImports->GetSources();
            auto* list   = std::get_if<std::vector<SourceAssetMetadata>>(&listed))
            for (const auto& source : *list)
                sources.emplace(source.sourceAssetID, source.Path.GetPath());
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

    std::variant<std::monostate, AssetError> EditorLayer::ActivateScene(Ref<Scene> scene, std::filesystem::path path)
    {
        if (!scene || !m_ViewportRenderer || !m_ActiveRenderScene)
            return AssetError {AssetErrorCode::InvalidArgument, "Scene/renderer unavailable"};
        const auto viewport = m_ViewportRenderer->GetViewportSize();
        scene->OnViewportResize(std::max(1u, viewport[0]), std::max(1u, viewport[1]));
        if (m_ActiveScene)
            m_ActiveScene->OnRuntimeStop();
        m_ActiveScene = std::move(scene);
        m_ActiveRenderScene->SetLogicScene(m_ActiveScene);
        m_SceneHierachyPanel.setSceneContext(m_ActiveScene);
        m_CameraEntity       = {};
        m_SecondCameraEntity = {};
        m_ScenePath          = std::move(path);
        m_CameraNavigating   = false;
        m_EditorCamera.resetMotion();
        m_ActiveScene->OnRuntimeStart();
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
        return ActivateScene(std::get<Ref<Scene>>(std::move(candidate)), std::move(fullPath));
    }

    std::variant<std::monostate, AssetError> EditorLayer::SaveScene(const std::filesystem::path& path)
    {
        auto resolved = SceneFilePath(m_AssetImports.get(), path);
        if (const auto* error = std::get_if<AssetError>(&resolved))
            return *error;
        auto fullPath = std::get<std::filesystem::path>(std::move(resolved));
        auto saved    = SceneSerializer(m_ActiveScene).SerializeChecked(fullPath);
        if (const auto* error = std::get_if<AssetError>(&saved))
            return *error;
        m_ScenePath = std::move(fullPath);
        return std::monostate {};
    }

    void EditorLayer::DrawSceneFileMenu()
    {
        auto record = [this](const std::variant<std::monostate, AssetError>& result) {
            m_SceneFileError.clear();
            if (const auto* error = std::get_if<AssetError>(&result))
            {
                m_SceneFileError = error->Message;
                CORE_ERROR("Scene operation: {}", error->Message);
            }
        };
        bool open = false, save = false;
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene"))
                record(NewScene());
            open = ImGui::MenuItem("Open Scene...");
            if (ImGui::MenuItem("Save Scene"))
            {
                if (m_ScenePath.empty())
                    save = true;
                else
                    record(SaveScene(m_ScenePath));
            }
            save = ImGui::MenuItem("Save Scene As...") || save;
            if (ImGui::MenuItem("Refresh Published Products"))
                record(RefreshProductAssets());
            if (ImGui::MenuItem("Product Import..."))
                m_ProductImportPanel.Open();
            if (ImGui::MenuItem("Legacy Model Preview..."))
                m_ImporterPanel.Open();
            if (!m_SceneFileError.empty())
                ImGui::TextWrapped("%s", m_SceneFileError.c_str());
            ImGui::EndMenu();
        }
        if (open || save)
        {
            m_SceneFileError.clear();
            m_ScenePathInput.fill(0);
            const auto suggested =
                m_ScenePath.empty() ? std::string("assets/scenes/UntitledScene.kans") : m_ScenePath.generic_u8string();
            if (suggested.size() < m_ScenePathInput.size())
                std::copy(suggested.begin(), suggested.end(), m_ScenePathInput.begin());
            ImGui::OpenPopup(open ? "Open Scene File" : "Save Scene File");
        }
        for (bool opening : {true, false})
        {
            if (!ImGui::BeginPopupModal(
                    opening ? "Open Scene File" : "Save Scene File", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                continue;
            ImGui::InputText("Scene path", m_ScenePathInput.data(), m_ScenePathInput.size());
            if (!m_SceneFileError.empty())
                ImGui::TextWrapped("%s", m_SceneFileError.c_str());
            if (ImGui::Button(opening ? "Open" : "Save"))
            {
                const auto path = std::filesystem::u8path(m_ScenePathInput.data());
                record(opening ? OpenScene(path) : SaveScene(path));
                if (m_SceneFileError.empty())
                    ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
} // namespace Kans
