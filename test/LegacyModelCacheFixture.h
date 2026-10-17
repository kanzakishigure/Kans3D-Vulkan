#pragma once
#include <fstream>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Importer/ImportFingerprint.h"
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/SourceAssetMetaSerializer.h"
#include "Kans3D/Asset/Storage/ProductStore.h"

namespace Kans::LegacyModelCacheFixture
{
    // Captured by running ModelImporter + KMSH/KMOD codecs from 00d8f82 on
    // these source files. Literal product bytes, not today's encoder output.
    inline SourceAssetID Source() { return SourceAssetID::FromString("1111111111111111"); }
    inline SourceAssetID MtlSource() { return SourceAssetID::FromString("2222222222222222"); }
    inline AssetID       ID(uint64_t local) { return {Source(), local}; }
    inline LocalIDMap    LocalIDs()
    {
        return {{"model:root", 1},
                {"material:3:Red", 2},
                {"material:4:Blue", 3},
                {"mesh:4:Body:material:3:Red", 4},
                {"mesh:4:Blue:material:4:Blue", 5},
                {"material:15:DefaultMaterial", 6}};
    }
    template<class T>
    T Value(std::variant<T, AssetError> result)
    {
        if (const auto* error = std::get_if<AssetError>(&result))
            throw std::runtime_error(error->Message);
        return std::get<T>(std::move(result));
    }
    inline void Write(const std::filesystem::path& path, const std::string& bytes)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out)
            throw std::runtime_error("Cannot write legacy test source");
    }
    inline std::vector<std::byte> Bytes(const std::string& bytes)
    {
        std::vector<std::byte> result;
        for (unsigned char value : bytes)
            result.push_back(static_cast<std::byte>(value));
        return result;
    }
    inline std::vector<std::byte> Unhex(const char* text)
    {
        std::vector<std::byte> bytes;
        const auto             digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (size_t i = 0; text[i]; i += 2)
            bytes.push_back(static_cast<std::byte>((digit(text[i]) << 4) | digit(text[i + 1])));
        return bytes;
    }
    inline const std::string      Obj = R"(mtllib legacy.mtl
o Body
v -1 -1 0
v 0 -1 0
v 0 1 0
v -1 1 0
v 1 -1 0
v 1 1 0
usemtl Red
f 1 2 3
f 1 3 4
usemtl Blue
f 2 5 6
f 2 6 3
)";
    inline const std::string      Mtl = R"(newmtl Red
Kd 1 0 0
newmtl Blue
Kd 0 0 1
)";
    inline std::vector<std::byte> Product1()
    {
        return Unhex("6d616769633a204b4d4f440a76657273696f6e3a20310a6e6f6465733a0a20202d206e616d653a206c65676163792e6f"
                     "626a0a20202020706172656e743a202d310a202020207472616e73666f726d3a205b312c20302c20302c20302c20302c"
                     "20312c20302c20302c20302c20302c20312c20302c20302c20302c20302c20315d0a202020206d65736865733a0a2020"
                     "202020205b5d0a20202d206e616d653a20426f64790a20202020706172656e743a20300a202020207472616e73666f72"
                     "6d3a205b312c20302c20302c20302c20302c20312c20302c20302c20302c20302c20312c20302c20302c20302c20302c"
                     "20315d0a202020206d65736865733a0a2020202020202d20313131313131313131313131313131313a30303030303030"
                     "3030303030303030340a2020202020202d20313131313131313131313131313131313a30303030303030303030303030"
                     "303035");
    }
    inline std::vector<std::byte> Product2()
    {
        return Unhex("6d616769633a204b4d41540a76657273696f6e3a20340a6e616d653a205265640a626173655f636f6c6f723a205b312c"
                     "20302c20302c20315d0a6d6574616c6c69633a20300a726f7567686e6573733a20310a74657874757265733a0a20205b"
                     "5d0a7368616465723a20666666666666666666666666666666663a303030303030303030303030303030310a62696e64"
                     "696e67733a20223462346434323532303130303030303030313030303030303030303030303030303130303030303030"
                     "303030303030303030303030303030303030303030303030613030303030303031303030303030303030303030303030"
                     "303030303030303030303030303030303430303030303030303030383033663030303030303030303030303030303030"
                     "303030383033663031303030303030303130303030303030303030303030303030303030303030303130303030303030"
                     "303030303030303031303030303030303230303030303030303030303030303030303030303030303130303030303030"
                     "303030383033663031303030303030303330303030303030303030303030303030303030303030303330303030303030"
                     "303030303030303030303030303030303030303030303030313030303030303034303030303030303030303030303030"
                     "303030303030303031303030303030303030303830336630313030303030303035303030303030303030303030303030"
                     "303030303030303031303030303030303030303830336630313030303030303036303030303030303030303030303030"
                     "303030303030303031303030303030303030303830336630313030303030303037303030303030303030303030303030"
                     "303030303030303032303030303030303030303830336630303030383033663031303030303030303830303030303030"
                     "303030303030303030303030303030303230303030303030303030303030303030303030303030303130303030303030"
                     "393030303030303030303030303030303030303030303030313030303030303030303030303030220a72656e6465723a"
                     "207b616c7068615f6d6f64653a206f70617175652c20616c7068615f6375746f66663a20302e352c20646f75626c655f"
                     "73696465643a20747275657d");
    }
    inline std::vector<std::byte> Product3()
    {
        return Unhex("6d616769633a204b4d41540a76657273696f6e3a20340a6e616d653a20426c75650a626173655f636f6c6f723a205b30"
                     "2c20302c20312c20315d0a6d6574616c6c69633a20300a726f7567686e6573733a20310a74657874757265733a0a2020"
                     "5b5d0a7368616465723a20666666666666666666666666666666663a303030303030303030303030303030310a62696e"
                     "64696e67733a202234623464343235323031303030303030303130303030303030303030303030303031303030303030"
                     "303030303030303030303030303030303030303030303030306130303030303030313030303030303030303030303030"
                     "303030303030303030303030303030303034303030303030303030303030303030303030303030303030303038303366"
                     "303030303830336630313030303030303031303030303030303030303030303030303030303030303031303030303030"
                     "303030303030303030313030303030303032303030303030303030303030303030303030303030303031303030303030"
                     "303030303830336630313030303030303033303030303030303030303030303030303030303030303033303030303030"
                     "303030303030303030303030303030303030303030303030303130303030303030343030303030303030303030303030"
                     "303030303030303030313030303030303030303038303366303130303030303030353030303030303030303030303030"
                     "303030303030303030313030303030303030303038303366303130303030303030363030303030303030303030303030"
                     "303030303030303030313030303030303030303038303366303130303030303030373030303030303030303030303030"
                     "303030303030303030323030303030303030303038303366303030303830336630313030303030303038303030303030"
                     "303030303030303030303030303030303032303030303030303030303030303030303030303030303031303030303030"
                     "30393030303030303030303030303030303030303030303030313030303030303030303030303030220a72656e646572"
                     "3a207b616c7068615f6d6f64653a206f70617175652c20616c7068615f6375746f66663a20302e352c20646f75626c65"
                     "5f73696465643a20747275657d");
    }
    inline std::vector<std::byte> Product4()
    {
        return Unhex(
            "4b4d53480200000004000000000000000400000006000000111111111111111102000000000000001111111111111111"
            "0200000000000000000080bf000080bf0000000000000000000000000000803f00000000000000000000803f0000803f"
            "0000803f0000803f0000803f00000000000000000000803f00000000000080bf0000000000000000000000000000803f"
            "00000000000000000000803f0000803f0000803f0000803f0000803f00000000000000000000803f000000000000803f"
            "0000000000000000000000000000803f00000000000000000000803f0000803f0000803f0000803f0000803f00000000"
            "000000000000803f000080bf0000803f0000000000000000000000000000803f00000000000000000000803f0000803f"
            "0000803f0000803f0000803f00000000000000000000803f000000000100000002000000000000000200000003000000");
    }
    inline std::vector<std::byte> Product5()
    {
        return Unhex(
            "4b4d53480200000004000000000000000400000006000000111111111111111103000000000000001111111111111111"
            "030000000000000000000000000080bf0000000000000000000000000000803f00000000000000000000803f0000803f"
            "0000803f0000803f0000803f00000000000000000000803f0000803f000080bf0000000000000000000000000000803f"
            "00000000000000000000803f0000803f0000803f0000803f0000803f00000000000000000000803f0000803f0000803f"
            "0000000000000000000000000000803f00000000000000000000803f0000803f0000803f0000803f0000803f00000000"
            "000000000000803f000000000000803f0000000000000000000000000000803f00000000000000000000803f0000803f"
            "0000803f0000803f0000803f00000000000000000000803f000000000100000002000000000000000200000003000000");
    }
    inline std::vector<std::byte> Product6()
    {
        return Unhex("6d616769633a204b4d41540a76657273696f6e3a20340a6e616d653a2044656661756c744d6174657269616c0a626173"
                     "655f636f6c6f723a205b302e3630303030303032342c20302e3630303030303032342c20302e3630303030303032342c"
                     "20315d0a6d6574616c6c69633a20300a726f7567686e6573733a20310a74657874757265733a0a20205b5d0a73686164"
                     "65723a20666666666666666666666666666666663a303030303030303030303030303030310a62696e64696e67733a20"
                     "223462346434323532303130303030303030313030303030303030303030303030303130303030303030303030303030"
                     "303030303030303030303030303030303030613030303030303031303030303030303030303030303030303030303030"
                     "303030303030303030303430303030303039613939313933663961393931393366396139393139336630303030383033"
                     "663031303030303030303130303030303030303030303030303030303030303030303130303030303030303030303030"
                     "303031303030303030303230303030303030303030303030303030303030303030303130303030303030303030383033"
                     "663031303030303030303330303030303030303030303030303030303030303030303330303030303030303030303030"
                     "303030303030303030303030303030303030313030303030303034303030303030303030303030303030303030303030"
                     "303031303030303030303030303830336630313030303030303035303030303030303030303030303030303030303030"
                     "303031303030303030303030303830336630313030303030303036303030303030303030303030303030303030303030"
                     "303031303030303030303030303830336630313030303030303037303030303030303030303030303030303030303030"
                     "303032303030303030303030303830336630303030383033663031303030303030303830303030303030303030303030"
                     "303030303030303030303230303030303030303030303030303030303030303030303130303030303030393030303030"
                     "303030303030303030303030303030303030313030303030303030303030303030220a72656e6465723a207b616c7068"
                     "615f6d6f64653a206f70617175652c20616c7068615f6375746f66663a20302e352c20646f75626c655f73696465643a"
                     "20747275657d");
    }
    inline void WriteSources(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root / "assets");
        Write(root / "assets/legacy.obj", Obj);
        Write(root / "assets/legacy.mtl", Mtl);
        SourceAssetMetadata metadata;
        metadata.sourceAssetID = Source();
        metadata.Path          = AssetPath("legacy.obj");
        metadata.LocalIDs      = LocalIDs();
        Value(SourceAssetMetaSerializer::Save(root / "assets/legacy.obj.kmeta", metadata));
        metadata.sourceAssetID = MtlSource();
        metadata.Path          = AssetPath("legacy.mtl");
        metadata.LocalIDs.clear();
        Value(SourceAssetMetaSerializer::Save(root / "assets/legacy.mtl.kmeta", metadata));
    }
    inline Ref<const AssetRegistry> Seed(const std::filesystem::path& root, const std::string& target)
    {
        const auto        storeRoot  = root / "Cache/Products";
        const std::string generation = "generations/pre-section-fixture";
        std::filesystem::create_directories(storeRoot / generation);
        struct Product
        {
            uint64_t               Local;
            AssetType              Type;
            std::vector<std::byte> Data;
            std::vector<AssetID>   Dependencies;
        };
        const std::vector<Product> products {{1, AssetType::Model, Product1(), {ID(4), ID(5)}},
                                             {2, AssetType::Material, Product2(), {}},
                                             {3, AssetType::Material, Product3(), {}},
                                             {4, AssetType::StaticMesh, Product4(), {ID(2)}},
                                             {5, AssetType::StaticMesh, Product5(), {ID(3)}},
                                             {6, AssetType::Material, Product6(), {}}};
        ImportFingerprintInput     key;
        key.Source          = {Source(), ComputeContentDigest(Bytes(Obj))};
        key.Importer        = "ModelImporter";
        key.ImporterVersion = 12;
        key.Toolchain       = "Kans3D";
        key.Target          = target;
        key.LocalIDs        = LocalIDs();
        key.Sources         = {{MtlSource(), ComputeContentDigest(Bytes(Mtl))}};
        // Reconstruct an existing on-disk catalog, rather than asking today's
        // publisher to accept formats its runtime loaders intentionally reject.
        YAML::Node catalog;
        catalog["format"]  = "KansProductCatalog";
        catalog["version"] = 4;
        auto source        = catalog["sources"][Source().ToString()];
        for (const auto& [name, id] : LocalIDs())
            source["local_ids"][name] = id;
        auto build           = source["targets"][target];
        build["generation"]  = generation;
        build["fingerprint"] = Value(ComputeImportFingerprint(key));
        build["source_inputs"].push_back(MtlSource().ToString());
        build["product_inputs"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& product : products)
        {
            const auto file = std::to_string(product.Local) + ".product";
            Value(WriteProductFile(storeRoot / generation / file, product.Data));
            YAML::Node record;
            record["local"]        = product.Local;
            record["type"]         = static_cast<uint32_t>(product.Type);
            record["file"]         = file;
            record["size"]         = product.Data.size();
            record["digest"]       = ComputeContentDigest(product.Data);
            record["dependencies"] = YAML::Node(YAML::NodeType::Sequence);
            for (auto dependency : product.Dependencies)
                record["dependencies"].push_back(dependency.ToString());
            build["products"].push_back(record);
        }
        YAML::Emitter out;
        out << catalog;
        Write(storeRoot / "catalog.yaml", out.c_str());
        return Value(ProductStore::Open(storeRoot))->Snapshot();
    }
} // namespace Kans::LegacyModelCacheFixture
