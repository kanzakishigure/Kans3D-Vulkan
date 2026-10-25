#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>
#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Importer/MaterialSource.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Editor/EditorAssetImportService.h"
#include "MaterialShaderTestUtilities.h"

namespace
{
    using namespace Kans;
    namespace fs = std::filesystem;

    template<typename T>
    T Value(std::variant<T, AssetError> result)
    {
        if (const auto* error = std::get_if<AssetError>(&result))
            throw std::runtime_error(error->Message);
        return std::get<T>(std::move(result));
    }

    MaterialSource Parse(const std::string& text)
    {
        return Value(ParseMaterialSource(reinterpret_cast<const std::byte*>(text.data()), text.size()));
    }

    class MaterialInstanceTest : public testing::Test
    {
    protected:
        fs::path                             Root;
        Scope<Project>                       Owner;
        Scope<EditorAssetImportService>      Service;
        std::map<std::string, SourceAssetID> Sources;
        const std::string                    Header = "magic: KansMaterial\nversion: 4\n";

        void SetUp() override
        {
            if (!Log::GetCoreLogger())
                Log::Init();
            Root = fs::temp_directory_path() / ("kans-instances-" + SourceAssetID::Generate().ToString());
            fs::create_directories(Root / "assets");
        }
        void TearDown() override
        {
            Service.reset();
            Owner.reset();
            std::error_code ignored;
            fs::remove_all(Root, ignored);
        }
        void Write(const std::string& file, const std::string& text)
        {
            std::ofstream(Root / "assets" / file, std::ios::binary | std::ios::trunc) << text;
        }
        AssetID Define(const std::string& file, const std::string& text, const std::string& key = "material:root")
        {
            const auto source = SourceAssetID::Generate();
            Sources.emplace(file, source);
            Write(file, text);
            Write(file + ".kmeta",
                  "version: 3\nsource_id: " + source.ToString() +
                      "\nimport: {importer: '', settings: {}}\nlocal_ids: {'" + key + "': '0000000000000001'}\n");
            return {source, 1};
        }
        AssetID Texture(const std::string& file, uint8_t color)
        {
            std::string tga(18, '\0');
            tga[2] = 2, tga[12] = 1, tga[14] = 1, tga[16] = 32, tga[17] = 8;
            tga += {char(color), char(color), char(color), char(255)};
            return Define(file, tga, "texture:root");
        }
        void Open()
        {
            ProjectConfig config;
            config.Name          = "Material instances";
            config.RootDirectory = Root;
            Owner                = Value(Project::Open(config));
        }
        std::string Instance(AssetID parent, const std::string& overrides = {})
        {
            return Header + "parent: " + parent.ToString() + "\n" + overrides;
        }
        AssetBuildGraphResult Build(const std::string& file, AssetBuildOptions options = {})
        {
            return Value(Owner->BuildAsset(Sources.at(file), options));
        }
        ProductRecord Record(AssetID id, const std::string& target = "default")
        {
            const auto registry = Owner ? Owner->GetRuntimeAssetRegistry() : Service->GetRuntimeAssetRegistry();
            const auto record   = registry->Find(id, target);
            if (!record)
                throw std::runtime_error("Expected published Product " + id.ToString());
            return *record;
        }
        MaterialData Material(AssetID id, const std::string& target = "default")
        {
            const auto directory = Owner ? Owner->GetProductDirectory() : Service->GetProductDirectory();
            const auto bytes     = Value(ReadRegisteredProduct(directory, Record(id, target)));
            return Value(DecodeMaterialProduct(bytes.data(), bytes.size()));
        }
        MaterialSource Named(AssetID id, const ShaderData& shader = GetStandardShader())
        {
            return Value(EditableMaterial(Material(id), shader));
        }
        EditorAssetImportSnapshot Finish()
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (Value(Service->Poll()))
                    return Value(Service->GetSnapshot());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            throw std::runtime_error("Material instance service build timed out");
        }
    };

    TEST_F(MaterialInstanceTest, BuildsUnpublishedParentChainWithOverridesClearsAndPerFieldRenderState)
    {
        // Catches flattening from defaults, ignoring overrides/clears, or replacing the entire render state.
        const auto albedo      = Texture("albedo.tga", 50);
        const auto normal      = Texture("normal.tga", 128);
        const auto replacement = Texture("replacement.tga", 200);
        const auto parent =
            Define("parent.material",
                   "magic: KansMaterial\nversion: 3\nparameters: {BaseColor: [0.2, 0.4, 0.6, 0.75], Metallic: 0.7, "
                   "Roughness: 0.8}\n"
                   "render: {alpha_mode: mask, alpha_cutoff: 0.375, double_sided: true}\ntextures: {BaseColorMap: " +
                       albedo.ToString() + ", NormalMap: " + normal.ToString() + "}\n");
        const auto child = Define("child.material",
                                  Instance(parent,
                                           "parameters: {Roughness: 0.25}\ntextures: {NormalMap: null, BaseColorMap: " +
                                               replacement.ToString() + "}\nrender: {double_sided: false}\n"));
        const auto grandchild = Define("grandchild.material",
                                       Instance(child, "parameters: {Metallic: 0.125}\nrender: {alpha_cutoff: 0}\n"));
        Open();
        const auto graph = Build("grandchild.material");
        const auto data  = Material(grandchild);
        EXPECT_EQ(data.Shader, StandardShaderID());
        EXPECT_EQ(data.BaseColor, (std::array<float, 4> {0.2f, 0.4f, 0.6f, 0.75f}));
        EXPECT_FLOAT_EQ(data.Roughness, 0.25f);
        EXPECT_FLOAT_EQ(data.Metallic, 0.125f);
        ASSERT_EQ(data.Textures.size(), 1u);
        EXPECT_EQ(data.Textures[0].Texture, replacement);
        EXPECT_EQ(data.Render.AlphaMode, MaterialAlphaMode::Mask);
        EXPECT_FLOAT_EQ(data.Render.AlphaCutoff, 0.0f);
        EXPECT_FALSE(data.Render.DoubleSided);
        EXPECT_FLOAT_EQ(Material(child).Metallic, 0.7f);
        EXPECT_FLOAT_EQ(Material(child).Render.AlphaCutoff, 0.375f);
        const auto childPosition =
            std::find_if(graph.Dependencies.begin(), graph.Dependencies.end(), [&](const auto& result) {
                return result.Source == child.GetSourceID();
            });
        const auto parentPosition =
            std::find_if(graph.Dependencies.begin(), graph.Dependencies.end(), [&](const auto& result) {
                return result.Source == parent.GetSourceID();
            });
        ASSERT_NE(childPosition, graph.Dependencies.end());
        EXPECT_LT(parentPosition, childPosition);
        const auto plan = Value(Owner->PlanSourceAssetDeletion(parent.GetSourceID()));
        EXPECT_TRUE(std::any_of(plan.Dependents.begin(), plan.Dependents.end(), [&](const auto& source) {
            return source.sourceAssetID == child.GetSourceID();
        }));
    }

    TEST_F(MaterialInstanceTest, InheritsCustomShaderPackedValuesAndSupportsUnsavedPreview)
    {
        // Catches using Standard schema/defaults for a custom parent or losing packed array/integer values.
        Write("custom.vert", TestMaterialVertex);
        Write("custom.frag", TestMaterialFragment);
        const auto shaderID =
            Define("custom.shaderprog",
                   "KansShaderProgram 1\nvertex=custom.vert\nfragment=custom.frag\ndefault Strength = 0.625\n",
                   "shader:root");
        const auto texture = Texture("albedo.tga", 30);
        const auto parent =
            Define("parent.material",
                   Header + "shader: " + shaderID.ToString() +
                       "\nparameters: {Tint: [0.25, 0.5, 0.75, 1], Steps: 16777217, Offsets: [[1, 2], [3, 4]]}\n"
                       "textures: {u_Albedo: " +
                       texture.ToString() + "}\nrender: {alpha_mode: blend, double_sided: true}\n");
        const auto child = Define("child.material", Instance(parent, "parameters: {Tint: [1, 0, 0, 1]}\n"));
        Open();
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        Build("child.material", options);
        const auto code = Value(ReadRegisteredProduct(Owner->GetProductDirectory(), Record(shaderID, options.Target)));
        const auto shader = Value(DecodeShaderProduct(code.data(), code.size()));
        const auto data   = Material(child, options.Target);
        EXPECT_EQ(data.Shader, shaderID);
        const auto named = Value(EditableMaterial(data, shader));
        EXPECT_EQ(named.Parameters.at("Tint"), (std::vector<double> {1, 0, 0, 1}));
        EXPECT_EQ(named.Parameters.at("Steps"), (std::vector<double> {16777217}));
        EXPECT_EQ(named.Parameters.at("Offsets"), (std::vector<double> {1, 2, 3, 4}));
        EXPECT_EQ(named.Parameters.at("Strength"), (std::vector<double> {0.625}));
        EXPECT_EQ(named.Textures.at("u_Albedo"), texture);
        const auto unsaved      = Parse(Instance(
            parent, "parameters: {Strength: 0.125}\ntextures: {u_Albedo: ''}\nrender: {alpha_mode: opaque}\n"));
        const auto effective    = Value(ApplyMaterialOverrides(
            Value(EditableMaterial(Material(parent, options.Target), shader)), unsaved, &shader));
        const auto preview      = Value(ResolveMaterialSource(effective, &shader));
        const auto previewNamed = Value(EditableMaterial(preview, shader));
        EXPECT_EQ(previewNamed.Parameters.at("Strength"), (std::vector<double> {0.125}));
        EXPECT_EQ(previewNamed.Parameters.at("Tint"), (std::vector<double> {0.25, 0.5, 0.75, 1}));
        EXPECT_TRUE(preview.Textures.empty());
        EXPECT_EQ(preview.Render.AlphaMode, MaterialAlphaMode::Opaque);
        EXPECT_TRUE(preview.Render.DoubleSided);
        EXPECT_TRUE(std::holds_alternative<AssetError>(ResolveMaterialSource(unsaved, &shader)));
    }

    TEST_F(MaterialInstanceTest, SerializationKeepsOnlyOverridesAndSavedSourceBuildsTheSameInstance)
    {
        // Catches writing effective values as overrides, or dropping explicit false, zero and cleared textures.
        const auto texture = Texture("albedo.tga", 100);
        const auto parent  = Define(
            "parent.material",
            "magic: KansMaterial\nversion: 2\nparameters: {metallic: 0.75, roughness: 0.875}\ntextures: {base_color: " +
                texture.ToString() + "}\n");
        const auto source = Parse(Instance(parent,
                                           "parameters: {Roughness: 0.125}\ntextures: {BaseColorMap: null}\nrender: "
                                           "{alpha_cutoff: 0, double_sided: false}\n"));
        const auto text   = SerializeMaterialSource(source);
        const auto yaml   = YAML::Load(text);
        EXPECT_EQ(yaml["version"].as<int>(), 4);
        EXPECT_EQ(yaml["parent"].as<std::string>(), parent.ToString());
        EXPECT_FALSE(yaml["shader"]);
        ASSERT_EQ(yaml["parameters"].size(), 1u);
        EXPECT_EQ(yaml["parameters"]["Roughness"].as<double>(), 0.125);
        ASSERT_EQ(yaml["textures"].size(), 1u);
        EXPECT_TRUE(yaml["textures"]["BaseColorMap"].IsNull());
        ASSERT_EQ(yaml["render"].size(), 2u);
        EXPECT_FALSE(yaml["render"]["alpha_mode"]);
        const auto again = Parse(text);
        ASSERT_TRUE(again.RenderOverrides.AlphaCutoff);
        ASSERT_TRUE(again.RenderOverrides.DoubleSided);
        EXPECT_EQ(*again.RenderOverrides.AlphaCutoff, 0.0f);
        EXPECT_FALSE(*again.RenderOverrides.DoubleSided);
        EXPECT_FALSE(again.Textures.at("BaseColorMap").IsValid());
        const auto child = Define("child.material", text);
        Open();
        Build("child.material");
        const auto data = Material(child);
        EXPECT_FLOAT_EQ(data.Metallic, 0.75f);
        EXPECT_FLOAT_EQ(data.Roughness, 0.125f);
        EXPECT_TRUE(data.Textures.empty());
        EXPECT_FLOAT_EQ(data.Render.AlphaCutoff, 0.0f);
    }

    TEST_F(MaterialInstanceTest, ServicePropagatesParentEditsThroughGrandchildrenAndLeavesUnrelatedProductsAlone)
    {
        // Catches rebuilding only the root/direct child, ignoring a new inherited texture, or rebuilding all sources.
        const auto texture    = Texture("new.tga", 180);
        const auto parent     = Define("parent.material", Header + "parameters: {Metallic: 0.25, Roughness: 0.875}\n");
        const auto child      = Define("child.material", Instance(parent, "parameters: {Roughness: 0.125}\n"));
        const auto grandchild = Define("grandchild.material", Instance(child));
        const auto unrelated  = Define("unrelated.material", Header + "parameters: {Metallic: 0.5}\n");
        const auto otherOnly  = Define("other.material", Instance(parent));
        Open();
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        Build("grandchild.material", options);
        Build("unrelated.material", options);
        Build("other.material"); // This source is only published for a different target.
        const auto childBefore      = Record(child, options.Target);
        const auto grandchildBefore = Record(grandchild, options.Target);
        const auto unrelatedBefore  = Record(unrelated, options.Target);
        const auto otherBefore      = Record(otherOnly);
        Write("parent.material",
              Header +
                  "parameters: {Metallic: 0.75, Roughness: 0.625}\n"
                  "textures: {BaseColorMap: " +
                  texture.ToString() + "}\nrender: {alpha_mode: blend, double_sided: true}\n");
        Service                   = Value(EditorAssetImportService::Create(std::move(Owner)));
        options.RebuildDependents = true;
        Value(Service->Start(parent.GetSourceID(), options));
        const auto done = Finish();
        ASSERT_EQ(done.State, EditorAssetImportState::Succeeded) << (done.Error ? done.Error->Message : "");
        ASSERT_TRUE(done.Result);
        EXPECT_NE(Record(child, options.Target).Location.ContentDigest, childBefore.Location.ContentDigest);
        EXPECT_NE(Record(grandchild, options.Target).Location.ContentDigest, grandchildBefore.Location.ContentDigest);
        for (const auto id : {child, grandchild})
        {
            const auto data = Material(id, options.Target);
            EXPECT_FLOAT_EQ(data.Metallic, 0.75f);
            EXPECT_FLOAT_EQ(data.Roughness, 0.125f);
            ASSERT_EQ(data.Textures.size(), 1u);
            EXPECT_EQ(data.Textures[0].Texture, texture);
            EXPECT_EQ(data.Render.AlphaMode, MaterialAlphaMode::Blend);
            EXPECT_TRUE(data.Render.DoubleSided);
            const auto snapshotRecord = done.Result->Root.Registry->Find(id, options.Target);
            ASSERT_TRUE(snapshotRecord);
            EXPECT_EQ(snapshotRecord->Location.ContentDigest, Record(id, options.Target).Location.ContentDigest);
        }
        EXPECT_EQ(Record(unrelated, options.Target).Location.RelativeFile, unrelatedBefore.Location.RelativeFile);
        EXPECT_EQ(Record(otherOnly).Location.RelativeFile, otherBefore.Location.RelativeFile);
        EXPECT_FALSE(Service->GetRuntimeAssetRegistry()->Find(otherOnly, options.Target));
        std::set<SourceAssetID> rebuilt;
        for (const auto& result : done.Result->Dependents)
            rebuilt.insert(result.Source);
        EXPECT_EQ(rebuilt, (std::set<SourceAssetID> {child.GetSourceID(), grandchild.GetSourceID()}));
    }

    TEST_F(MaterialInstanceTest, BrokenDependentKeepsItsProductAndDoesNotStopHealthyBranches)
    {
        // Catches propagation aborting at a broken sibling, hiding the root, or attempting its descendants.
        Write("custom.vert", TestMaterialVertex);
        Write("custom.frag", TestMaterialFragment);
        const auto shaderID = Define("custom.shaderprog",
                                     "KansShaderProgram 1\nvertex=custom.vert\nfragment=custom.frag\n"
                                     "default Tint = 0.2 0.4 0.6 1\ndefault Strength = 0.25\n",
                                     "shader:root");
        const auto parent   = Define("parent.material", Header + "shader: " + shaderID.ToString() + "\n");
        const auto first    = Define("first.material", Instance(parent));
        const auto second   = Define("second.material", Instance(parent));
        // The broken sibling must be visited first: otherwise an abort could still update the healthy one.
        const bool        firstBroken = first.GetSourceID() < second.GetSourceID();
        const auto        broken      = firstBroken ? first : second;
        const auto        healthy     = firstBroken ? second : first;
        const std::string brokenFile  = firstBroken ? "first.material" : "second.material";
        Write(brokenFile, Instance(parent, "parameters: {Strength: 0.75}\n"));
        const auto blocked     = Define("blocked.material", Instance(broken));
        const auto healthyLeaf = Define("healthy-leaf.material", Instance(healthy));
        Open();
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        Build("blocked.material", options);
        Build("healthy-leaf.material", options);
        const auto brokenBefore  = Record(broken, options.Target);
        const auto blockedBefore = Record(blocked, options.Target);
        const auto healthyBefore = Record(healthy, options.Target);
        const auto leafBefore    = Record(healthyLeaf, options.Target);

        std::string fragment = TestMaterialFragment;
        for (size_t offset = 0; (offset = fragment.find("Strength", offset)) != std::string::npos; offset += 5)
            fragment.replace(offset, 8, "Power");
        Write("custom.frag", fragment);
        Write("custom.shaderprog",
              "KansShaderProgram 1\nvertex=custom.vert\nfragment=custom.frag\n"
              "default Tint = 0.8 0.4 0.6 1\ndefault Power = 0.625\n");
        // A blocked descendant's own parse error must never replace the failed-parent diagnostic.
        Write("blocked.material", Header + "unknown: true\n");
        Service                   = Value(EditorAssetImportService::Create(std::move(Owner)));
        options.RebuildDependents = true;
        Value(Service->Start(parent.GetSourceID(), options));
        const auto done = Finish();
        ASSERT_EQ(done.State, EditorAssetImportState::Succeeded) << (done.Error ? done.Error->Message : "");
        ASSERT_TRUE(done.Result);
        EXPECT_FALSE(done.Error);
        EXPECT_EQ(done.Result->Root.Source, parent.GetSourceID());
        EXPECT_EQ(done.Result->Root.Disposition, AssetBuildDisposition::Published);
        EXPECT_EQ(Record(broken, options.Target).Location.RelativeFile, brokenBefore.Location.RelativeFile);
        EXPECT_EQ(Record(blocked, options.Target).Location.RelativeFile, blockedBefore.Location.RelativeFile);
        EXPECT_NE(Record(healthy, options.Target).Location.RelativeFile, healthyBefore.Location.RelativeFile);
        EXPECT_NE(Record(healthyLeaf, options.Target).Location.RelativeFile, leafBefore.Location.RelativeFile);
        const auto code =
            Value(ReadRegisteredProduct(Service->GetProductDirectory(), Record(shaderID, options.Target)));
        const auto shader = Value(DecodeShaderProduct(code.data(), code.size()));
        for (const auto id : {parent, healthy, healthyLeaf})
        {
            const auto effective = Value(EditableMaterial(Material(id, options.Target), shader));
            EXPECT_EQ(effective.Parameters.at("Power"), (std::vector<double> {0.625}));
            EXPECT_EQ(effective.Parameters.at("Tint"), (std::vector<double> {0.8f, 0.4f, 0.6f, 1}));
            EXPECT_EQ(done.Result->Root.Registry->Find(id, options.Target)->Location.ContentDigest,
                      Record(id, options.Target).Location.ContentDigest);
        }
        const auto& warnings = done.Result->Root.Warnings;
        ASSERT_EQ(warnings.size(), 2u);
        EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(), [&](const auto& warning) {
            return warning.find(brokenFile) != std::string::npos && warning.find("Strength") != std::string::npos;
        }));
        EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(), [&](const auto& warning) {
            return warning.find("Skipped dependent blocked.material") != std::string::npos &&
                   warning.find(brokenFile) != std::string::npos;
        }));
        std::set<SourceAssetID> rebuilt;
        for (const auto& result : done.Result->Dependents)
            rebuilt.insert(result.Source);
        EXPECT_EQ(rebuilt, (std::set<SourceAssetID> {healthy.GetSourceID(), healthyLeaf.GetSourceID()}));
    }

    TEST_F(MaterialInstanceTest, ParentCyclesReportTheOrderedChainAndNeverPublish)
    {
        const auto a = Define("a.material", Header);
        const auto b = Define("b.material", Instance(a));
        Open();
        for (const bool self : {true, false})
        {
            Write("a.material", Instance(self ? a : b));
            const auto result = Owner->BuildAsset(a.GetSourceID(), {});
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            const auto message = std::get<AssetError>(result).Message;
            EXPECT_NE(message.find("cycle"), std::string::npos) << message;
            const auto start = message.find("Source dependency cycle: a.material");
            ASSERT_NE(start, std::string::npos) << message;
            const auto end = message.find("-> a.material", start);
            EXPECT_NE(end, std::string::npos) << message;
            if (!self)
            {
                const auto middle = message.find("-> b.material", start);
                ASSERT_NE(middle, std::string::npos) << message;
                EXPECT_LT(middle, end);
            }
            EXPECT_FALSE(Owner->GetRuntimeAssetRegistry()->Find(a));
            EXPECT_FALSE(Owner->GetRuntimeAssetRegistry()->Find(b));
        }
    }

    TEST_F(MaterialInstanceTest, UnavailableOrWrongTypeParentsFailAndKeepThePreviousInstancePublished)
    {
        const auto parent  = Define("parent.material", Header + "parameters: {Metallic: 0.375}\n");
        const auto texture = Texture("texture.tga", 90);
        const auto child   = Define("child.material", Instance(parent));
        Open();
        Build("child.material");
        const auto before      = Record(child);
        const auto unknown     = AssetID(SourceAssetID::Generate(), 1);
        const auto unpublished = AssetID(parent.GetSourceID(), 99);
        for (const auto bad : {unknown, unpublished, texture})
        {
            Write("child.material", Instance(bad));
            const auto result = Owner->BuildAsset(child.GetSourceID(), {});
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            const auto message = std::get<AssetError>(result).Message;
            EXPECT_NE(message.find("Material parent"), std::string::npos) << message;
            EXPECT_NE(message.find(bad.ToString()), std::string::npos) << message;
            EXPECT_EQ(Record(child).Location.RelativeFile, before.Location.RelativeFile);
        }
        Write("child.material", Instance(parent));
        fs::remove(Root / "assets/parent.material");
        Value(Owner->RefreshAssets());
        const auto missing = Owner->BuildAsset(child.GetSourceID(), {});
        ASSERT_TRUE(std::holds_alternative<AssetError>(missing));
        EXPECT_NE(std::get<AssetError>(missing).Message.find("Missing"), std::string::npos);
        EXPECT_EQ(Record(child).Location.RelativeFile, before.Location.RelativeFile);
    }

    TEST_F(MaterialInstanceTest, InstancesRejectShaderDeclarationsEvenWhenEmpty)
    {
        const auto parent = Define("parent.material", Header);
        const auto child  = Define("child.material", Header);
        Open();
        for (const std::string shader : std::vector<std::string> {"''", "null", StandardShaderID().ToString()})
        {
            Write("child.material", Instance(parent, "shader: " + shader + "\n"));
            const auto result = Owner->BuildAsset(child.GetSourceID(), {});
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_NE(std::get<AssetError>(result).Message.find("both parent and shader"), std::string::npos);
            EXPECT_FALSE(Owner->GetRuntimeAssetRegistry()->Find(child));
        }
    }

    TEST_F(MaterialInstanceTest, InvalidOverridesFailBothImportAndUnsavedCompositionIncludingClearedTextures)
    {
        const auto parent = Define("parent.material", Header);
        const auto child  = Define("child.material", Instance(parent));
        Open();
        Build("parent.material");
        const auto effectiveParent = Named(parent);
        // Composition itself must reject invalid shapes/types before the editor tries to render.
        for (const auto& [override, diagnostic] :
             std::map<std::string, std::string> {{"parameters: {Rougness: 0.25}\n", "unknown"},
                                                 {"textures: {NormalTypo: null}\n", "unknown"},
                                                 {"parameters: {BaseColor: [1, 0]}\n", "needs 4 values"},
                                                 {"parameters: {Metallic: .nan}\n", "wrong type"}})
        {
            Write("child.material", Instance(parent, override));
            const auto result = Owner->BuildAsset(child.GetSourceID(), {});
            ASSERT_TRUE(std::holds_alternative<AssetError>(result));
            EXPECT_NE(std::get<AssetError>(result).Message.find(diagnostic), std::string::npos);
            EXPECT_FALSE(Owner->GetRuntimeAssetRegistry()->Find(child));
            const auto composed = ApplyMaterialOverrides(effectiveParent, Parse(Instance(parent, override)), nullptr);
            ASSERT_TRUE(std::holds_alternative<AssetError>(composed));
            EXPECT_NE(std::get<AssetError>(composed).Message.find(diagnostic), std::string::npos);
        }
    }
} // namespace
