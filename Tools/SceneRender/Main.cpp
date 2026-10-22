#include <png.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Asset/Runtime/AudioClipData.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Project/Project.h"
#include "Kans3D/Renderer/RenderWorld.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/ViewTarget.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/SceneSerializer.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;
    namespace RH = RenderHardware;
    template<class T>
    T Checked(std::variant<T, AssetError> value)
    {
        if (const auto* error = std::get_if<AssetError>(&value))
            throw std::runtime_error(error->Message);
        return std::get<T>(std::move(value));
    }
    struct Options
    {
        fs::path   Scene, Output;
        glm::uvec2 Size {512, 512};
        double     Time = 0;
    };
    Options Parse(int argc, char** argv)
    {
        Options result;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg(argv[i]);
            const auto        at  = arg.find('=');
            const auto        key = arg.substr(0, at), value = at == std::string::npos ? "" : arg.substr(at + 1);
            if (key == "--scene")
                result.Scene = fs::u8path(value);
            else if (key == "--output")
                result.Output = fs::u8path(value);
            else if (key == "--time")
            {
                size_t used = 0;
                result.Time = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(result.Time) || result.Time < 0 ||
                    result.Time >= double(UINT64_MAX) / 60.0)
                    throw std::invalid_argument("--time must be finite and nonnegative");
            }
            else if (key == "--size")
            {
                const auto x = value.find('x');
                if (x == std::string::npos)
                    throw std::invalid_argument("--size must be WxH");
                const auto width = value.substr(0, x), height = value.substr(x + 1);
                const auto dimension = [](const std::string& text) {
                    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
                        throw std::invalid_argument("--size must contain positive integer dimensions");
                    const auto n = std::stoul(text);
                    if (n == 0 || n > 16384)
                        throw std::invalid_argument("--size dimensions must be 1..16384");
                    return uint32_t(n);
                };
                result.Size = {dimension(width), dimension(height)};
            }
            else
                throw std::invalid_argument("Unknown argument: " + arg);
        }
        if (result.Scene.empty() || result.Output.empty())
            throw std::invalid_argument("Usage: SceneRender --scene=<path> --time=<seconds> --size=WxH --output=<png>");
        return result;
    }
    void Build(Project& project)
    {
        const auto        importers = Checked(CreateDefaultImporterRegistry());
        AssetBuildOptions options;
        options.Target = "vulkan12";
        std::vector<std::string> errors;
        for (const auto& error : project.GetAssetWarnings())
            errors.push_back(error.Message);
        for (const auto& source : project.GetSourceAssetDatabase().GetAllSources())
        {
            // Deleted files keep their records (for ID recovery); a scene that needs one fails when it loads.
            if (source.Missing)
                continue;
            if (!importers.Supports(source.Path.GetPath()) && source.Import.Importer.empty())
                continue;
            auto built = project.BuildAsset(source.sourceAssetID, options);
            if (const auto* error = std::get_if<AssetError>(&built))
                errors.push_back(source.Path.GetPath().generic_string() + ": " + error->Message);
            else
            {
                const auto& graph = std::get<AssetBuildGraphResult>(built);
                for (const auto& warning : graph.Root.Warnings)
                    std::cerr << source.Path.GetPath() << ": " << warning << '\n';
            }
        }
        if (!errors.empty())
        {
            std::string message = "Asset build failed:";
            for (const auto& error : errors)
                message += "\n" + error;
            throw std::runtime_error(message);
        }
    }
    void LoadProducts(AssetManager& assets)
    {
        // Blocking loads make every CPU dependency available before prefab expansion and animation sampling.
        for (const auto& product : assets.GetRegistry()->GetAllProducts())
        {
            if (product.Target != assets.GetTarget())
                continue;
            const auto id = product.Metadata.assetID;
            switch (product.Metadata.Type)
            {
                case AssetType::Model:
                    Checked(assets.Load<ModelAsset>(id));
                    break;
                case AssetType::StaticMesh:
                    Checked(assets.Load<MeshAsset>(id));
                    break;
                case AssetType::Material:
                    Checked(assets.Load<MaterialDataAsset>(id));
                    break;
                case AssetType::Shader:
                    Checked(assets.Load<ShaderDataAsset>(id));
                    break;
                case AssetType::Texture:
                    Checked(assets.Load<TextureAsset>(id));
                    break;
                case AssetType::Skeleton:
                    Checked(assets.Load<SkeletonAsset>(id));
                    break;
                case AssetType::AnimationClip:
                    Checked(assets.Load<AnimationClipAsset>(id));
                    break;
                case AssetType::AnimationController:
                    Checked(assets.Load<AnimationControllerAsset>(id));
                    break;
                case AssetType::AudioClip:
                    Checked(assets.Load<AudioClipAsset>(id));
                    break;
                default:
                    throw std::runtime_error("Unsupported runtime Product " + id.ToString());
            }
        }
    }
    void Sample(Scene& scene, AssetManager& assets, double time)
    {
        scene.UpdatePrefabs();
        auto entities = scene.GetRootEntities();
        for (size_t i = 0; i < entities.size(); ++i)
        {
            auto entity   = entities[i];
            auto children = scene.GetChildren(entity);
            entities.insert(entities.end(), children.begin(), children.end());
            if (entity.HasComponent<PrefabInstanceComponent>())
            {
                const auto& prefab = entity.GetComponent<PrefabInstanceComponent>();
                if (!prefab.Source || !prefab.Expanded)
                    throw std::runtime_error("Cannot resolve prefab on " + std::to_string(uint64_t(entity.GetUUID())));
            }
            if (entity.HasComponent<MeshComponent>())
            {
                const auto& mesh   = entity.GetComponent<MeshComponent>();
                const auto  source = Checked(assets.Load<MeshAsset>(mesh.Mesh));
                if (mesh.Section && *mesh.Section >= source->GetData().Sections.size())
                    throw std::runtime_error("Mesh section out of range on " +
                                             std::to_string(uint64_t(entity.GetUUID())));
                for (const auto material : mesh.Materials)
                    if (material.IsValid())
                        Checked(assets.Load<MaterialDataAsset>(material));
            }
            if (entity.HasComponent<EditableMeshComponent>() && !scene.ResolveEditableMesh(entity))
                throw std::runtime_error("Cannot build editable mesh on " + std::to_string(uint64_t(entity.GetUUID())));
            if (entity.HasComponent<AnimatorComponent>() && entity.GetComponent<AnimatorComponent>().Enabled &&
                !scene.PrepareAnimator(entity))
            {
                const auto& error = entity.GetComponent<AnimatorComponent>().LastError;
                throw std::runtime_error(error ? error->Message : "Cannot resolve animator assets");
            }
        }
        // Start from the freshly loaded scene. Fixed simulation steps preserve controller transitions,
        // layer mixing, root motion and modifiers, independently of build/render wall time.
        constexpr double step  = 1.0 / 60.0;
        const auto       whole = uint64_t(std::floor(time / step));
        for (uint64_t i = 0; i < whole; ++i)
            scene.UpdateAnimations(step);
        scene.UpdateAnimations(time - double(whole) * step);
        scene.UpdateMaterialParameterDrivers();
    }
    void Render(Project& project, const Options& options)
    {
        AssetManagerSpecification spec;
        spec.Target = "vulkan12";
        AssetManager assets(Checked(LooseFileAssetProvider::Open(project.GetProductDirectory())),
                            project.GetRuntimeAssetRegistry(),
                            spec);
        LoadProducts(assets);
        auto scene = Checked(SceneSerializer::DeserializeChecked(options.Scene));
        scene->SetPrefabAssetManager(&assets);
        scene->GetRenderWorld().EnvironmentRoot = project.GetConfig().RootDirectory;
        scene->OnViewportResize(options.Size.x, options.Size.y);
        scene->GetRenderWorld().Settings.Piplinestate.EnableGridShader = false;
        Sample(*scene, assets, options.Time);
        auto view = MakeRuntimeView(*scene, options.Size);
        if (!view)
            throw std::runtime_error("Scene has no primary camera");
        if (!scene->GetEnvironmentSettings().Map.empty() &&
            !fs::is_regular_file(project.GetConfig().RootDirectory / scene->GetEnvironmentSettings().Map))
            throw std::runtime_error("Environment map does not exist");
        view->Time          = float(options.Time);
        auto       device   = RH::InitializeDevice(RH::Backend::Vulkan);
        auto       renderer = Renderer::Acquire(device);
        ViewTarget target(device, 1);
        const auto map = scene->GetEnvironmentSettings().Map;
        if (!map.empty())
        {
            const auto path        = project.GetConfig().RootDirectory / map;
            const auto environment = renderer->PrepareEnvironment(path);
            if (!environment)
                throw std::runtime_error("Cannot prepare environment map: " + path.string());
            view->EnvironmentMaps = {environment->Maps.IrradianceMap, environment->Maps.RadianceMap, path};
            view->BrdfLUT         = environment->BrdfLUT;
        }
        // Interactive rendering may omit unavailable meshes or fall back to Standard. A golden
        // render must fail instead of silently capturing those temporary/failure states.
        view->Assets = renderer->AcquireAssetCache(assets);
        scene->GetRenderWorld().Synchronize();
        for (const auto& draw : scene->GetRenderWorld().CollectDraws())
        {
            if (!view->Assets->GetMesh(draw.Mesh))
                throw std::runtime_error("Cannot upload scene mesh on " + std::to_string(uint64_t(draw.EntityID)));
            const auto* bindings = view->Assets->GetMaterialBindings(draw.Material);
            const auto  expected =
                draw.Material.IsValid() ?
                    MakeGpuMaterial(Checked(assets.Load<MaterialDataAsset>(draw.Material))->GetData()).Shader :
                    StandardShaderID();
            if (!bindings || bindings->Shader != expected || !view->Assets->GetShader(expected))
                throw std::runtime_error("Cannot prepare scene material " + draw.Material.ToString());
        }
        const auto result = renderer->Render(scene->GetRenderWorld(), *view, target);
        if (!result.Complete)
            throw std::runtime_error("Scene render incomplete (unresolved or failed render assets)");
        const auto   image    = target.GetColors().at(0);
        const size_t rowBytes = size_t(options.Size.x) * 4, bytes = rowBytes * options.Size.y;
        auto buffer   = device->CreateBuffer({bytes, RH::BufferUsage::CopyDestination, RH::MemoryUsage::Readback});
        auto commands = device->CreateCommandList();
        commands->CopyTextureToBuffer(image, {}, buffer);
        device->Submit(commands);
        device->WaitIdle();
        auto pixels = buffer->Read(0, bytes);
        if (device->GetCoordinateConventions().Origin == RH::FramebufferOrigin::LowerLeft)
            for (uint32_t y = 0; y < options.Size.y / 2; ++y)
                std::swap_ranges(pixels.begin() + y * rowBytes,
                                 pixels.begin() + (y + 1) * rowBytes,
                                 pixels.begin() + (options.Size.y - 1 - y) * rowBytes);
        // The runtime display target already contains tone-mapped, sRGB-encoded RGBA8.
        png_image png {};
        png.version = PNG_IMAGE_VERSION;
        png.width   = options.Size.x;
        png.height  = options.Size.y;
        png.format  = PNG_FORMAT_RGBA;
        if (!png_image_write_to_file(&png, options.Output.string().c_str(), 0, pixels.data(), 0, nullptr))
        {
            const std::string error = png.message;
            png_image_free(&png);
            throw std::runtime_error("PNG output: " + error);
        }
        png_image_free(&png);
    }
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Kans::Log::Init();
        const auto          options = Parse(argc, argv);
        Kans::ProjectConfig config;
        config.Name          = "SceneRender";
        config.RootDirectory = std::filesystem::current_path();
        auto project         = Checked(Kans::Project::Open(config));
        Build(*project);
        Render(*project, options);
        Kans::RenderHardware::ShutdownDevice();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SceneRender: " << error.what() << '\n';
        Kans::RenderHardware::ShutdownDevice();
        return 1;
    }
}
