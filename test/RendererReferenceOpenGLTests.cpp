#include <glad/glad.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <gtest/gtest.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Importer/ShaderImporter.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/Renderer/Features/Editor/EditorRenderFeatures.h"
#include "Kans3D/Renderer/MaterialPreviewRenderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/Resource/InternalShader.h"
#include "Kans3D/Renderer/Resource/MaterialPass.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/SceneSerializer.h"
#include "Kans3D/Scene/SceneSnapshot.h"
#include "GpuTestContext.h"
#include "PrefabRenderTestUtilities.h"
#include "ReferenceImage.h"

namespace
{
    using namespace Kans;
    using namespace Kans::Test;
    using namespace Kans::Test;
    namespace fs = std::filesystem;

    template<typename T>
    T Value(std::variant<T, AssetError> value)
    {
        if (const auto* error = std::get_if<AssetError>(&value))
            throw std::runtime_error(error->Message);
        return std::get<T>(std::move(value));
    }
    class ReferenceProvider final : public IAssetProvider
    {
    public:
        std::map<std::string, std::vector<std::byte>>    Files;
        std::variant<std::vector<std::byte>, AssetError> Read(const ProductRecord& record) const override
        {
            return Files.at(record.Location.RelativeFile.ToString());
        }
    };

    // Each backend parameter owns its context and environment. CTest runs these serially.
    class RendererReferenceGL : public testing::TestWithParam<GpuTestBackend>
    {
    protected:
        static constexpr uint32_t                     Size = 128;
        std::unique_ptr<GpuTestContext>               Context;
        fs::path                                      Directory;
        Ref<ReferenceProvider>                        Provider;
        SourceAssetID                                 Source;
        uint64_t                                      NextID = 0;
        std::vector<ProductRecord>                    Records;
        Scope<AssetManager>                           Assets;
        Ref<Scene>                                    Logic;
        Scope<RenderScene>                            Render;
        Scope<SceneRenderer>                          Renderer;
        RenderHardware::Backend                       PreviousBackend;
        SceneInfo                                     Info {};
        AssetID                                       Albedo, Normal, Textured, Emissive, Toon, ToonShader;
        ProductModelComponent                         Sphere, Quad, EmissionSphere, ToonSphere;
        std::vector<AssetID>                          Models, Meshes, Materials, Textures;
        bool                                          SpriteInitialized     = false;
        bool                                          FileSystemInitialized = false;

        AssetID Add(AssetType type, std::vector<std::byte> bytes)
        {
            const AssetID id {Source, ++NextID};
            ProductRecord record;
            record.Metadata.assetID       = id;
            record.Metadata.Type          = type;
            record.Location.RelativeFile  = AssetPath("reference-" + std::to_string(NextID));
            record.Location.ByteSize      = bytes.size();
            record.Location.ContentDigest = ComputeContentDigest(bytes);
            Provider->Files.emplace(record.Location.RelativeFile.ToString(), std::move(bytes));
            Records.push_back(record);
            return id;
        }
        AssetID Material(MaterialSource source, const ShaderData* shader = nullptr)
        {
            const auto id =
                Add(AssetType::Material, Value(EncodeMaterialProduct(Value(ResolveMaterialSource(source, shader)))));
            Materials.push_back(id);
            return id;
        }
        AssetID Color(glm::vec4 color, MaterialAlphaMode mode = MaterialAlphaMode::Opaque, bool back = false)
        {
            MaterialSource source;
            source.Parameters["BaseColor"] = {color.r, color.g, color.b, color.a};
            source.Parameters["Roughness"] = {0.6};
            source.Render.AlphaMode        = mode;
            source.Render.DoubleSided      = back;
            return Material(source);
        }
        TextureData Pattern(bool normal)
        {
            TextureData data;
            data.Width = data.Height = 16;
            data.ColorSpace          = normal ? TextureColorSpace::Linear : TextureColorSpace::SRGB;
            data.Usage               = normal ? TextureUsage::Normal : TextureUsage::Color;
            data.Origin              = TextureOrigin::TopLeft;
            data.Mips.resize(1);
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                {
                    // Unequal quadrants + off-centre mark distinguish Y orientation,
                    // sRGB decode, alpha cutoff and tangent-space normal direction.
                    std::array<uint8_t, 4> pixel;
                    if (normal)
                        pixel = {uint8_t(x < 5 ? 190 : 100), uint8_t(y < 9 ? 160 : 80), 235, 255};
                    else
                        pixel = {uint8_t(y < 6 ? 220 : 45),
                                 uint8_t(x < 10 ? 90 : 205),
                                 uint8_t((x < 5 && y > 9) ? 240 : 55),
                                 uint8_t((x + 2 * y) % 9 < 4 ? 64 : 230)};
                    for (auto channel : pixel)
                        data.Mips[0].Pixels.push_back(std::byte(channel));
                }
            return Value(GenerateTextureMips(data));
        }
        AssetID Texture(TextureData data)
        {
            const auto id = Add(AssetType::Texture, Value(EncodeTextureProduct(data)));
            Textures.push_back(id);
            return id;
        }
        MeshData QuadData(bool back = false)
        {
            MeshData mesh;
            for (const auto& xy : {glm::vec2 {-1, -1}, {1, -1}, {1, 1}, {-1, 1}})
            {
                MeshVertexData v;
                v.Position = {xy.x, xy.y, 0};
                v.Normal   = {0, 0, 1};
                v.UV       = {(xy.x + 1) / 2, (xy.y + 1) / 2};
                v.Tangent  = {1, 0, 0, 1};
                mesh.Vertices.push_back(v);
            }
            mesh.Indices = back ? std::vector<uint32_t> {2, 1, 0, 0, 3, 2} : std::vector<uint32_t> {0, 1, 2, 2, 3, 0};
            return mesh;
        }
        MeshData SphereData()
        {
            // UV sphere with explicit tangent handedness, built as a Mesh Product
            // and uploaded by RenderAssetCache, never a test-only GL draw.
            constexpr unsigned longitude = 32, latitude = 16;
            constexpr float    pi = 3.14159265358979323846f;
            MeshData           mesh;
            for (unsigned y = 0; y <= latitude; ++y)
                for (unsigned x = 0; x <= longitude; ++x)
                {
                    const float    theta = float(y) / latitude * pi, phi = float(x) / longitude * 2 * pi;
                    MeshVertexData v;
                    v.Position =
                        v.Normal = {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
                    v.UV         = {float(x) / longitude, 1 - float(y) / latitude};
                    v.Tangent    = {-std::sin(phi), 0, std::cos(phi), -1};
                    mesh.Vertices.push_back(v);
                }
            for (unsigned y = 0; y < latitude; ++y)
                for (unsigned x = 0; x < longitude; ++x)
                {
                    const unsigned a = y * (longitude + 1) + x, b = a + longitude + 1;
                    mesh.Indices.insert(mesh.Indices.end(), {a, a + 1, b, a + 1, b + 1, b});
                }
            return mesh;
        }
        ProductModelComponent Model(MeshData mesh, AssetID material)
        {
            mesh.Material = material;
            BuildSingleSection(mesh);
            const auto id = Add(AssetType::StaticMesh, Value(EncodeMeshProduct(mesh)));
            Meshes.push_back(id);
            ModelData model;
            model.Nodes.push_back({"Root", -1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {id}});
            ProductModelComponent component;
            component.Model = Add(AssetType::Model, Value(EncodeModelProduct(model)));
            Models.push_back(component.Model);
            return component;
        }
        glm::mat4 Transform(glm::vec3 position, glm::vec3 scale, float angle = 0)
        {
            return glm::translate(glm::mat4(1), position) * glm::rotate(glm::mat4(1), angle, {0, 0, 1}) *
                   glm::scale(glm::mat4(1), scale);
        }
        void Ready()
        {
            auto registry = Value(AssetRegistry::FromProducts(Records));
            Value(Assets->SetRegistry(CreateRef<const AssetRegistry>(std::move(registry))));
            auto&      cache    = *Renderer->GetAssetCache();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            bool       ready    = false;
            do
            {
                cache.BeginFrame();
                ready = true;
                for (auto id : Models)
                    ready &= cache.GetModel(id) != nullptr;
                for (auto id : Meshes)
                    ready &= cache.GetMesh(id) != nullptr;
                for (auto id : Materials)
                    ready &= cache.GetMaterialBindings(id) != nullptr;
                for (auto id : Textures)
                    ready &= cache.GetTexture(id) != nullptr;
                ready &= cache.GetShader(ToonShader) != nullptr;
                if (!ready)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (!ready && std::chrono::steady_clock::now() < deadline);
            ASSERT_TRUE(ready) << "Real asset/cache resources did not become ready";
            ASSERT_EQ(cache.GetMaterialBindings(Toon)->Shader, ToonShader) << "Toon must not fall back to Standard";
        }
        void WriteEnvironment()
        {
            // Raw Radiance RGBE, 8x4; unequal bright lobes test cubemap orientation
            // and roughness mip sampling. Small precompute targets keep Mesa fast.
            std::ofstream out(Directory / "environment.hdr", std::ios::binary);
            out << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 8; ++x)
                {
                    const std::array<uint8_t, 4> rgbe {
                        uint8_t(45 + x * 20), uint8_t(40 + y * 35), uint8_t(x < 3 ? 150 : 65), 129};
                    out.write(reinterpret_cast<const char*>(rgbe.data()), rgbe.size());
                }
            ASSERT_TRUE(out.good());
        }
        void SetUpResources()
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Context = std::make_unique<GpuTestContext>(GetParam());
            ASSERT_NO_FATAL_FAILURE(Context->SetUp());
            PreviousBackend = RenderHardware::GetBackend();
            Directory = fs::temp_directory_path() / ("kans-render-reference-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Directory / "stage");
            Provider = CreateRef<ReferenceProvider>();
            Source   = SourceAssetID::Generate();
            Assets = CreateScope<AssetManager>(Provider,
                                               CreateRef<const AssetRegistry>(Value(AssetRegistry::FromProducts({}))));
            Logic  = CreateRef<Scene>();
            Render = CreateScope<RenderScene>(Logic);
            Render->OnViewportResize(Size, Size);
            SceneRendererSpecification spec;
            Renderer              = CreateScope<SceneRenderer>(Render.get(), spec);
            RegisterDefaultEditorFeatures(Renderer->GetFeatures());
            Renderer->SetAssetManager(Assets.get());
            Info.sceneCamera.Position   = {0, 0, 4};
            Info.sceneCamera.viewMatrix = glm::lookAt(Info.sceneCamera.Position, glm::vec3(0), glm::vec3(0, 1, 0));
            Info.sceneCamera.camera     = Camera(glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 20.0f));
            Info.pointLight.Position    = {-2, 3, 4};
            Info.pointLight.Ambient_Intensity  = {30, 24, 20};
            Info.pointLight.Diffuse_Intensity  = {1, 1, 1};
            Info.pointLight.Specular_Intensity = {1, 1, 1};
            ASSERT_NO_FATAL_FAILURE(WriteEnvironment());
            Render->PrepareEnvironment(Directory / "environment.hdr");
            Renderer->PrepareEnvironment({32, 8, 32, 32}); // Same production GLSL and integration sample counts.
            ASSERT_TRUE(Renderer->GetEnvironment().IrradianceMap);
            ASSERT_TRUE(Renderer->GetEnvironment().RadianceMap);
            ASSERT_TRUE(Renderer->GetBrdfLUT());
            Albedo = Texture(Pattern(false));
            Normal = Texture(Pattern(true));
            MaterialSource standard;
            standard.Parameters = {{"BaseColor", {0.9, 0.75, 0.6, 1}},
                                   {"Metallic", {0.35}},
                                   {"Roughness", {0.42}},
                                   {"UVTiling", {2.3, 1.7}},
                                   {"UVOffset", {0.13, 0.07}}};
            standard.Textures   = {{"BaseColorMap", Albedo}, {"NormalMap", Normal}};
            Textured            = Material(standard);
            Sphere              = Model(SphereData(), Textured);
            Quad                = Model(QuadData(), Textured);
            MaterialSource emission;
            emission.Parameters              = {{"BaseColor", {0.02, 0.03, 0.04, 1}},
                                                {"EmissiveColor", {0.9, 0.12, 0.5}},
                                                {"EmissiveIntensity", {3.5}}};
            emission.Textures["EmissiveMap"] = Albedo;
            Emissive                         = Material(emission);
            EmissionSphere                   = Model(SphereData(), Emissive);
            // Import the actual editor sample, including its shaderprog defaults.
            ImportContext context;
            context.SourceID         = SourceAssetID::Generate();
            context.AssetRoot        = fs::path(KANS_TEST_EDITOR_ASSET_DIR);
            context.SourceFile       = context.AssetRoot / "shaders/Toon/Toon.shaderprog";
            context.StagingDirectory = Directory / "stage";
            context.Target           = "vulkan12";
            const auto inputs        = Value(ShaderImporter {}.DiscoverInputs(context));
            for (const auto& file : inputs.SourceFiles)
                context.SourceInputs.emplace(file, SourceAssetID::Generate());
            const auto imported = Value(ShaderImporter {}.Import(context));
            ASSERT_EQ(imported.Products.size(), 1u);
            auto bytes  = Value(ReadProductFile(context.StagingDirectory / imported.Products[0].StagedFile));
            auto shader = Value(DecodeShaderProduct(bytes.data(), bytes.size()));
            ToonShader  = Add(AssetType::Shader, std::move(bytes));
            MaterialSource toon;
            toon.Shader               = ToonShader;
            toon.Parameters           = {{"BaseColor", {0.3, 0.8, 0.65, 1}},
                                         {"ShadowColor", {0.2, 0.09, 0.4, 1}},
                                         {"ShadowThreshold", {0.75}},
                                         {"RimColor", {1, 0.2, 0.08, 0.7}}};
            toon.Textures["u_Albedo"] = Albedo;
            Toon                      = Material(toon, &shader);
            ToonSphere                = Model(SphereData(), Toon);
            ASSERT_NO_FATAL_FAILURE(Ready());
            if (GetParam() == GpuTestBackend::OpenGL)
                EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
        void TearDown() override
        {
            Renderer.reset();
            Render.reset();
            Logic.reset();
            Assets.reset();
            Provider.reset();
            if (SpriteInitialized)
            {
                Renderer2D::Shutdown();
            }
            if (FileSystemInitialized)
                KansFileSystem::ShutDown();
            if (!Directory.empty())
                fs::remove_all(Directory);
            Context.reset();
            RenderHardware::SetBackend(PreviousBackend);
        }
        void SetUp() override
        {
            ASSERT_NO_FATAL_FAILURE(SetUpResources());
            ASSERT_TRUE(Renderer);
            RecordProperty("renderer",
                           GetParam() == GpuTestBackend::OpenGL ?
                               reinterpret_cast<const char*>(glGetString(GL_RENDERER)) :
                               "lavapipe");
            Renderer->SetViewMode(SceneViewMode::Lit);
        }
        RgbaImage Read(const RenderHardware::Handle<RenderHardware::Texture>& texture)
        {
            RgbaImage image {Size, Size, std::vector<uint8_t>(Size * Size * 4)};
            auto      device   = Renderer->GetDevice();
            auto      buffer   = device->CreateBuffer({image.Pixels.size(),
                                                       RenderHardware::BufferUsage::CopyDestination,
                                                       RenderHardware::MemoryUsage::Readback});
            auto      commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(texture, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            image.Pixels = buffer->Read(0, image.Pixels.size());
            if (device->GetCoordinateConventions().Origin == RenderHardware::FramebufferOrigin::LowerLeft)
                for (unsigned y = 0; y < Size / 2; ++y)
                    std::swap_ranges(image.Pixels.begin() + y * Size * 4,
                                     image.Pixels.begin() + (y + 1) * Size * 4,
                                     image.Pixels.begin() + (Size - 1 - y) * Size * 4);
            return image;
        }
        void Compare(const char* name, const RgbaImage& image, ImageTolerance tolerance)
        {
            WritePng(fs::path(KANS_TEST_OUTPUT_DIR) / "phase2d" /
                         (GetParam() == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan") / (std::string(name) + ".png"),
                     image);
            if (GetParam() == GpuTestBackend::OpenGL)
            {
                // Exact pre-2d oracles. Wireframe/sky were audited against the saved phase-one library;
                // their <=1-byte differences from the older PNGs already existed before this task.
                const std::pair<const char*, const char*> unchanged[] = {
                    {"debug-base-color", "03aa1f06506ba842db0743d478f3354e78b4d7a5ea27610a2d6d7b0206312aa4"},
                    {"debug-normals", "0281fd1ef71c99837b1e79f1fc850010ad72ff9ec17c77da5ec8e20c11ac9750"},
                    {"debug-wireframe", "38b0fea0e64248d077bc9e80f256bb11861279f4697763a896f1c0f239905c39"},
                    {"grid", "f6730a0d1a3f7d1508c853bb268c542a8ff8a108a50d0a867070168053ffe3f1"},
                    {"skybox", "9ff26345a562e96a39db980386b33ff417bfecc759ea55020dbc18b3e8743399"},
                    {"renderer2d-sprite", "b5d42010d764ec7af44e4cb0e5b4b30a3dd401a37b16f0ebf4ab376d6397b192"}};
                for (const auto& [scene, digest] : unchanged)
                    if (std::string(name) == scene)
                    {
                        std::vector<std::byte> bytes(image.Pixels.size());
                        std::memcpy(bytes.data(), image.Pixels.data(), bytes.size());
                        EXPECT_EQ(ComputeContentDigest(bytes), digest);
                    }
            }
            if (GetParam() == GpuTestBackend::Vulkan)
            {
                // Native SPIR-V evaluation and finite-precision filtered sampling
                // differ at a few high-gradient sphere texels. These are the exact
                // measured maxima; coverage/outliers remain strict (fraction zero).
                // Phase 2d HDR-only maxima were remeasured from actual RGBA16F captures;
                // analytic ACES+sRGB checks every RGB pixel independently within one byte.
                // See test/references/README.md for radiances and unchanged phase-3b bounds.
                const std::pair<const char*, uint8_t> measured[] = {{"standard-ibl-point", 7},
                                                                    {"toon-imported", 7},
                                                                    {"debug-base-color", 7},
                                                                    {"debug-normals", 6},
                                                                    {"preview-sphere", 18},
                                                                    {"emission", 3}};
                for (const auto& [scene, maximum] : measured)
                    if (std::string(name) == scene)
                        tolerance.MaxChannelDifference = maximum;
            }
            const auto result = CompareReference(image,
                                                 fs::path(KANS_TEST_REFERENCE_DIR) / (std::string(name) + ".png"),
                                                 fs::path(KANS_TEST_OUTPUT_DIR) /
                                                     (GetParam() == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan"),
                                                 tolerance);
            EXPECT_TRUE(result.Passed) << result.Message;
            if (GetParam() == GpuTestBackend::OpenGL)
                EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
        }
        void AuditHDR(const RenderHardware::Handle<RenderHardware::Texture>& hdr,
                      const char*                                            name,
                      const RgbaImage&                                       display,
                      float                                                  exposure,
                      bool                                                   assertCurve)
        {
            // Capture actual pre-curve radiance, not reconstructed values from the final PNG.
            auto device = Renderer->GetDevice();
            auto buffer = device->CreateBuffer(
                {Size * Size * 8, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(hdr, {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto bytes = buffer->Read(0, Size * Size * 8);
            if (device->GetCoordinateConventions().Origin == RenderHardware::FramebufferOrigin::LowerLeft)
                for (unsigned y = 0; y < Size / 2; ++y)
                    std::swap_ranges(bytes.begin() + y * Size * 8,
                                     bytes.begin() + (y + 1) * Size * 8,
                                     bytes.begin() + (Size - 1 - y) * Size * 8);
            const auto path = fs::path(KANS_TEST_OUTPUT_DIR) / "phase2d" /
                              (GetParam() == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan") /
                              (std::string(name) + ".hdr.rgba16f");
            fs::create_directories(path.parent_path());
            std::ofstream dump(path, std::ios::binary);
            dump.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!assertCurve)
                return; // Selection's subsequent display compositor has its own strict reference comparison.
            unsigned maximum = 0, outside = 0;
            for (unsigned pixel = 0; pixel < Size * Size; ++pixel)
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    uint16_t half;
                    std::memcpy(&half, bytes.data() + pixel * 8 + channel * 2, 2);
                    const int      expected   = FilmicByte(glm::unpackHalf1x16(half), exposure);
                    const unsigned difference = unsigned(std::abs(int(display.Pixels[pixel * 4 + channel]) - expected));
                    maximum                   = std::max(maximum, difference);
                    outside += difference > 1;
                }
            EXPECT_EQ(outside, 0u) << name << " maximum analytic post-pass channel delta=" << maximum;
        }
        void CompareScene(const char* name, ImageTolerance tolerance)
        {
            const auto        image = Read(Renderer->GetRenderTarget().Colors[0].Target);
            const std::string scene(name);
            if (scene == "standard-ibl-point" || scene == "alpha-depth-sort-backfaces" || scene == "emission" ||
                scene == "toon-imported" || scene == "selection-outline")
                AuditHDR(Renderer->GetHDRTarget(),
                         name,
                         image,
                         Info.sceneCamera.camera.GetExposure(),
                         scene != "selection-outline");
            Compare(name, image, tolerance);
        }
        ProductModelComponent
        Probe(const std::string& body, ShaderOutputSpace space, MaterialAlphaMode alpha = MaterialAlphaMode::Opaque)
        {
            auto       shader = Value(CompileEmbeddedShaderProgram(
                "KansShaderProgram 1\nvertex=p.vert\nfragment=p.frag\noutput_space=" +
                    std::string(space == ShaderOutputSpace::LinearHDR ? "linear_hdr" : "display") + "\n",
                {{"p.vert", R"(#version 450
#include <Kans3D/Engine.glsl>
#include <Kans3D/VertexInput.glsl>
void main(){gl_Position=u_Frame.ViewProjection*u_Object.Transform*vec4(a_Position,1);}
)"},
                 {"p.frag",
                  "#version 450\n#include <Kans3D/Lighting.glsl>\nlayout(location=0) out vec4 o_Color;\nvoid main(){" +
                      body + "}\n"}}));
            const auto id =
                Add(AssetType::Shader, Value(EncodeShaderProduct(shader.Code, {}, {}, {}, {}, {}, shader.OutputSpace)));
            MaterialSource material;
            material.Shader           = id;
            material.Render.AlphaMode = alpha;
            auto model                = Model(QuadData(), Material(material, &shader));
            Ready();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            auto*      cache    = Renderer->GetAssetCache();
            while ((!cache->GetShader(id) || cache->GetMaterialBindings(Materials.back())->Shader != id) &&
                   std::chrono::steady_clock::now() < deadline)
            {
                cache->BeginFrame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!cache->GetShader(id) || cache->GetMaterialBindings(Materials.back())->Shader != id)
                throw std::runtime_error("Probe must use its published shader, not fallback Standard");
            return model;
        }
        std::array<uint8_t, 4> Centre()
        {
            const auto image  = Read(Renderer->GetRenderTarget().Colors[0].Target);
            const auto offset = (Size / 2 * Size + Size / 2) * 4;
            return {image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2], image.Pixels[offset + 3]};
        }
        static int FilmicByte(double linear, double exposure)
        {
            const double x    = std::max(0.0, linear) * exposure;
            const double y    = std::clamp(x * (2.51 * x + .03) / (x * (2.43 * x + .59) + .14), 0.0, 1.0);
            const double srgb = y <= .0031308 ? 12.92 * y : 1.055 * std::pow(y, 1.0 / 2.4) - .055;
            return int(std::lround(srgb * 255));
        }
        void RenderProbe(const ProductModelComponent& model)
        {
            Renderer->BeginScene(Info);
            SubmitLegacyOracle(*Renderer, model, glm::mat4(1));
            Renderer->EndScene();
        }
        void TexturedScene()
        {
            Renderer->BeginScene(Info);
            SubmitLegacyOracle(*Renderer, Sphere, Transform({-0.38f, 0.22f, 0.15f}, {0.72f, 0.72f, 0.72f}, 0.23f));
            SubmitLegacyOracle(*Renderer, Quad, Transform({0.57f, -0.46f, -0.35f}, {0.48f, 0.35f, 1}, -0.18f));
            Renderer->EndScene();
            ASSERT_EQ(Renderer->GetStats().DrawCalls, 2u);
        }
    };

    // Lit images allow 2/255 for half-float IBL/filtering and LDR quantization.
    // NO outlier pixels are allowed: edges, orientation and coverage remain strict.
    // Unlit geometry/debug images need only 1/255 rounding tolerance. These are
    // fixed explicit budgets, not inferred or increased after reference comparison.
    TEST_P(RendererReferenceGL, StandardTexturedIblPointLight)
    {
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("standard-ibl-point", {2, 0.0});
    }
    TEST_P(RendererReferenceGL, AlphaModesDepthSortingAndBackFaces)
    {
        const auto     opaque = Model(QuadData(), Color({0.12f, 0.32f, 0.8f, 1}));
        MaterialSource mask;
        mask.Render.AlphaMode         = MaterialAlphaMode::Mask;
        mask.Render.AlphaCutoff       = 0.5f;
        mask.Parameters["BaseColor"]  = {1, 0.85, 0.2, 1};
        mask.Textures["BaseColorMap"] = Albedo;
        const auto masked             = Model(QuadData(), Material(mask));
        const auto near   = Model(QuadData(), Color({0.95f, 0.08f, 0.15f, 0.45f}, MaterialAlphaMode::Blend));
        const auto far    = Model(QuadData(), Color({0.05f, 0.9f, 0.2f, 0.65f}, MaterialAlphaMode::Blend));
        const auto back   = Model(QuadData(true), Color({0.8f, 0.3f, 0.9f, 1}, MaterialAlphaMode::Opaque, true));
        const auto culled = Model(QuadData(true), Color({1, 1, 1, 1}));
        ASSERT_NO_FATAL_FAILURE(Ready());
        Renderer->BeginScene(Info);
        // Intentionally submit near before far and opaque last. Renderer must
        // sort across models, test opaque depth, and avoid writing blend depth.
        SubmitLegacyOracle(*Renderer, near, Transform({0.1f, -0.15f, 0.5f}, {0.62f, 0.56f, 1}));
        SubmitLegacyOracle(*Renderer, far, Transform({0.37f, 0.03f, 0.1f}, {0.67f, 0.6f, 1}));
        SubmitLegacyOracle(*Renderer, masked, Transform({-0.58f, 0.4f, 0.65f}, {0.6f, 0.5f, 1}));
        SubmitLegacyOracle(*Renderer, back, Transform({-0.83f, -0.75f, 0.3f}, {0.32f, 0.22f, 1}));
        SubmitLegacyOracle(*Renderer, culled, Transform({0.9f, 0.92f, 0.2f}, {0.28f, 0.18f, 1}));
        SubmitLegacyOracle(*Renderer, opaque, Transform({-0.1f, 0.1f, 0.35f}, {0.4f, 0.28f, 1}));
        Renderer->EndScene();
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 6u);
        CompareScene("alpha-depth-sort-backfaces", {2, 0.0});
    }
    TEST_P(RendererReferenceGL, EmissionTextureAndToneMapping)
    {
        Renderer->BeginScene(Info);
        SubmitLegacyOracle(*Renderer, EmissionSphere, Transform({0.25f, 0.28f, 0}, {0.8f, 0.8f, 0.8f}, -0.3f));
        Renderer->EndScene();
        CompareScene("emission", {2, 0.0});
    }
    TEST_P(RendererReferenceGL, ImportedToonMaterial)
    {
        Renderer->BeginScene(Info);
        SubmitLegacyOracle(*Renderer, ToonSphere, Transform({-0.22f, 0.13f, 0}, {0.93f, 0.93f, 0.93f}, 0.3f));
        Renderer->EndScene();
        CompareScene("toon-imported", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, DebugBaseColor)
    {
        Renderer->SetViewMode(SceneViewMode::BaseColor);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("debug-base-color", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, DebugNormals)
    {
        Renderer->SetViewMode(SceneViewMode::Normals);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        CompareScene("debug-normals", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, DebugWireframe)
    {
        Renderer->SetViewMode(SceneViewMode::Wireframe);
        ASSERT_NO_FATAL_FAILURE(TexturedScene());
        // Raster coverage is the regression being tested, so no edge exclusions.
        CompareScene("debug-wireframe", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, SelectionOutlineAndNumericSurfacePicker)
    {
        const auto transform = Transform({-0.3f, 0.23f, 0.2f}, {0.55f, 0.4f, 1}, 0.15f);
        Renderer->BeginScene(Info);
        SubmitLegacyOracle(*Renderer, Quad, transform);
        HighlightLegacyOracle(*Renderer, *Logic, {{Quad, transform}});
        Renderer->EndScene();
        CompareScene("selection-outline", {2, 0.0});
        auto pickScene = CreateRef<Scene>();
        auto nearRoot  = pickScene->CreateEntityWithID(UUID(37), "Near prefab", false);
        nearRoot.AddComponent<PrefabInstanceComponent>().Prefab = Quad.Model;
        ExactTransform(nearRoot, transform);
        auto farRoot = pickScene->CreateEntityWithID(UUID(91), "Far prefab", false);
        farRoot.AddComponent<PrefabInstanceComponent>().Prefab = Quad.Model;
        ExactTransform(farRoot, Transform({-0.3f, 0.23f, -0.5f}, {0.7f, 0.6f, 1}));
        pickScene->UpdatePrefabs(*Assets);
        const auto nearPart = pickScene->GetMeshEntities(nearRoot).front().GetUUID();
        const auto farPart  = pickScene->GetMeshEntities(farRoot).front().GetUUID();
        ASSERT_EQ(pickScene->GetMeshEntities().size(), 2u);
        const auto       vp = Info.sceneCamera.camera.GetProjectionMatrix() * Info.sceneCamera.viewMatrix;
        const glm::uvec2 pixel {52, 73};
        const auto       hit = Renderer->PickSceneSurface(*pickScene, vp, pixel);
        ASSERT_EQ(hit.EntityID, nearPart);
        EXPECT_EQ(hit.InstanceRoot, UUID(37));
        ASSERT_TRUE(hit.Position);
        // Pixel centres unproject onto the known z=0.2 plane. This detects a
        // Vulkan depth-range mismatch as well as Y-origin/off-by-one errors.
        const float distance = 4.0f - 0.2f;
        const float extent   = distance * std::tan(glm::radians(45.0f) / 2);
        EXPECT_NEAR(hit.Position->x, ((pixel.x + 0.5f) / Size * 2 - 1) * extent, 0.0001f);
        EXPECT_NEAR(hit.Position->y, ((pixel.y + 0.5f) / Size * 2 - 1) * extent, 0.0001f);
        EXPECT_NEAR(hit.Position->z, 0.2f, 0.0001f);
        const auto far = Renderer->PickSceneSurface(*pickScene, vp, {77, 80});
        EXPECT_EQ(far.InstanceRoot, UUID(91));
        ASSERT_EQ(far.EntityID, farPart); // Exposed part of the larger, farther quad.
        ASSERT_TRUE(far.Position);
        EXPECT_NEAR(far.Position->z, -0.5f, 0.0001f);
        const auto empty = Renderer->PickSceneSurface(*pickScene, vp, {2, 2});
        EXPECT_EQ(empty.EntityID, UUID(0));
        EXPECT_FALSE(empty.Position);
        if (GetParam() == GpuTestBackend::OpenGL)
            EXPECT_EQ(glGetError(), GLenum(GL_NO_ERROR));
    }
    TEST_P(RendererReferenceGL, MaterialPreviewSphere)
    {
        MaterialPreviewRenderer preview(*Renderer);
        MaterialPreviewOptions  options;
        options.Size         = {Size, Size};
        options.Rotation     = {0.37f, -0.23f};
        const auto thumbnail = preview.RenderThumbnail(Textured, options);
        ASSERT_TRUE(thumbnail);
        AuditHDR(preview.GetHDRTarget(), "preview-sphere", {Size, Size, thumbnail->Pixels}, options.Exposure, true);
        Compare("preview-sphere", {Size, Size, thumbnail->Pixels}, {2, 0.0});
    }
    TEST_P(RendererReferenceGL, Grid)
    {
        SceneInfo info              = Info;
        info.sceneCamera.Position   = {2, 2.2f, 3};
        info.sceneCamera.viewMatrix = glm::lookAt(info.sceneCamera.Position, glm::vec3(0), glm::vec3(0, 1, 0));
        Renderer->BeginScene(info);
        Renderer->SubmitGrid(0.035f, 12.0f, Transform({0.1f, -0.3f, 0}, {2.0f, 1, 2.0f}), {true});
        Renderer->EndScene();
        CompareScene("grid", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, Skybox)
    {
        SceneInfo info = Info;
        info.sceneCamera.viewMatrix =
            glm::lookAt(info.sceneCamera.Position, glm::vec3(0.8f, 0.4f, 0), glm::vec3(0, 1, 0));
        Renderer->BeginScene(info);
        Renderer->SubmitSkybox(Renderer->GetSkybox());
        Renderer->EndScene();
        CompareScene("skybox", {2, 0.0});
    }
    TEST_P(RendererReferenceGL, Renderer2DSprite)
    {
        // Renderer2D locates its real TextureShader through the filesystem service.
        // Only a temp ini is written; editor resources remain read-only.
        if (!SpriteInitialized)
        {
            std::ofstream config(Directory / "filesystem.ini");
            config << "ShaderDir=" << fs::path(KANS_TEST_SHADER_DIR).parent_path().generic_string() << "/\n";
            config.close();
            ASSERT_TRUE(config.good());
            KansFileSystem::Init(Directory / "filesystem.ini");
            FileSystemInitialized = true;
            Renderer2D::Init();
            SpriteInitialized = true;
        }
        auto texture = Renderer->GetAssetCache()->GetTexture(Albedo);
        ASSERT_TRUE(texture);
        Renderer->BeginScene(Info);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(Info.sceneCamera.camera, Info.sceneCamera.viewMatrix, {true});
        Renderer2D::DrawQuad(
            Transform({-0.3f, 0.25f, 0.2f}, {1.9f, 1.2f, 1}, 0.27f), texture, {0.8f, 1, 0.6f, 0.7f}, 1.5f);
        Renderer2D::EndScene();
        Renderer->EndScene();
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 1u);
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 1u);
        CompareScene("renderer2d-sprite", {1, 0.0});
    }
    TEST_P(RendererReferenceGL, HdrValuesAboveOneAndExposureHaveAnalyticFilmicBytes)
    {
        // Bugs: RGBA8 clamps before the post pass, exposure is ignored, or HDR skips/doubles tone mapping.
        const auto model = Probe("o_Color=vec4(4.0,0.5,0.125,1);", ShaderOutputSpace::LinearHDR);
        EXPECT_EQ(Renderer->GetHDRTarget()->GetDesc().PixelFormat, RenderHardware::Format::RGBA16Float);
        for (float exposure : {.25f, .8f, 2.0f})
        {
            Info.sceneCamera.camera.SetExposure(exposure);
            RenderProbe(model);
            const auto pixel = Centre();
            for (unsigned c = 0; c < 3; ++c)
                EXPECT_NEAR(pixel[c], FilmicByte(std::array<double, 3> {4, .5, .125}[c], exposure), 1) << c;
            EXPECT_EQ(pixel[3], 255);
            // Check the working image too, independently of the final curve.
            auto device = Renderer->GetDevice();
            auto buffer = device->CreateBuffer(
                {Size * Size * 8, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(Renderer->GetHDRTarget(), {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto     bytes = buffer->Read(0, Size * Size * 8);
            uint16_t red;
            std::memcpy(&red, bytes.data() + (Size / 2 * Size + Size / 2) * 8, 2);
            EXPECT_EQ(red, 0x4400u); // IEEE binary16, exactly 4.0. Catches hidden clamping.
        }
    }
    TEST_P(RendererReferenceGL, PointPbrUsesDistinctDiffuseRadianceCountAndSpecularScale)
    {
        // Bugs: four copies, ambient radiance, ignored count/zero-slot NaNs, missing fifth light, unused specular.
        const auto model =
            Probe("o_Color=vec4(KansShadePbr(vec3(0.5),0,1,0,vec3(0,0,1),vec3(0)),1);", ShaderOutputSpace::LinearHDR);
        Info.pointLight  = {};
        Info.pointLights = {{{0, 0, 2}, {4, 8, 12}, {0, 0, 0}, {100, 100, 100}}};
        RenderProbe(model);
        auto             pixel = Centre();
        constexpr double pi    = 3.141592653589793;
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte(.96 * .5 / pi * (c + 1), .8), 1);
        Info.pointLights[0].Specular_Intensity = glm::vec3(4);
        RenderProbe(model);
        pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte((.96 * .5 / pi + .04 / pi / (4.001) * 4) * (c + 1), .8), 1);
        Info.pointLights.clear();
        RenderProbe(model);
        pixel = Centre();
        EXPECT_EQ(pixel, (std::array<uint8_t, 4> {0, 0, 0, 255}));
        Info.pointLights = {{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {100, 100, 100}}};
        RenderProbe(model);
        EXPECT_EQ(Centre(),
                  (std::array<uint8_t, 4> {0, 0, 0, 255})); // A zero-radiance colocated light must remain finite.
        Info.pointLights.clear();
        for (unsigned i = 0; i < 5; ++i)
            Info.pointLights.push_back({{0, 0, 2}, glm::vec3(float(i + 1)), glm::vec3(0), glm::vec3(200)});
        RenderProbe(model);
        pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte(.96 * .5 / pi * 15 / 4, .8), 1);
    }
    TEST_P(RendererReferenceGL, PbrLightCountIgnoresPoisonedUnusedSlots)
    {
        // Bug: loop over the engine limit instead of the live count; zero-radiance padding alone cannot detect this.
        Probe("o_Color=vec4(KansShadePbr(vec3(0.5),0,1,0,vec3(0,0,1),vec3(0)),1);", ShaderOutputSpace::LinearHDR);
        Info.pointLight  = {};
        Info.pointLights = {{{0, 0, 2}, {4, 4, 4}, {0, 0, 0}, {100, 100, 100}}};
        auto frame       = MakeLightingUniforms(Info);
        frame.ViewProjection =
            DeviceProjection(Info.sceneCamera.camera.GetProjectionMatrix() * Info.sceneCamera.viewMatrix,
                             Renderer->GetDevice()->GetCoordinateConventions());
        for (unsigned i = 1; i < EnginePointLightLimit; ++i)
        {
            frame.PointPositions[i] = {0, 0, 2, 1};
            frame.PointRadiance[i]  = {100, 100, 100, 0};
            frame.PointSpecular[i]  = {1, 1, 1, 0};
        }
        using namespace RenderHardware;
        auto device  = Renderer->GetDevice();
        auto uniform = [&](const auto& data) {
            auto buffer = device->CreateBuffer({sizeof(data), BufferUsage::Uniform, MemoryUsage::Upload});
            buffer->Write(0, &data, sizeof(data));
            return buffer;
        };
        MaterialPassData pass;
        pass.Frame              = uniform(frame);
        pass.Object             = uniform(EngineObjectUniforms {});
        pass.Environment        = Renderer->GetMaterialEnvironment();
        pass.EnvironmentSampler = Renderer->GetMaterialEnvironmentSampler();
        pass.ColorFormats       = {Format::RGBA16Float, Format::RGBA8Unorm};
        auto*       cache       = Renderer->GetAssetCache();
        const auto* bindings    = cache->GetMaterialBindings(Materials.back());
        ASSERT_TRUE(bindings);
        const auto* shader = cache->GetShader(bindings->Shader);
        ASSERT_TRUE(shader);
        auto target             = Renderer->GetRenderTarget();
        target.Colors[0].Target = Renderer->GetHDRTarget();
        for (auto& color : target.Colors)
            color.Load = LoadOp::Clear;
        target.Depth.Load = LoadOp::Clear;
        auto commands     = device->CreateCommandList();
        commands->BeginRendering(target);
        commands->SetViewport({0, 0, Size, Size});
        commands->SetScissor({0, 0, Size, Size});
        ASSERT_TRUE(RecordMaterialDraw(*cache, *commands, *shader, *bindings, *cache->GetMesh(Meshes.back()), pass));
        commands->EndRendering();
        ToneMappingPass post;
        post.Record(*device, *commands, Renderer->GetHDRTarget(), {Renderer->GetRenderTarget().Colors[0].Target}, .8f);
        device->Submit(commands);
        const auto pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte(.96 * .5 / 3.141592653589793, .8), 1);
        device->WaitIdle();
    }
    TEST_P(RendererReferenceGL, AppendedCameraExposureAndLightCountAreBoundToShaders)
    {
        // Bug: the post gets exposure but the appended KansFrame tail is misbound or never uploaded.
        const auto model = Probe("o_Color=vec4(KansExposure()/4.0,float(KansPointLightCount())/16.0,0.25,1);",
                                 ShaderOutputSpace::Display);
        Info.sceneCamera.camera.SetExposure(2);
        Info.pointLights.resize(5);
        RenderProbe(model);
        EXPECT_EQ(Centre(), (std::array<uint8_t, 4> {128, 80, 64, 255}));
    }
    TEST_P(RendererReferenceGL, DirectionalPbrReachesShadersWithAmbientAndSpecular)
    {
        // Bugs: drop directional light, use travel direction as to-light, or omit ambient/specular.
        // Black environment isolates analytic direct + ambient terms.
        Renderer = CreateScope<SceneRenderer>(Render.get());
        Renderer->SetAssetManager(Assets.get());
        const auto model =
            Probe("o_Color=vec4(KansShadePbr(vec3(0.5),0,1,1,vec3(0,0,1),vec3(0)),1);", ShaderOutputSpace::LinearHDR);
        Info.pointLight          = {};
        Info.dirLight            = {{0, 0, -4}, {1, 2, 3}, {0, 0, 0}, {.1f, .2f, .3f}};
        Info.HasDirectionalLight = true;
        RenderProbe(model);
        auto pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte((.96 * .5 / 3.141592653589793 + .05) * (c + 1), .8), 1);
        Info.dirLight.Dirction = {0, 0, 4};
        RenderProbe(model);
        pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixel[c], FilmicByte(.05 * (c + 1), .8), 1);
        Info.dirLight.Dirction           = {0, 0, -1};
        Info.dirLight.Specular_Intensity = glm::vec3(4);
        RenderProbe(model);
        pixel = Centre();
        for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(
                pixel[c],
                FilmicByte((.96 * .5 / 3.141592653589793 + .05 + .04 / 3.141592653589793 / 4.001 * 4) * (c + 1), .8),
                1);
    }
    TEST_P(RendererReferenceGL, PublishedV6ProductsBindPrefixAndFourDistinctLights)
    {
        // Bug: accept old products but misbind prefix offsets, duplicate slots, or classify them as HDR.
        auto           bytes  = Value(ReadProductFile(fs::path(KANS_TEST_MULTIPASS_DIR) / "lights-prefix-v6.kshd"));
        const auto     shader = Value(DecodeShaderProduct(bytes.data(), bytes.size()));
        const auto     id     = Add(AssetType::Shader, std::move(bytes));
        MaterialSource source;
        source.Shader       = id;
        const auto material = Material(source, &shader);
        const auto model    = Model(QuadData(), material);
        Ready();
        for (unsigned attempt = 0;
             attempt < 1000 && Renderer->GetAssetCache()->GetMaterialBindings(material)->Shader != id;
             ++attempt)
        {
            Renderer->GetAssetCache()->BeginFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_EQ(Renderer->GetAssetCache()->GetMaterialBindings(material)->Shader, id);
        Info.pointLights = {{{0, 0, 2}, {2, 4, 6}, {1, 1, 1}, {99, 99, 99}},
                            {{0, 0, 3}, {1, 2, 3}, {1, 1, 1}, {88, 88, 88}}};
        for (float exposure : {.1f, 4.0f})
        {
            Info.sceneCamera.camera.SetExposure(exposure);
            RenderProbe(model);
            EXPECT_EQ(Centre(), (std::array<uint8_t, 4> {48, 96, 143, 255}));
        }
    }
    TEST_P(RendererReferenceGL, DisplayMaterialsAndBlendsRemainByteExactAcrossHdrTransitions)
    {
        // Bug: tone map display pixels twice, transform custom output, reorder blends,
        // or damage untouched pixels during the inverse -> binary16 -> forward conversion.
        Info.sceneCamera.camera = Camera(glm::ortho(-1.f, 1.f, -1.f, 1.f, .1f, 20.f));
        const auto display      = Probe(
            "float "
            "b=mod(floor(gl_FragCoord.x)+128*mod(floor(gl_FragCoord.y),2),256);o_Color=vec4(b/255,(255-b)/255,0.5,1);",
            ShaderOutputSpace::Display);
        const auto blend =
            Probe("o_Color=vec4(0.17,0.73,0.31,0.35);", ShaderOutputSpace::Display, MaterialAlphaMode::Blend);
        const auto hdr    = Probe("o_Color=vec4(4,2,1,1);", ShaderOutputSpace::LinearHDR);
        auto       render = [&](bool mixed, bool blended) {
            Renderer->BeginScene(Info);
            SubmitLegacyOracle(*Renderer, display, glm::mat4(1));
            if (mixed)
                SubmitLegacyOracle(*Renderer, hdr, glm::translate(glm::mat4(1), glm::vec3(4, 0, 0)));
            if (blended)
                SubmitLegacyOracle(*Renderer, blend, glm::translate(glm::mat4(1), glm::vec3(0, 0, .1f)));
            Renderer->EndScene();
            return Read(Renderer->GetRenderTarget().Colors[0].Target);
        };
        for (float exposure : {.001f, .1f, .8f, 4.f, 100.f})
        {
            Info.sceneCamera.camera.SetExposure(exposure);
            for (bool blended : {false, true})
            {
                const auto direct = render(false, blended);
                const auto mixed  = render(true, blended);
                EXPECT_EQ(direct.Pixels, mixed.Pixels) << exposure << " blended=" << blended;
                if (!blended)
                    for (unsigned y = 0; y < Size; ++y)
                        for (unsigned x = 0; x < Size; ++x)
                        {
                            const unsigned nativeY = Renderer->GetDevice()->GetCoordinateConventions().Origin ==
                                                             RenderHardware::FramebufferOrigin::LowerLeft ?
                                                         Size - 1 - y :
                                                         y;
                            const unsigned b       = x + 128 * (nativeY % 2);
                            const auto     i       = (y * Size + x) * 4;
                            EXPECT_EQ(direct.Pixels[i], b);
                            EXPECT_EQ(direct.Pixels[i + 1], 255 - b);
                            EXPECT_EQ(direct.Pixels[i + 2], 128);
                            EXPECT_EQ(direct.Pixels[i + 3], 255);
                        }
            }
        }
    }
    TEST_P(RendererReferenceGL, DebugViewsBypassExposureAndPostPassExactly)
    {
        // Bug: normal/base/wireframe views enter HDR or are tone mapped by the final pass.
        for (auto mode : {SceneViewMode::BaseColor, SceneViewMode::Normals, SceneViewMode::Wireframe})
        {
            Renderer->SetViewMode(mode);
            Info.sceneCamera.camera.SetExposure(.1f);
            TexturedScene();
            const auto low = Read(Renderer->GetRenderTarget().Colors[0].Target);
            Info.sceneCamera.camera.SetExposure(4);
            TexturedScene();
            EXPECT_EQ(low.Pixels, Read(Renderer->GetRenderTarget().Colors[0].Target).Pixels);
            for (auto pass : Renderer->GetRenderGraph().ExecutionOrder())
                EXPECT_NE(Renderer->GetRenderGraph().PassName(pass), "Tone mapping (ACES fitted + sRGB)");
        }
    }
    TEST_P(RendererReferenceGL, PreviewUsesSceneHdrCurveAndExplicitExposure)
    {
        // Bug: thumbnails use the old LDR material path or preview cache loses output-space metadata.
        const auto              hdr = Probe("o_Color=vec4(4,0.5,0.125,1);", ShaderOutputSpace::LinearHDR);
        MaterialPreviewRenderer preview(*Renderer);
        MaterialPreviewOptions  options;
        options.Size  = {Size, Size};
        options.Shape = MaterialPreviewShape::Plane;
        for (float exposure : {.25f, .8f, 2.f})
        {
            options.Exposure = exposure;
            auto thumbnail   = preview.RenderThumbnail(Materials.back(), options);
            ASSERT_TRUE(thumbnail);
            const auto i = (Size / 2 * Size + Size / 2) * 4;
            for (unsigned c = 0; c < 3; ++c)
                EXPECT_NEAR(thumbnail->Pixels[i + c], FilmicByte(std::array<double, 3> {4, .5, .125}[c], exposure), 1);
        }
        const auto display = Probe("o_Color=vec4(0.25,0.5,0.75,1);", ShaderOutputSpace::Display);
        options.Exposure   = 4;
        auto thumbnail     = preview.RenderThumbnail(Materials.back(), options);
        ASSERT_TRUE(thumbnail);
        const auto i = (Size / 2 * Size + Size / 2) * 4;
        EXPECT_EQ(thumbnail->Pixels[i], 64);
        EXPECT_EQ(thumbnail->Pixels[i + 1], 128);
        EXPECT_EQ(thumbnail->Pixels[i + 2], 191);
    }
    TEST_P(RendererReferenceGL, PreviewCacheTracksOutputSpaceEvenWithIdenticalBytecode)
    {
        // Bug: unsaved preview cache compares only shader bytecode and ignores output-space metadata.
        Probe("o_Color=vec4(0.25,0.5,0.75,1);", ShaderOutputSpace::Display);
        auto materialAsset = Assets->Request<MaterialDataAsset>(Materials.back()).Asset;
        ASSERT_TRUE(materialAsset);
        auto shaderAsset = Assets->Request<ShaderDataAsset>(materialAsset->GetData().Shader).Asset;
        ASSERT_TRUE(shaderAsset);
        auto                    source = shaderAsset->GetData();
        MaterialPreviewRenderer preview(*Renderer);
        MaterialPreviewOptions  options;
        options.Size  = {Size, Size};
        options.Shape = MaterialPreviewShape::Plane;
        for (auto space : {ShaderOutputSpace::Display, ShaderOutputSpace::LinearHDR, ShaderOutputSpace::Display})
        {
            source.OutputSpace = space;
            const auto image   = preview.RenderImage(materialAsset->GetData(), &source, options);
            ASSERT_TRUE(image.Target);
            ASSERT_TRUE(image.Complete);
            const auto pixels = Read(image.Target).Pixels;
            const auto i      = (Size / 2 * Size + Size / 2) * 4;
            for (unsigned c = 0; c < 3; ++c)
            {
                const double linear = (c + 1) * .25;
                const int    expected =
                    space == ShaderOutputSpace::Display ? int(std::lround(linear * 255)) : FilmicByte(linear, .8);
                EXPECT_NEAR(pixels[i + c], expected, space == ShaderOutputSpace::Display ? 0 : 1);
            }
        }
    }
    TEST_P(RendererReferenceGL, MigratedMultipartFrameMatchesLegacyAfterSaveReloadAndPlayCopy)
    {
        // Bug: hierarchy multiplication or draw/pass order changes pixels, or a
        // per-part override leaks to another section sharing its material slot.
        auto mesh     = QuadData();
        mesh.Material = Textured;
        BuildSingleSection(mesh);
        mesh.MaterialSlots[0].AuthoredMaterial = Textured;
        const auto originalVertices            = mesh.Vertices;
        const auto originalIndices             = mesh.Indices;
        for (unsigned part = 1; part < 3; ++part)
        {
            const uint32_t base = mesh.Vertices.size(), first = mesh.Indices.size();
            Bounds         bounds;
            for (auto vertex : originalVertices)
            {
                vertex.Position[0] += .3f * part;
                vertex.Position[2] -= .07f * part;
                bounds.Expand(glm::vec3(vertex.Position[0], vertex.Position[1], vertex.Position[2]));
                mesh.Vertices.push_back(vertex);
            }
            for (auto index : originalIndices)
                mesh.Indices.push_back(base + index);
            mesh.Sections.push_back({first, uint32_t(originalIndices.size()), 0, bounds});
            mesh.LocalBounds.Expand(bounds);
        }
        const auto meshID = Add(AssetType::StaticMesh, Value(EncodeMeshProduct(mesh)));
        Meshes.push_back(meshID);
        const std::array<float, 16> identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ModelData                   prefab;
        prefab.Nodes = {
            {"Root", -1, identity, {}, "root"}, {"A", 0, identity, {meshID}, "A"}, {"B", 0, identity, {meshID}, "B"}};
        prefab.Nodes[0].LocalTransform[3] = .137f;
        prefab.Nodes[1].LocalTransform[3] = -.47f;
        prefab.Nodes[1].LocalTransform[7] = .293f;
        prefab.Nodes[2].LocalTransform[3] = .23f;
        prefab.Nodes[2].LocalTransform[7] = -.311f;
        for (unsigned part = 0; part < 3; ++part)
            prefab.SectionIdentities.push_back({"part" + std::to_string(part), {Source, 10000 + part}, meshID, part});
        ProductModelComponent legacy;
        legacy.Model = Add(AssetType::Model, Value(EncodeModelProduct(prefab)));
        Models.push_back(legacy.Model);
        legacy.SetOverride(Textured, Emissive);
        legacy.SetOverride({Source, 10001}, Toon);
        Ready();
        auto root = Logic->CreateEntityWithID(UUID(1234), "Migrated multipart", false);
        SetLegacyPending(root, legacy);
        root.GetComponent<TransformComponent>() = {{.123f, .217f, -.11f}, {.13f, -.09f, .19f}, {.61f, .73f, .83f}};
        const auto rootWorld                    = Logic->GetWorldTransform(root);
        auto       camera                       = Logic->CreateEntity("Camera");
        camera.GetComponent<TransformComponent>().Position = Info.sceneCamera.Position;
        camera.AddComponent<CameraComponent>().SceneCamera.SetPerspectve(glm::radians(45.0f), .1f, 20);
        Logic->OnViewportResize(Size, Size);
        auto light                                        = Logic->CreateEntity("Light");
        light.GetComponent<TransformComponent>().Position = Info.pointLight.Position;
        auto& lighting                                    = light.AddComponent<PointLightComponent>();
        lighting.Diffuse_Intensity                        = Info.pointLight.Diffuse_Intensity;
        lighting.Specular_Intensity                       = Info.pointLight.Specular_Intensity;
        lighting.Ambient_Intensity                        = Info.pointLight.Ambient_Intensity;
        auto runtime                                      = CreateRef<SceneRenderer>(Render.get());
        RegisterDefaultEditorFeatures(runtime->GetFeatures());
        runtime->SetAssetManager(Assets.get());
        runtime->PrepareEnvironment({32, 8, 32, 32});
        RenderResource settings;
        settings.Piplinestate.EnableStencil         = settings.Piplinestate.EnableOutline =
            settings.Piplinestate.EnableDebugNormal = true;
        settings.Piplinestate.EnableGridShader      = false;
        Render->SetRenderResource(settings);
        const auto oracleHighlights = HighlightLegacyOracle(*runtime, *Logic, {{legacy, rootWorld}});
        runtime->BeginScene(Info);
        SubmitLegacyOracle(*runtime, legacy, rootWorld, ModelStencilMode::WriteReference);
        SubmitLegacyGeometryOracle(*runtime,
                                   legacy,
                                   rootWorld * glm::scale(glm::mat4(1), glm::vec3(1.007f)),
                                   "StencilShader",
                                   ModelGeometryMode::StencilOutline);
        SubmitLegacyGeometryOracle(*runtime, legacy, rootWorld, "OutLineShader", ModelGeometryMode::BackFaces);
        SubmitLegacyGeometryOracle(*runtime, legacy, rootWorld, "DebugNormalShader");
        runtime->SubmitSkybox(runtime->GetEnvironment().IrradianceMap);
        runtime->EndScene();
        const auto expected      = Read(runtime->GetRenderTarget().Colors[0].Target).Pixels;
        const auto expectedStats = runtime->GetStats();
        for (auto id : oracleHighlights)
            Logic->DestroyEntity(Logic->GetEntityByUUID(id));
        const auto verify = [&] {
            runtime->SetEntityHighlight({UUID(1234)});
            Render->OnRenderRuntime(runtime, TimeStep(0));
            EXPECT_EQ(Read(runtime->GetRenderTarget().Colors[0].Target).Pixels, expected);
            EXPECT_EQ(runtime->GetStats().DrawCalls, expectedStats.DrawCalls);
            EXPECT_EQ(runtime->GetStats().Triangles, expectedStats.Triangles);
            EXPECT_EQ(runtime->GetRenderView().Draws.size(), 6u);
        };
        verify();
        Value(SceneSerializer(Logic).SerializeChecked(Directory / "prefab.scene"));
        auto loaded = Value(SceneSerializer::DeserializeChecked(Directory / "prefab.scene"));
        loaded->UpdatePrefabs(*Assets);
        EXPECT_EQ(loaded->Capture(), Logic->Capture());
        Render->SetLogicScene(loaded);
        verify();
        auto copy = CreateRef<Scene>();
        copy->Restore(loaded->Capture());
        Render->SetLogicScene(copy);
        verify();
        Render->SetLogicScene(Logic);
    }

    TEST_P(RendererReferenceGL, PrefabSelectionOutlinesDescendantsAndPickingExposesOwnerAndRoot)
    {
        // Bug: a prefab root has no MeshComponent so its outline is empty;
        // picking reports the root instead of the entity owning the section.
        ModelData                   prefab;
        const std::array<float, 16> identity {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        prefab.Nodes                      = {{"Root", -1, identity, {}, "root"},
                                             {"Left", 0, identity, {Meshes[0]}, "left"},
                                             {"Right", 0, identity, {Meshes[0]}, "right"}};
        prefab.Nodes[1].LocalTransform[3] = -.65f;
        prefab.Nodes[2].LocalTransform[3] = .65f;
        for (auto index : {1u, 2u})
            for (auto diagonal : {0u, 5u, 10u})
                prefab.Nodes[index].LocalTransform[diagonal] = .4f;
        const auto id = Add(AssetType::Model, Value(EncodeModelProduct(prefab)));
        Models.push_back(id);
        Ready();
        auto root = Logic->InstantiatePrefab(id);
        Logic->UpdatePrefabs(*Assets);
        const auto parts = Logic->GetMeshEntities(root);
        ASSERT_EQ(parts.size(), 2u);
        const auto render = [&](std::vector<UUID> selection, std::vector<LegacyOracleDraw> legacy) {
            Renderer->SetEntityHighlight(std::move(selection));
            std::vector<UUID> oracleRoots;
            if (!legacy.empty())
                oracleRoots = HighlightLegacyOracle(*Renderer, *Logic, std::move(legacy));
            Renderer->BeginScene(Info);
            for (auto entity : parts)
                Renderer->SubmitMesh({entity.GetComponent<MeshComponent>(),
                                      Logic->GetWorldTransform(entity),
                                      entity.GetUUID(),
                                      root.GetUUID()});
            Renderer->EndScene();
            auto pixels = Read(Renderer->GetRenderTarget().Colors[0].Target).Pixels;
            for (auto id : oracleRoots)
                Logic->DestroyEntity(Logic->GetEntityByUUID(id));
            return pixels;
        };
        const auto                    baseline = render({}, {});
        const auto                    actual   = render({root.GetUUID()}, {});
        std::vector<LegacyOracleDraw> legacy;
        for (auto entity : parts)
            legacy.push_back({Sphere, Logic->GetWorldTransform(entity)});
        EXPECT_EQ(actual, render({}, legacy));
        EXPECT_NE(actual, baseline);
        const auto one = render({parts[0].GetUUID()}, {});
        EXPECT_EQ(one, render({}, {legacy[0]}));
        EXPECT_NE(one, actual);
        std::vector<MeshDraw> draws;
        Renderer->AppendEntityDraws(*Logic, root.GetUUID(), 7, draws);
        ASSERT_EQ(draws.size(), 2u);
        const auto projection = Info.sceneCamera.camera.GetProjectionMatrix() * Info.sceneCamera.viewMatrix;
        for (auto part : parts)
        {
            const auto       clip = projection * glm::vec4(glm::vec3(Logic->GetWorldTransform(part)[3]), 1);
            const auto       ndc  = glm::vec3(clip) / clip.w;
            const glm::uvec2 pixel {uint32_t((ndc.x * .5f + .5f) * Size), uint32_t((ndc.y * .5f + .5f) * Size)};
            const auto       hit = Renderer->PickSceneSurface(*Logic, projection, pixel);
            EXPECT_EQ(hit.EntityID, part.GetUUID());
            EXPECT_EQ(hit.InstanceRoot, root.GetUUID());
            EXPECT_TRUE(hit.Position);
        }
    }

    TEST_P(RendererReferenceGL, FullRuntimeFrameIsByteIdenticalToPhaseTwoD)
    {
        // Deliberately captured phase-2d baseline (ACES fitted, diffuse radiance, distinct lights). All attachment
        // bytes detect moving sprites/grid/sky after materials, shell state, or outline order.
        Renderer2D::Init();
        SpriteInitialized                                  = true;
        auto camera                                        = Logic->CreateEntity("frame camera");
        camera.GetComponent<TransformComponent>().Position = {0, 1.4f, 4};
        camera.GetComponent<TransformComponent>().Rotation = {-.32f, 0, 0};
        camera.AddComponent<CameraComponent>().SceneCamera.SetPerspectve(glm::radians(45.0f), .1f, 20);
        auto light                                        = Logic->CreateEntity("frame light");
        light.GetComponent<TransformComponent>().Position = Info.pointLight.Position;
        auto& lighting                                    = light.AddComponent<PointLightComponent>();
        lighting.Ambient_Intensity                        = Info.pointLight.Ambient_Intensity;
        lighting.Diffuse_Intensity                        = Info.pointLight.Diffuse_Intensity;
        lighting.Specular_Intensity                       = Info.pointLight.Specular_Intensity;
        auto model                                        = Logic->CreateEntity("frame model");
        model.AddComponent<PrefabInstanceComponent>().Prefab = Sphere.Model;
        model.GetComponent<TransformComponent>().Scale     = glm::vec3(.7f);
        auto sprite                                        = Logic->CreateEntity("frame sprite");
        sprite.GetComponent<TransformComponent>().Position = {-.3f, .2f, .5f};
        sprite.GetComponent<TransformComponent>().Scale    = {1.2f, .8f, 1};
        sprite.AddComponent<SpriteRendererComponent>(glm::vec4(.8f, .4f, .2f, .7f),
                                                     Renderer->GetAssetCache()->GetTexture(Albedo));
        auto runtime = CreateRef<SceneRenderer>(Render.get());
        RegisterDefaultEditorFeatures(runtime->GetFeatures());
        runtime->SetAssetManager(Assets.get());
        runtime->PrepareEnvironment({32, 8, 32, 32});
        // Prewarm the same ready assets; this test isolates frame structure from async loading.
        for (auto id : Models)
            ASSERT_TRUE(runtime->GetAssetCache()->GetModel(id));
        for (auto id : Meshes)
            ASSERT_TRUE(runtime->GetAssetCache()->GetMesh(id));
        for (auto id : Materials)
            ASSERT_TRUE(runtime->GetAssetCache()->GetMaterialBindings(id));
        RenderResource settings {};
        settings.Piplinestate.EnableStencil     = true;
        settings.Piplinestate.EnableOutline     = true;
        settings.Piplinestate.EnableDebugNormal = true;
        Render->SetRenderResource(settings);
        runtime->SetEntityHighlight({model.GetUUID()});
        runtime->SetPreview(Quad.Model, Transform({.6f, -.4f, .3f}, {.4f, .3f, 1}));
        Logic->OnViewportResize(Size, Size);
        Render->OnRenderRuntime(runtime, TimeStep(0));
        EXPECT_EQ(runtime->GetStats().DrawCalls, 5u);
        EXPECT_EQ(runtime->GetStats().Triangles, 4098u);
        EXPECT_EQ(Renderer2D::GetStats().QuadCount, 1u);
        EXPECT_EQ(Renderer2D::GetStats().DrawCalls, 1u);
        auto                                                         target = runtime->GetRenderTarget();
        std::vector<RenderHardware::Handle<RenderHardware::Texture>> attachments {
            target.Colors[0].Target, target.Colors[1].Target, target.Depth.Target};
        for (size_t i = 0; i < attachments.size(); ++i)
        {
            auto device = runtime->GetDevice();
            auto buffer = device->CreateBuffer(
                {Size * Size * 4, RenderHardware::BufferUsage::CopyDestination, RenderHardware::MemoryUsage::Readback});
            auto commands = device->CreateCommandList();
            commands->CopyTextureToBuffer(attachments[i], {}, buffer);
            device->Submit(commands);
            device->WaitIdle();
            auto                   bytes = buffer->Read(0, Size * Size * 4);
            if (device->GetCoordinateConventions().Origin == RenderHardware::FramebufferOrigin::UpperLeft)
                for (unsigned y = 0; y < Size / 2; ++y)
                    std::swap_ranges(bytes.begin() + y * Size * 4,
                                     bytes.begin() + (y + 1) * Size * 4,
                                     bytes.begin() + (Size - 1 - y) * Size * 4);
            fs::create_directories(KANS_TEST_OUTPUT_DIR);
            std::ofstream dump(fs::path(KANS_TEST_OUTPUT_DIR) /
                                   (std::string("runtime-") + (GetParam() == GpuTestBackend::OpenGL ? "gl-" : "vk-") +
                                    std::to_string(i) + ".bin"),
                               std::ios::binary);
            dump.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            std::vector<std::byte> digestBytes(bytes.size());
            std::memcpy(digestBytes.data(), bytes.data(), bytes.size());
            const char* expected[] = {"3b60ff2f4b8a27b07ba6f82c2be76ea45f000af0e72a8d3491c9a9079a9643cd",
                                      "d1b4803bb70e5b8e4a1b68ed184ddfd2a8fe31cba2a72561c6870b0da2feb6db",
                                      "62d92c7f002105fa75ac102c333e394e9b3706a8c5ad7a9da310d49680544988"};
            // Preserve an exact byte oracle on each backend. Conversion of the
            // projection's depth range changes D24 rounding by -4..+3 LSBs; one
            // Bresenham debug-line boundary pixel and filtered material samples
            // differ too. Color-1 and stencil are preserved by the HDR resolve.
            // Canonical row order is handled above, not folded into these hashes.
            const char* vulkanExpected[] = {"44727924897d5193e0820f9bda06ac0caa0f224adaff83a71eda0e418d2456c7",
                                            "d1b4803bb70e5b8e4a1b68ed184ddfd2a8fe31cba2a72561c6870b0da2feb6db",
                                            "9a31a96d79d6b7dba14d006d111a6780e4aae7257f2b992437d0ef93e570fc28"};
            EXPECT_EQ(ComputeContentDigest(digestBytes),
                      GetParam() == GpuTestBackend::OpenGL ? expected[i] : vulkanExpected[i])
                << "attachment " << i;
        }
        const auto&              graph = runtime->GetRenderGraph();
        std::vector<std::string> passes;
        for (auto pass : graph.ExecutionOrder())
            passes.push_back(graph.PassName(pass));
        EXPECT_EQ(passes,
                  (std::vector<std::string> {"Clear",
                                             "Renderer2D sprites",
                                             "Grid",
                                             "Skybox",
                                             "Materials (opaque/mask, blend)",
                                             "Tone mapping (ACES fitted + sRGB)",
                                             "Legacy geometry",
                                             "Selection mask",
                                             "Selection composite",
                                             "Preview mask",
                                             "Preview composite"}));
        RenderHardware::Handle<RenderHardware::Texture> mask;
        size_t                                          maskCount = 0;
        for (size_t i = 0; i < graph.Resources().size(); ++i)
            if (!graph.Resources()[i].Imported)
            {
                ++maskCount;
                if (mask)
                    EXPECT_EQ(graph.Texture(uint32_t(i)), mask); // Selection and preview alias.
                mask = graph.Texture(uint32_t(i));
            }
        EXPECT_EQ(maskCount, 2u);
        ASSERT_TRUE(mask);
        // Catches recreating masks each frame even though viewport and lifetimes are stable.
        for (int frame = 0; frame < 5; ++frame)
        {
            if (frame == 2)
            {
                // Deselect for one frame; selecting again must reuse the stable-viewport mask.
                Render->OnRenderRuntime(runtime, TimeStep(0));
                for (const auto& resource : runtime->GetRenderGraph().Resources())
                    EXPECT_TRUE(resource.Imported);
                EXPECT_EQ(runtime->GetStats().DrawCalls, 4u);
            }
            runtime->SetEntityHighlight({model.GetUUID()});
            runtime->SetPreview(Quad.Model, Transform({.6f, -.4f, .3f}, {.4f, .3f, 1}));
            Render->OnRenderRuntime(runtime, TimeStep(0));
            const auto& next = runtime->GetRenderGraph();
            for (size_t i = 0; i < next.Resources().size(); ++i)
                if (!next.Resources()[i].Imported)
                    EXPECT_EQ(next.Texture(uint32_t(i)), mask);
            EXPECT_EQ(runtime->GetStats().DrawCalls, 5u);
            EXPECT_EQ(runtime->GetStats().Triangles, 4098u);
        }
        for (auto entity : {sprite, model, light, camera})
            Logic->DestroyEntity(entity);
    }

    class PointMarkerFeature final : public RenderFeature
    {
    public:
        explicit PointMarkerFeature(std::array<unsigned, 5>& records, unsigned& preparations) :
            Records(records), Preparations(preparations)
        {}
        void Prepare(RenderView&, SceneRenderer&) override { ++Preparations; }
        bool RunsAt(RenderExtensionPoint) const override { return true; }
        void AddPasses(RenderExtensionPoint point, const RenderFeatureContext& context) override
        {
            const char* name = nullptr;
            switch (point)
            {
                case RenderExtensionPoint::Immediate:
                    name = "Feature Immediate";
                    break;
                case RenderExtensionPoint::BeforeOpaque:
                    name = "Feature BeforeOpaque";
                    break;
                case RenderExtensionPoint::AfterTransparent:
                    name = "Feature AfterTransparent";
                    break;
                case RenderExtensionPoint::BeforePostProcess:
                    name = "Feature BeforePostProcess";
                    break;
                case RenderExtensionPoint::Overlay:
                    name = "Feature Overlay";
                    break;
            }
            context.Graph.AddPass(name, {}, [this, point](auto&) { ++Records[size_t(point)]; }, true);
        }
        std::array<unsigned, 5>& Records;
        unsigned&                Preparations;
    };
    std::vector<std::string> FeaturePassNames(const SceneRenderer& renderer)
    {
        std::vector<std::string> names;
        const auto&              graph = renderer.GetRenderGraph();
        for (auto pass : graph.ExecutionOrder())
            names.push_back(graph.PassName(pass));
        return names;
    }
    TEST_P(RendererReferenceGL, FeaturePointsPreserveInterleavedImmediateAndMaterialOverlayOrder)
    {
        // Bug: emit all grids together, inject BeforeOpaque before the immediate sequence,
        // put AfterTransparent before materials, or put Overlay before either outline.
        std::array<unsigned, 5> records {};
        unsigned                preparations = 0;
        Renderer->GetFeatures().Register("point markers", std::make_unique<PointMarkerFeature>(records, preparations));
        HighlightLegacyOracle(*Renderer, *Logic, {{Sphere, glm::mat4(1)}});
        Renderer->SetPreview(Quad.Model, Transform({.6f, -.4f, .3f}, {.4f, .3f, 1}));
        Renderer->BeginScene(Info);
        Renderer->SubmitGrid(.025f, 16.025f, glm::mat4(1));
        Renderer->SubmitSkybox(Renderer->GetSkybox());
        Renderer->SubmitGrid(.05f, 8, glm::mat4(1), {true, false});
        SubmitLegacyOracle(*Renderer, Sphere, glm::mat4(1));
        SubmitLegacyGeometryOracle(*Renderer, Sphere, glm::mat4(1), "DebugNormalShader");
        Renderer->EndScene();
        // EditorGrid was registered before the marker; both inject at Immediate.
        // At Skybox entries, only the marker injects before the core Skybox pass.
        EXPECT_EQ(FeaturePassNames(*Renderer),
                  (std::vector<std::string> {"Clear",
                                             "Grid",
                                             "Feature Immediate",
                                             "Feature Immediate",
                                             "Skybox",
                                             "Grid",
                                             "Feature Immediate",
                                             "Feature BeforeOpaque",
                                             "Materials (opaque/mask, blend)",
                                             "Tone mapping (ACES fitted + sRGB)",
                                             "Legacy geometry",
                                             "Feature AfterTransparent",
                                             "Feature BeforePostProcess",
                                             "Selection mask",
                                             "Selection composite",
                                             "Preview mask",
                                             "Preview composite",
                                             "Feature Overlay"}));
        EXPECT_EQ(records, (std::array<unsigned, 5> {3, 1, 1, 1, 1}));
        EXPECT_EQ(preparations, 1u);
        Renderer->GetFeatures().SetEnabled("point markers", false);
    }
    TEST_P(RendererReferenceGL, DisabledEditorFeaturesSkipGhostPreparationPassesAndTransientResources)
    {
        // Bug: gate only AddPasses while still submitting the lit ghost in Prepare,
        // or let grid/debug/outline passes or masks survive a disabled feature.
        for (const char* name : {"EditorGrid", "DebugGeometry", "EditorSelection", "EditorPreview"})
            Renderer->GetFeatures().SetEnabled(name, false);
        for (int frame = 0; frame < 3; ++frame)
        {
            HighlightLegacyOracle(*Renderer, *Logic, {{Sphere, glm::mat4(1)}});
            Renderer->SetPreview(Quad.Model, glm::mat4(1));
            Renderer->BeginScene(Info);
            SubmitLegacyOracle(*Renderer, Sphere, glm::mat4(1));
            Renderer->SubmitGrid(.025f, 16.025f, glm::mat4(1));
            SubmitLegacyGeometryOracle(*Renderer, Sphere, glm::mat4(1), "DebugNormalShader");
            Renderer->EndScene();
            EXPECT_EQ(FeaturePassNames(*Renderer),
                      (std::vector<std::string> {
                          "Clear", "Materials (opaque/mask, blend)", "Tone mapping (ACES fitted + sRGB)"}));
            EXPECT_EQ(Renderer->GetRenderView().Draws.size(), 1u);
            EXPECT_EQ(Renderer->GetStats().DrawCalls, 1u);
            EXPECT_EQ(Renderer->GetStats().Triangles, 1024u);
            ASSERT_EQ(Renderer->GetRenderGraph().Resources().size(),
                      10u); // Display/depth targets + HDR, three IBL images, albedo/normal/shared white.
            for (const auto& resource : Renderer->GetRenderGraph().Resources())
                EXPECT_TRUE(resource.Imported);
        }
        for (const char* name : {"EditorGrid", "DebugGeometry", "EditorSelection", "EditorPreview"})
            Renderer->GetFeatures().SetEnabled(name, true);
        Renderer->SetViewMode(SceneViewMode::Normals);
        HighlightLegacyOracle(*Renderer, *Logic, {{Sphere, glm::mat4(1)}});
        Renderer->SetPreview(Quad.Model, glm::mat4(1));
        Renderer->BeginScene(Info);
        SubmitLegacyOracle(*Renderer, Sphere, glm::mat4(1));
        Renderer->SubmitGrid(.025f, 16.025f, glm::mat4(1));
        SubmitLegacyGeometryOracle(*Renderer, Sphere, glm::mat4(1), "DebugNormalShader");
        Renderer->EndScene();
        EXPECT_EQ(FeaturePassNames(*Renderer),
                  (std::vector<std::string> {"Clear",
                                             "Grid",
                                             "Materials (opaque/mask, blend)",
                                             "Tone mapping (ACES fitted + sRGB)",
                                             "Legacy geometry",
                                             "Selection mask",
                                             "Selection composite",
                                             "Preview mask",
                                             "Preview composite"}));
        ASSERT_EQ(Renderer->GetRenderView().Draws.size(), 2u);
        EXPECT_EQ(Renderer->GetRenderView().Draws[0].ViewMode, SceneViewMode::Normals);
        EXPECT_EQ(Renderer->GetRenderView().Draws[1].ViewMode, SceneViewMode::Lit);
        EXPECT_EQ(Renderer->GetStats().DrawCalls, 3u);
        EXPECT_EQ(Renderer->GetStats().Triangles, 2050u);
        unsigned transients = 0;
        for (const auto& resource : Renderer->GetRenderGraph().Resources())
            if (!resource.Imported)
                ++transients;
        EXPECT_EQ(transients, 2u);
        Renderer->SetViewMode(SceneViewMode::Lit);
    }
    TEST_P(RendererReferenceGL, FeatureRegistrationAndEnablingAreIsolatedBetweenSceneRenderers)
    {
        // Bug: store feature instances/enabled flags globally or in the pipeline registry;
        // a marker or grid toggle on one view then affects the other renderer.
        auto                    other = CreateScope<SceneRenderer>(Render.get());
        RegisterDefaultEditorFeatures(other->GetFeatures());
        std::array<unsigned, 5> records {};
        unsigned                preparations = 0;
        Renderer->GetFeatures().Register("private markers",
                                         std::make_unique<PointMarkerFeature>(records, preparations));
        Renderer->GetFeatures().SetEnabled("EditorGrid", false);
        EXPECT_TRUE(other->GetFeatures().IsEnabled("EditorGrid"));
        EXPECT_THROW(other->GetFeatures().IsEnabled("private markers"), std::invalid_argument);
        Renderer->BeginScene(Info);
        Renderer->SubmitGrid(.025f, 16.025f, glm::mat4(1));
        Renderer->EndScene();
        EXPECT_EQ(FeaturePassNames(*Renderer),
                  (std::vector<std::string> {"Clear",
                                             "Feature Immediate",
                                             "Feature BeforeOpaque",
                                             "Feature AfterTransparent",
                                             "Feature BeforePostProcess",
                                             "Feature Overlay"}));
        other->BeginScene(Info);
        other->SubmitGrid(.025f, 16.025f, glm::mat4(1));
        other->EndScene();
        EXPECT_EQ(FeaturePassNames(*other), (std::vector<std::string> {"Clear", "Grid"}));
        EXPECT_EQ(records, (std::array<unsigned, 5> {1, 1, 1, 1, 1}));
        EXPECT_EQ(preparations, 1u);
        Renderer->GetFeatures().SetEnabled("private markers", false);
        Renderer->GetFeatures().SetEnabled("EditorGrid", true);
    }
    INSTANTIATE_TEST_SUITE_P(Backends,
                             RendererReferenceGL,
                             testing::Values(GpuTestBackend::OpenGL, GpuTestBackend::Vulkan),
                             [](const testing::TestParamInfo<GpuTestBackend>& info) {
                                 return info.param == GpuTestBackend::OpenGL ? "OpenGL" : "Vulkan";
                             });
} // namespace
