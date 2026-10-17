#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <thread>
#include <gtest/gtest.h>
#ifdef __linux__
#include <unistd.h>
#include <csignal>
#include <sys/resource.h>
#endif
#include "Kans3D/Asset/Importer/MaterialImporter.h"
#include "Kans3D/Asset/Importer/ModelImporter.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Editor/EditorAssetImportService.h"
#include "Kans3D/Scene/ProductModelComponent.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    // Damage a resolved Material input at the importer-registry seam. The store
    // validates publication, so malformed bytes cannot be published directly.
    class MalformedInputModelImporter final : public AssetImporterBase
    {
    public:
        AssetID  Material;
        uint32_t GetVersion() const override { return ModelImporter().GetVersion() + 100; }
        bool     RequiresProductBytes() const override { return true; }
        std::variant<ImportInputs, AssetError> DiscoverInputs(const ImportContext& context) const override
        {
            return ModelImporter().DiscoverInputs(context);
        }
        std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override
        {
            auto damaged                            = context;
            damaged.ProductBytes.at(Material).at(0) = std::byte {'!'};
            return ModelImporter().Import(damaged);
        }
    };

    // A future model importer using an actual imported mesh and a non-Standard
    // embedded material, published through the normal build coordinator.
    class ShaderModelImporter final : public AssetImporterBase
    {
    public:
        AssetID  Shader;
        uint32_t GetVersion() const override { return ModelImporter().GetVersion() + 100; }
        bool     RequiresProductBytes() const override { return true; }
        std::variant<ImportInputs, AssetError> DiscoverInputs(const ImportContext& context) const override
        {
            auto found = ModelImporter().DiscoverInputs(context);
            if (auto* inputs = std::get_if<ImportInputs>(&found))
                inputs->Products.push_back(Shader);
            return found;
        }
        std::variant<ImportResult, AssetError> Import(const ImportContext& context) const override
        {
            auto imported = ModelImporter().Import(context);
            if (auto* result = std::get_if<ImportResult>(&imported))
                for (auto& product : result->Products)
                {
                    if (product.Type != AssetType::Material)
                        continue;
                    const auto file  = context.StagingDirectory / product.StagedFile;
                    auto       input = ReadProductFile(file);
                    if (const auto* error = std::get_if<AssetError>(&input))
                        return *error;
                    const auto& bytes   = std::get<std::vector<std::byte>>(input);
                    auto        decoded = DecodeMaterialProduct(bytes.data(), bytes.size());
                    if (const auto* error = std::get_if<AssetError>(&decoded))
                        return *error;
                    const auto& original = std::get<MaterialData>(decoded);
                    if (original.Name != "Cutout")
                        continue;
                    const auto& shaderBytes = context.ProductBytes.at(Shader);
                    auto        shader      = DecodeShaderProduct(shaderBytes.data(), shaderBytes.size());
                    if (const auto* error = std::get_if<AssetError>(&shader))
                        return *error;
                    MaterialSource source;
                    source.Name        = original.Name;
                    source.Shader      = Shader;
                    source.Render      = original.Render;
                    source.Parameters  = {{"Tint", {0.25, 0.5, 0.75, 1}},
                                          {"Strength", {2}},
                                          {"Steps", {-2147483647}},
                                          {"Offsets", {0.1, 0.2, 0.3, -0.0}}};
                    const auto texture = context.ProductsBySource.at(context.SourceFile.parent_path() / "pixel.tga");
                    source.Textures["u_Albedo"] = texture;
                    auto material               = ResolveMaterialSource(source, &std::get<ShaderData>(shader));
                    if (const auto* error = std::get_if<AssetError>(&material))
                        return *error;
                    auto output = EncodeMaterialProduct(std::get<MaterialData>(material));
                    if (const auto* error = std::get_if<AssetError>(&output))
                        return *error;
                    fs::remove(file);
                    auto written = WriteProductFile(file, std::get<std::vector<std::byte>>(output));
                    if (const auto* error = std::get_if<AssetError>(&written))
                        return *error;
                    product.Dependencies = {Shader, texture};
                }
            return imported;
        }
    };

    class ModelMaterialRemappingTest : public ::testing::Test
    {
    protected:
        fs::path       Root;
        Scope<Project> Owner;
        void           SetUp() override
        {
            Root = fs::temp_directory_path() / ("kans-remap-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root / "assets");
        }
        void TearDown() override
        {
            Owner.reset();
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
        void Write(const fs::path& file, const std::string& text)
        {
            const auto path = Root / "assets" / file;
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        }
        std::string Read(const fs::path& file)
        {
            std::ifstream input(file, std::ios::binary);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
        void Open()
        {
            Owner.reset();
            ProjectConfig config;
            config.Name          = "Remapping";
            config.RootDirectory = Root;
            auto opened          = Project::Open(config);
            ASSERT_TRUE(std::holds_alternative<Scope<Project>>(opened));
            Owner = std::get<Scope<Project>>(std::move(opened));
        }
        SourceAssetID ID(const std::string& file = "model.gltf")
        {
            return Owner->GetSourceAssetDatabase().FindIDByPath(AssetPath(file));
        }
        AssetBuildGraphResult Build(const std::string& file = "model.gltf", const std::string& target = "default")
        {
            AssetBuildOptions options;
            options.Target = target;
            auto result    = Owner->BuildAsset(ID(file), options);
            if (const auto* error = std::get_if<AssetError>(&result))
            {
                ADD_FAILURE() << error->Message;
                return {};
            }
            return std::get<AssetBuildGraphResult>(std::move(result));
        }
        template<typename T>
        T Value(std::variant<T, AssetError> value)
        {
            if (const auto* error = std::get_if<AssetError>(&value))
            {
                ADD_FAILURE() << error->Message;
                return {};
            }
            return std::get<T>(std::move(value));
        }
        std::vector<ModelMaterialSlot> Slots(const std::string& target = "default")
        {
            return Value(Owner->GetModelMaterialSlots(ID(), target));
        }
        ModelMaterialSlot Slot(const std::string& name, const std::string& target = "default")
        {
            for (const auto& slot : Slots(target))
                if (slot.DisplayName == name)
                    return slot;
            ADD_FAILURE() << "Missing slot: " << name;
            return {};
        }
        std::vector<std::byte> Bytes(AssetID id, const std::string& target = "default")
        {
            const auto record = Owner->GetRuntimeAssetRegistry()->Find(id, target);
            if (!record)
            {
                ADD_FAILURE() << "Product absent: " << id.ToString();
                return {};
            }
            return Value(ReadRegisteredProduct(Owner->GetProductDirectory(), *record));
        }
        MaterialData Material(AssetID id, const std::string& target = "default")
        {
            const auto bytes = Bytes(id, target);
            return Value(DecodeMaterialProduct(bytes.data(), bytes.size()));
        }
        std::vector<std::pair<AssetID, MeshData>> Meshes()
        {
            std::vector<std::pair<AssetID, MeshData>> result;
            for (const auto& record : Owner->GetRuntimeAssetRegistry()->GetAllProducts())
                if (record.Metadata.assetID.GetSourceID() == ID() && record.Metadata.Type == AssetType::StaticMesh)
                {
                    const auto bytes = Bytes(record.Metadata.assetID);
                    result.emplace_back(record.Metadata.assetID, Value(DecodeMeshProduct(bytes.data(), bytes.size())));
                }
            return result;
        }
        void Gltf(const std::string& secondName = "Glass")
        {
            std::string buffer(42, '\0');
            buffer[14] = char(0x80), buffer[15] = char(0x3f);
            buffer[30] = char(0x80), buffer[31] = char(0x3f);
            buffer[38] = 1, buffer[40] = 2;
            Write("geometry.bin", buffer);
            std::string image(18, '\0');
            image[2] = 2, image[12] = 1, image[14] = 1, image[16] = 32, image[17] = 8;
            image += {char(32), char(64), char(128), char(255)};
            Write("pixel.tga", image);
            Write("model.gltf", R"({"asset":{"version":"2.0"},
"buffers":[{"uri":"geometry.bin","byteLength":42}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"images":[{"uri":"pixel.tga"}],"textures":[{"source":0}],
"materials":[{"name":"Cutout","alphaMode":"MASK","alphaCutoff":0.375,"doubleSided":true,
"emissiveFactor":[0.25,0.5,0.75],"emissiveTexture":{"index":0},"normalTexture":{"index":0},
"occlusionTexture":{"index":0},"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.6,0.8],
"metallicFactor":0.3,"roughnessFactor":0.7,"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}}},
{"name":")" + secondName + R"(","alphaMode":"BLEND","pbrMetallicRoughness":{"baseColorFactor":[0.9,0.8,0.7,0.5]}}],
"meshes":[{"name":"A","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]},
{"name":"B","primitives":[{"attributes":{"POSITION":0},"indices":1,"material":1}]}],
"nodes":[{"mesh":0},{"mesh":1}],"scenes":[{"nodes":[0,1]}],"scene":0})");
        }
        AssetID External()
        {
            MaterialSource source;
            source.Name                    = "External";
            source.Parameters["BaseColor"] = {1, 0, 0, 1};
            Write("external.material", SerializeMaterialSource(source));
            EXPECT_TRUE(std::holds_alternative<std::monostate>(Owner->RefreshAssets()));
            const auto result = Build("external.material");
            return AssetID(ID("external.material"), result.Root.LocalIDs.at(MaterialImporter::RootObjectKey));
        }
        void Remap(const ModelMaterialSlot& slot, std::optional<AssetID> id)
        {
            EXPECT_TRUE(std::holds_alternative<std::monostate>(Owner->SetModelMaterialRemap(ID(), slot.Key, id)));
        }
    };

    TEST_F(ModelMaterialRemappingTest, RemapBuildsItsInputAndSurvivesReopenWhileEmbeddedIdentityStaysPublished)
    {
        // Catches hidden dependency, ignored mapping, lost sidecar setting, and removed embedded Product.
        Gltf();
        Open();
        Build();
        const auto     slot     = Slot("Cutout");
        const auto     external = External();
        MaterialSource changed;
        changed.Name                    = "Edited external";
        changed.Parameters["BaseColor"] = {0, 1, 0, 1};
        Write("external.material", SerializeMaterialSource(changed));
        Remap(slot, external);
        auto built = Build();
        EXPECT_EQ(built.Root.Disposition, AssetBuildDisposition::Published);
        const auto dep = std::find_if(built.Dependencies.begin(), built.Dependencies.end(), [&](const auto& input) {
            return input.Source == external.GetSourceID();
        });
        ASSERT_NE(dep, built.Dependencies.end());
        EXPECT_EQ(dep->Disposition, AssetBuildDisposition::Published);
        const auto record = Owner->GetProductStore().FindBuild(ID(), "default");
        ASSERT_TRUE(record);
        EXPECT_NE(std::find(record->ProductInputs.begin(), record->ProductInputs.end(), external),
                  record->ProductInputs.end());
        EXPECT_EQ(Material(external).BaseColor, (std::array<float, 4> {0, 1, 0, 1}));
        Open();
        EXPECT_EQ(Slot("Cutout").Remap, external);
        EXPECT_EQ(Build().Root.Disposition, AssetBuildDisposition::UpToDate);
        AssetBuildOptions forced;
        forced.Force          = true;
        const auto reimported = Value(Owner->BuildAsset(ID(), forced));
        EXPECT_EQ(reimported.Root.Disposition, AssetBuildDisposition::Published);
        const auto meshes = Meshes();
        ASSERT_EQ(meshes.size(), 2u);
        size_t mapped = 0;
        for (const auto& [id, mesh] : meshes)
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
            {
                EXPECT_EQ(mesh.Material, external);
                ++mapped;
                const auto published = Owner->GetRuntimeAssetRegistry()->Find(id);
                EXPECT_NE(std::find(published->Metadata.Dependencies.begin(),
                                    published->Metadata.Dependencies.end(),
                                    external),
                          published->Metadata.Dependencies.end());
            }
        EXPECT_EQ(mapped, 1u);
        EXPECT_EQ(Material(slot.EmbeddedMaterial).Render.AlphaMode, MaterialAlphaMode::Mask);
        changed.Parameters["BaseColor"] = {0, 0, 1, 1};
        Write("external.material", SerializeMaterialSource(changed));
        EXPECT_EQ(Build().Root.Disposition, AssetBuildDisposition::Published);
        EXPECT_EQ(Material(external).BaseColor, (std::array<float, 4> {0, 0, 1, 1}));
        for (const auto& [id, mesh] : Meshes())
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                EXPECT_EQ(mesh.Material, external);
        Remap(slot, std::nullopt);
        EXPECT_EQ(Build().Root.Disposition, AssetBuildDisposition::Published);
        for (const auto& [id, mesh] : Meshes())
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                EXPECT_EQ(mesh.Material, slot.EmbeddedMaterial);
    }

    TEST_F(ModelMaterialRemappingTest, UnavailableWrongTypeAndUnpublishedRemapsWarnAndKeepDrawingEmbeddedMaterial)
    {
        // Optional remaps must not abort the model, including a valid source that emits no requested Product.
        Gltf();
        Open();
        Build();
        const auto    slot       = Slot("Cutout");
        const auto    external   = External();
        const auto    textureIDs = Build("pixel.tga").Root.LocalIDs;
        const AssetID texture(ID("pixel.tga"), textureIDs.at("texture:root"));
        for (AssetID bad : {AssetID(SourceAssetID::Generate(), 1),
                            texture,
                            AssetID(external.GetSourceID(), external.GetLocalID() + 100)})
        {
            Remap(slot, bad);
            const auto built = Build();
            ASSERT_FALSE(built.Root.Warnings.empty());
            EXPECT_TRUE(std::any_of(built.Root.Warnings.begin(), built.Root.Warnings.end(), [&](const auto& warning) {
                return warning.find(bad.ToString()) != std::string::npos &&
                       warning.find("embedded") != std::string::npos;
            }));
            for (const auto& [id, mesh] : Meshes())
                if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                    EXPECT_EQ(mesh.Material, slot.EmbeddedMaterial);
        }
        // Present bytes must warn when KMAT decoding fails, beyond discovery's type check.
        Remap(slot, external);
        auto importer      = std::make_shared<MalformedInputModelImporter>();
        importer->Material = external;
        auto importers     = Value(CreateDefaultImporterRegistry());
        ASSERT_TRUE(std::holds_alternative<std::monostate>(
            importers.Register("MalformedInputModel", {".gltf"}, importer, false)));
        auto configuration     = Owner->GetSourceAssetDatabase().FindByID(ID()).Import;
        configuration.Importer = "MalformedInputModel";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), configuration)));
        const auto built = Value(Owner->BuildAsset(ID(), {}, importers));
        EXPECT_TRUE(std::any_of(built.Root.Warnings.begin(), built.Root.Warnings.end(), [&](const auto& warning) {
            return warning.find(slot.Key) != std::string::npos &&
                   warning.find(external.ToString()) != std::string::npos &&
                   warning.find("embedded") != std::string::npos;
        }));
        for (const auto& [id, mesh] : Meshes())
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                EXPECT_EQ(mesh.Material, slot.EmbeddedMaterial);
        configuration.Importer.clear();
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), configuration)));
        // A failed material rebuild must not silently bind its previous published revision.
        Write("external.material", "broken material source");
        Remap(slot, external);
        EXPECT_FALSE(Build().Root.Warnings.empty());
        for (const auto& [id, mesh] : Meshes())
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                EXPECT_EQ(mesh.Material, slot.EmbeddedMaterial);
    }

    TEST_F(ModelMaterialRemappingTest, PublishedRemappedMeshesKeepSlotOverridesAndPartOverridePrecedence)
    {
        Gltf();
        Open();
        Build();
        const auto slot     = Slot("Cutout");
        const auto external = External();
        Remap(slot, external);
        Build();
        ProductModelComponent component;
        const AssetID         slotChoice(SourceAssetID::Generate(), 1), partChoice(SourceAssetID::Generate(), 1);
        component.SetOverride(slot.EmbeddedMaterial, slotChoice);
        size_t covered = 0;
        for (const auto& [id, mesh] : Meshes())
        {
            if (mesh.Material != external)
                continue;
            // Old products are reimported; v3 persists the authored slot separately
            // from its remapped default. Preserve the geometry round-trip check.
            const auto bytes     = Bytes(id);
            const auto persisted = Value(DecodeMeshProduct(bytes.data(), bytes.size()));
            EXPECT_EQ(persisted.AuthoredMaterial, slot.EmbeddedMaterial);
            EXPECT_EQ(persisted.Indices, mesh.Indices);
            EXPECT_EQ(component.ResolveMaterial(id, mesh.Material, mesh.AuthoredMaterial), slotChoice);
            component.SetOverride(id, partChoice);
            EXPECT_EQ(component.ResolveMaterial(id, mesh.Material, mesh.AuthoredMaterial), partChoice);
            ++covered;
        }
        EXPECT_EQ(covered, 1u);
    }

    TEST_F(ModelMaterialRemappingTest,
           ExtractionPreservesAllTextureBindingsParametersAndRenderStateThenEditsReachTheModel)
    {
        // Checks actual rebuilt KMAT uniform bytes, not merely serialized names or defaults.
        Gltf();
        Open();
        Build();
        const auto slot     = Slot("Cutout");
        const auto original = Material(slot.EmbeddedMaterial);
        Write("extracted/model_Cutout.material", "sentinel: do not overwrite\n");
        const auto extracted = Value(Owner->ExtractModelMaterials(ID(), "extracted", {slot.Key}));
        ASSERT_FALSE(extracted.Error);
        ASSERT_TRUE(extracted.RemapsApplied);
        ASSERT_EQ(extracted.Created.size(), 1u);
        const auto created = extracted.Created.front();
        EXPECT_EQ(created.File.filename(), "model_Cutout_1.material");
        EXPECT_EQ(Read(Root / "assets/extracted/model_Cutout.material"), "sentinel: do not overwrite\n");
        EXPECT_TRUE(fs::is_regular_file(created.File.string() + ".kmeta"));
        auto sourceText = Read(created.File);
        auto source =
            Value(ParseMaterialSource(reinterpret_cast<const std::byte*>(sourceText.data()), sourceText.size()));
        EXPECT_EQ(source.Shader, original.Shader);
        EXPECT_EQ(source.Parameters.at("EmissiveColor"), (std::vector<double> {0.25, 0.5, 0.75}));
        const auto built = Build();
        EXPECT_TRUE(std::any_of(built.Dependencies.begin(), built.Dependencies.end(), [&](const auto& dependency) {
            return dependency.Source == created.Source && dependency.Disposition == AssetBuildDisposition::Published;
        }));
        const auto editable = Material(created.Material);
        EXPECT_EQ(editable.Render.AlphaMode, MaterialAlphaMode::Mask);
        EXPECT_FLOAT_EQ(editable.Render.AlphaCutoff, 0.375f);
        EXPECT_TRUE(editable.Render.DoubleSided);
        ASSERT_EQ(editable.Textures.size(), 5u);
        for (const auto& binding : original.Textures)
            EXPECT_TRUE(std::any_of(editable.Textures.begin(), editable.Textures.end(), [&](const auto& other) {
                return other.Semantic == binding.Semantic && other.Texture == binding.Texture;
            }));
        ASSERT_TRUE(original.Bindings && editable.Bindings);
        const auto before = Value(PackMaterialUniforms(original, GetStandardShader().Code, *original.Bindings));
        const auto after  = Value(PackMaterialUniforms(editable, GetStandardShader().Code, *editable.Bindings));
        ASSERT_EQ(before.size(), after.size());
        EXPECT_EQ(before.at(0).Bytes, after.at(0).Bytes);
        for (const auto& [id, mesh] : Meshes())
            if (mesh.AuthoredMaterial == slot.EmbeddedMaterial)
                EXPECT_EQ(mesh.Material, created.Material);
        source.Parameters["BaseColor"] = {1, 0.1, 0.2, 1};
        Write(created.File.lexically_relative(Root / "assets"), SerializeMaterialSource(source));
        Build(created.File.lexically_relative(Root / "assets").generic_string());
        EXPECT_EQ(Material(created.Material).BaseColor, (std::array<float, 4> {1, 0.1f, 0.2f, 1}));
        const auto again = Value(Owner->ExtractModelMaterials(ID(), "extracted", {slot.Key}));
        EXPECT_TRUE(again.Created.empty());
        EXPECT_EQ(again.Skipped, (std::vector<std::string> {slot.Key}));
    }

    TEST_F(ModelMaterialRemappingTest, ExtractionUsesActualCustomShaderAndItsSignedAndArrayParameters)
    {
        // Hardcoding Standard's ID, schema, or uniform layout loses this published material.
        Gltf();
        Write("custom.vert", TestMaterialVertex);
        Write("custom.frag", TestMaterialFragment);
        Write("custom.shaderprog", "KansShaderProgram 1\nvertex=custom.vert\nfragment=custom.frag\n");
        Open();
        AssetBuildOptions options;
        options.Target            = EditorAssetTarget;
        const auto    shaderBuild = Build("custom.shaderprog", EditorAssetTarget);
        const AssetID shaderID(ID("custom.shaderprog"), shaderBuild.Root.LocalIDs.at("shader:root"));
        auto          importer = std::make_shared<ShaderModelImporter>();
        importer->Shader       = shaderID;
        auto importers         = Value(CreateDefaultImporterRegistry());
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(importers.Register("ShaderModel", {".gltf"}, importer, false)));
        ASSERT_TRUE(
            std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), {"ShaderModel", {}})));
        ASSERT_TRUE(std::holds_alternative<AssetBuildGraphResult>(Owner->BuildAsset(ID(), options, importers)));
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), {"", {}})));
        const auto slot     = Slot("Cutout", EditorAssetTarget);
        const auto embedded = Material(slot.EmbeddedMaterial, EditorAssetTarget);
        ASSERT_EQ(embedded.Shader, shaderID);
        const auto report = Value(Owner->ExtractModelMaterials(ID(), "custom", {slot.Key}, EditorAssetTarget));
        ASSERT_FALSE(report.Error);
        ASSERT_EQ(report.Created.size(), 1u);
        const auto created = report.Created[0];
        const auto text    = Read(created.File);
        const auto source  = Value(ParseMaterialSource(reinterpret_cast<const std::byte*>(text.data()), text.size()));
        EXPECT_EQ(source.Shader, shaderID);
        EXPECT_EQ(source.Parameters.at("Steps"), (std::vector<double> {-2147483647}));
        EXPECT_EQ(source.Parameters.at("Tint"), (std::vector<double> {0.25, 0.5, 0.75, 1}));
        ASSERT_EQ(source.Parameters.at("Offsets").size(), 4u);
        const std::array<float, 4> offsets {0.1f, 0.2f, 0.3f, -0.0f};
        for (size_t i = 0; i < offsets.size(); ++i)
            EXPECT_FLOAT_EQ(source.Parameters.at("Offsets")[i], offsets[i]);
        EXPECT_EQ(source.Textures.at("u_Albedo"), embedded.Textures.at(0).Texture);
        Build(created.File.lexically_relative(Root / "assets").generic_string(), EditorAssetTarget);
        const auto extracted = Material(created.Material, EditorAssetTarget);
        EXPECT_EQ(extracted.Shader, shaderID);
        const auto code   = Bytes(shaderID, EditorAssetTarget);
        const auto shader = Value(DecodeShaderProduct(code.data(), code.size()));
        // Packed equality catches integer rounding and loss of the array's signed zero.
        const auto before = Value(PackMaterialUniforms(embedded, shader.Code, *embedded.Bindings));
        const auto after  = Value(PackMaterialUniforms(extracted, shader.Code, *extracted.Bindings));
        EXPECT_EQ(before.at(0).Bytes, after.at(0).Bytes);
    }

    TEST_F(ModelMaterialRemappingTest, DuplicateNamesRemainDistinctSlotsAndOnlySelectedSlotIsRemapped)
    {
        Gltf("Cutout");
        Open();
        Build();
        std::vector<ModelMaterialSlot> duplicates;
        for (const auto& slot : Slots())
            if (slot.DisplayName == "Cutout")
                duplicates.push_back(slot);
        ASSERT_EQ(duplicates.size(), 2u);
        ASSERT_NE(duplicates[0].Key, duplicates[1].Key);
        const auto external = External();
        Remap(duplicates[1], external);
        Build();
        size_t untouched = 0, mapped = 0;
        for (const auto& [id, mesh] : Meshes())
        {
            if (mesh.AuthoredMaterial == duplicates[0].EmbeddedMaterial)
            {
                EXPECT_EQ(mesh.Material, duplicates[0].EmbeddedMaterial);
                ++untouched;
            }
            if (mesh.AuthoredMaterial == duplicates[1].EmbeddedMaterial)
            {
                EXPECT_EQ(mesh.Material, external);
                ++mapped;
            }
        }
        EXPECT_EQ(untouched, 1u);
        EXPECT_EQ(mapped, 1u);
        const auto extracted = Value(Owner->ExtractModelMaterials(ID(), "materials"));
        ASSERT_FALSE(extracted.Error);
        EXPECT_NE(std::find(extracted.Skipped.begin(), extracted.Skipped.end(), duplicates[1].Key),
                  extracted.Skipped.end());
        EXPECT_TRUE(std::any_of(extracted.Created.begin(), extracted.Created.end(), [&](const auto& created) {
            return created.SlotKey == duplicates[0].Key;
        }));
        for (const auto& slot : Slots())
            if (slot.Key == duplicates[1].Key)
                EXPECT_EQ(slot.Remap, external);
    }

    TEST_F(ModelMaterialRemappingTest, SidecarFailureReportsWrittenFilesAndCommitsNoneOfTheRemaps)
    {
        // A failed model-sidecar save after registering materials must leave all slots unmapped.
        Gltf();
        Open();
        Build();
        const std::vector<std::string> requested {Slot("Glass").Key, Slot("Cutout").Key};
        const auto                     before = Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings;
        fs::rename(Root / "assets/model.gltf.kmeta", Root / "saved.kmeta");
        fs::create_directory(Root / "assets/model.gltf.kmeta");
        const auto report = Value(Owner->ExtractModelMaterials(ID(), "materials", requested));
        ASSERT_TRUE(report.Error);
        EXPECT_FALSE(report.RemapsApplied);
        ASSERT_EQ(report.Created.size(), 2u);
        for (const auto& created : report.Created)
            EXPECT_TRUE(fs::is_regular_file(created.File));
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings, before);
        fs::remove(Root / "assets/model.gltf.kmeta");
        fs::rename(Root / "saved.kmeta", Root / "assets/model.gltf.kmeta");
        Open();
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings, before);
        for (const auto& slot : Slots())
            EXPECT_FALSE(slot.Remap);
    }

#ifdef __linux__
    TEST_F(ModelMaterialRemappingTest, SecondRegistrationFailureReportsBothFilesAndPersistsNoSlotRemap)
    {
        // Saving each remap during registration would commit the first slot when the second fails.
        const std::string longName(80, 'Z');
        Gltf(longName);
        Open();
        Build();
        const std::vector<std::string> requested {Slot("Cutout").Key, Slot(longName).Key};
        const auto                     before        = Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings;
        const auto                     sidecarBefore = Read(Root / "assets/model.gltf.kmeta");
        const auto                     pathLimit     = pathconf((Root / "assets").c_str(), _PC_PATH_MAX);
        ASSERT_GT(pathLimit, 1024);
        // Both material files and sidecar names fit. Only the longer material's
        // .kmeta.tmp-<id>/payload exceeds the filesystem's pathname limit.
        const auto temporarySuffix = std::string("/model_") + longName + ".material.kmeta.tmp-" + std::string(16, '0');
        const auto directoryLength = size_t(pathLimit - 1) - temporarySuffix.size();
        fs::path   directory       = Root / "assets/registration";
        while (directoryLength - directory.string().size() > 201)
            directory /= std::string(200, 'd');
        const auto remaining = directoryLength - directory.string().size();
        if (remaining == 1)
            directory += 'd';
        else if (remaining > 1)
            directory /= std::string(remaining - 1, 'd');
        fs::create_directories(directory);
        const auto report = Value(Owner->ExtractModelMaterials(ID(), directory, requested));
        ASSERT_TRUE(report.Error);
        EXPECT_FALSE(report.RemapsApplied);
        ASSERT_EQ(report.Created.size(), 2u);
        // Verify that the first registration finished before the second failed.
        EXPECT_TRUE(report.Created[0].Material.IsValid());
        EXPECT_FALSE(report.Created[1].Material.IsValid());
        EXPECT_NE(report.Error->Message.find(report.Created[1].File.string()), std::string::npos);
        for (const auto& created : report.Created)
            EXPECT_TRUE(fs::is_regular_file(created.File));
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings, before);
        EXPECT_EQ(Read(Root / "assets/model.gltf.kmeta"), sidecarBefore);
        Open();
        for (const auto& slot : Slots())
            EXPECT_FALSE(slot.Remap);
    }

    TEST_F(ModelMaterialRemappingTest, WriteFailureAfterFirstFileDoesNotApplyAnySlotRemap)
    {
        // A disk write failure on the second material must not leave the first slot remapped.
        Gltf();
        Open();
        Build();
        const std::vector<std::string> requested {Slot("Glass").Key, Slot("Cutout").Key};
        const auto                     baseline = Value(Owner->ExtractModelMaterials(ID(), "baseline", requested));
        ASSERT_FALSE(baseline.Error);
        ASSERT_EQ(baseline.Created.size(), 2u);
        for (const auto& slot : Slots())
            Remap(slot, std::nullopt);
        const auto before     = Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings;
        const auto firstSize  = fs::file_size(baseline.Created[0].File);
        const auto secondSize = fs::file_size(baseline.Created[1].File);
        // Leave enough room for a buggy incremental sidecar save, but fail the larger material.
        const auto limit = std::max(firstSize, fs::file_size(Root / "assets/model.gltf.kmeta") + 256);
        ASSERT_LT(limit, secondSize);
        struct FileLimit
        {
            rlimit                                  Previous {};
            decltype(std::signal(SIGXFSZ, SIG_IGN)) Handler;
            FileLimit() : Handler(std::signal(SIGXFSZ, SIG_IGN)) { getrlimit(RLIMIT_FSIZE, &Previous); }
            ~FileLimit()
            {
                setrlimit(RLIMIT_FSIZE, &Previous);
                std::signal(SIGXFSZ, Handler);
            }
        };
        ModelMaterialExtractionResult report;
        {
            FileLimit restore;
            rlimit    restricted {limit, restore.Previous.rlim_max};
            ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &restricted), 0);
            report = Value(Owner->ExtractModelMaterials(ID(), "limited", requested));
        }
        ASSERT_TRUE(report.Error);
        EXPECT_FALSE(report.RemapsApplied);
        ASSERT_EQ(report.Created.size(), 1u);
        EXPECT_TRUE(fs::is_regular_file(report.Created[0].File));
        EXPECT_EQ(Owner->GetSourceAssetDatabase().FindByID(ID()).Import.Settings, before);
        Open();
        for (const auto& slot : Slots())
            EXPECT_FALSE(slot.Remap);
    }
#endif

    TEST_F(ModelMaterialRemappingTest, ExtractionRejectsEscapingDirectoryAndMalformedRemapWithoutPublishing)
    {
        Gltf("Cloth/Layer:*?");
        Open();
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->GetModelMaterialSlots(ID())));
        Build();
        const auto slot    = Slot("Cutout");
        auto       invalid = Owner->ExtractModelMaterials(ID(), "../outside", {slot.Key});
        EXPECT_TRUE(std::holds_alternative<AssetError>(invalid));
        EXPECT_FALSE(fs::exists(Root / "outside"));
        fs::create_directory_symlink(Root, Root / "assets/escape");
        EXPECT_TRUE(std::holds_alternative<AssetError>(Owner->ExtractModelMaterials(ID(), "escape", {slot.Key})));
        EXPECT_TRUE(std::holds_alternative<AssetError>(
            Owner->SetModelMaterialRemap(ID(), "material:missing", AssetID(SourceAssetID::Generate(), 1))));
        const auto before                       = Owner->GetProductStore().FindBuild(ID(), "default")->Fingerprint;
        auto       config                       = Owner->GetSourceAssetDatabase().FindByID(ID()).Import;
        config.Settings["material." + slot.Key] = "not an AssetID";
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), config)));
        const auto failed = Owner->BuildAsset(ID(), {});
        ASSERT_TRUE(std::holds_alternative<AssetError>(failed));
        EXPECT_EQ(Owner->GetProductStore().FindBuild(ID(), "default")->Fingerprint, before);
        config.Settings.erase("material." + slot.Key);
        ASSERT_TRUE(std::holds_alternative<std::monostate>(Owner->SetSourceImportConfiguration(ID(), config)));
        const auto cloth  = Slot("Cloth/Layer:*?");
        const auto report = Value(Owner->ExtractModelMaterials(ID(), "safe", {cloth.Key}));
        ASSERT_FALSE(report.Error);
        ASSERT_EQ(report.Created.size(), 1u);
        EXPECT_EQ(report.Created[0].File.filename(), "model_Cloth_Layer___.material");
        EXPECT_TRUE(fs::is_regular_file(report.Created[0].File));
    }

    TEST_F(ModelMaterialRemappingTest, ServiceRejectsSlotOperationsOnWorkersAndWhileBuilding)
    {
        Gltf();
        Open();
        Build("model.gltf", EditorAssetTarget);
        const auto model   = ID();
        auto       service = Value(EditorAssetImportService::Create(std::move(Owner)));
        const auto slots   = Value(service->GetModelMaterialSlots(model));
        ASSERT_FALSE(slots.empty());
        const auto key    = slots.front().Key;
        auto       worker = std::async(std::launch::async, [&] {
            EXPECT_TRUE(std::holds_alternative<AssetError>(service->GetModelMaterialSlots(model)));
            EXPECT_TRUE(std::holds_alternative<AssetError>(service->SetModelMaterialRemap(model, key, std::nullopt)));
            EXPECT_TRUE(std::holds_alternative<AssetError>(service->ExtractModelMaterials(model, "materials")));
        });
        worker.get();
        std::promise<void> entered, release;
        auto               released = release.get_future().share();
        AssetBuildOptions  options;
        options.Target = EditorAssetTarget;
        options.Force  = true;
        std::atomic<bool> visited {false};
        options.Progress = [&](PublicationPhase phase) {
            if (phase == PublicationPhase::BeforeCommit && !visited.exchange(true))
            {
                entered.set_value();
                released.wait_for(std::chrono::seconds(10));
            }
            return true;
        };
        ASSERT_TRUE(std::holds_alternative<std::monostate>(service->Start(model, options)));
        EXPECT_EQ(entered.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);
        EXPECT_TRUE(std::holds_alternative<AssetError>(service->GetModelMaterialSlots(model)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(service->SetModelMaterialRemap(model, key, std::nullopt)));
        EXPECT_TRUE(std::holds_alternative<AssetError>(service->ExtractModelMaterials(model, "materials")));
        release.set_value();
        ASSERT_TRUE(std::holds_alternative<std::monostate>(service->Shutdown()));
        EXPECT_FALSE(fs::exists(Root / "assets/materials"));
        service.reset();
    }
} // namespace
