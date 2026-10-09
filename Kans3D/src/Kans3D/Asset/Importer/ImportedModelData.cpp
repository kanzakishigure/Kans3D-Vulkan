#include "kspch.h"
#include "ImportedModelData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <assimp/cfileio.h>
#include <assimp/cimport.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Kans
{
    namespace
    {
        // Assimp 6 numbering for aiTextureType_GLTF_METALLIC_ROUGHNESS; absent before Assimp 6.
        constexpr uint32_t AssimpGltfMetallicRoughness = 27;

        // Maps an Assimp texture type to the engine semantic, or 0 to ignore it.
        uint32_t EngineTextureSemantic(uint32_t assimpType)
        {
            if (assimpType >= 1 && assimpType <= static_cast<uint32_t>(MaterialTextureSemantic::Transmission))
                return assimpType;
            if (assimpType == AssimpGltfMetallicRoughness)
                return static_cast<uint32_t>(MaterialTextureSemantic::MetallicRoughness);
            return 0;
        }

        struct ModelFile
        {
            aiFile        Handle {};
            std::ifstream Input;
            size_t        Size     = 0;
            size_t        Position = 0;
        };

        struct ModelIO
        {
            std::vector<std::filesystem::path>        Files;
            std::vector<std::filesystem::path>        MissingMaterials;
            const std::vector<std::filesystem::path>* AllowedFiles = nullptr;
            std::vector<std::filesystem::path>        DeniedFiles;
            bool                                      Failed = false;
        };

        ModelFile& File(aiFile* handle) { return *reinterpret_cast<ModelFile*>(handle->UserData); }

        size_t ReadModelFile(aiFile* handle, char* buffer, size_t size, size_t count)
        {
            auto& file = File(handle);
            if (!size || !count || count > size_t(std::numeric_limits<std::streamsize>::max()) / size)
                return 0;
            if (file.Position >= file.Size)
                return 0;
            const auto bytes = std::min(size * count, file.Size - file.Position);
            file.Input.read(buffer, static_cast<std::streamsize>(bytes));
            const auto read = static_cast<size_t>(file.Input.gcount());
            file.Position += read;
            return read / size;
        }

        aiReturn SeekModelFile(aiFile* handle, size_t offset, aiOrigin origin)
        {
            auto&  file = File(handle);
            size_t next = offset;
            if (origin == aiOrigin_CUR || origin == aiOrigin_END)
            {
                const size_t base = origin == aiOrigin_CUR ? file.Position : file.Size;
                const size_t sign = size_t(1) << (sizeof(size_t) * 8 - 1);
                if (offset & sign)
                {
                    const auto distance = std::numeric_limits<size_t>::max() - offset + 1;
                    if (distance > base)
                        return aiReturn_FAILURE;
                    next = base - distance;
                }
                else
                {
                    if (offset > file.Size - base)
                        return aiReturn_FAILURE;
                    next = base + offset;
                }
            }
            else if (origin != aiOrigin_SET)
                return aiReturn_FAILURE;
            if (next > file.Size || next > size_t(std::numeric_limits<std::streamoff>::max()))
                return aiReturn_FAILURE;
            file.Input.clear();
            file.Input.seekg(static_cast<std::streamoff>(next));
            if (!file.Input)
                return aiReturn_FAILURE;
            file.Position = next;
            return aiReturn_SUCCESS;
        }

        aiFile* OpenModelFile(aiFileIO* io, const char* path, const char* mode)
        {
            auto& context = *reinterpret_cast<ModelIO*>(io->UserData);
            try
            {
                if (!path || !mode || mode[0] != 'r' || std::strchr(mode, '+'))
                    return nullptr;
                const auto normalized = std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
                if (context.AllowedFiles &&
                    std::find(context.AllowedFiles->begin(), context.AllowedFiles->end(), normalized) ==
                        context.AllowedFiles->end())
                {
                    if (normalized.native().size() > 64 * 1024 || context.DeniedFiles.size() >= 1024)
                    {
                        context.Failed = true;
                        return nullptr;
                    }
                    if (std::find(context.DeniedFiles.begin(), context.DeniedFiles.end(), normalized) ==
                        context.DeniedFiles.end())
                        context.DeniedFiles.push_back(normalized);
                    return nullptr;
                }
                auto file = std::make_unique<ModelFile>();
                file->Input.open(normalized, std::ios::binary | std::ios::ate);
                if (!file->Input)
                {
                    auto extension = normalized.extension().u8string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (extension == ".mtl")
                        context.MissingMaterials.push_back(normalized);
                    return nullptr;
                }
                const auto length = file->Input.tellg();
                if (length < 0 || uint64_t(length) > std::numeric_limits<size_t>::max())
                    return nullptr;
                file->Size = static_cast<size_t>(length);
                file->Input.seekg(0);
                if (!file->Input)
                    return nullptr;
                file->Handle.ReadProc     = ReadModelFile;
                file->Handle.WriteProc    = [](aiFile*, const char*, size_t, size_t) -> size_t { return 0; };
                file->Handle.TellProc     = [](aiFile* f) { return File(f).Position; };
                file->Handle.FileSizeProc = [](aiFile* f) { return File(f).Size; };
                file->Handle.SeekProc     = SeekModelFile;
                file->Handle.FlushProc    = [](aiFile*) {};
                file->Handle.UserData     = reinterpret_cast<char*>(file.get());
                if (std::find(context.Files.begin(), context.Files.end(), normalized) == context.Files.end())
                    context.Files.push_back(normalized);
                auto* handle = &file->Handle;
                file.release();
                return handle;
            }
            catch (const std::exception&)
            {
                // Never unwind a C++ exception through a third-party C callback.
                context.Failed = true;
                return nullptr;
            }
        }
    } // namespace

    static std::variant<ImportedModelData, AssetError>
    ParseModelCpuImpl(const std::filesystem::path& path, const std::vector<std::filesystem::path>* allowedFiles)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
            return AssetError {
                (!error || error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory) ?
                    AssetErrorCode::NotFound :
                    AssetErrorCode::IoError,
                "Model source is not a readable regular file: " + path.u8string()};

        ModelIO context;
        context.AllowedFiles = allowedFiles;
        aiFileIO io {OpenModelFile, [](aiFileIO*, aiFile* f) { delete &File(f); }, reinterpret_cast<char*>(&context)};
        // C callbacks keep STL state on our side of the DLL boundary. Serialize
        // import/error retrieval because the C error string is process-global.
        static std::mutex importMutex;
        const aiScene*    rawScene;
        std::string       parseError;
        {
            std::lock_guard<std::mutex> lock(importMutex);
            rawScene =
                aiImportFileEx(path.u8string().c_str(),
                               aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
                                   aiProcess_CalcTangentSpace | aiProcess_ValidateDataStructure,
                               &io);
            if (!rawScene)
                parseError = aiGetErrorString();
        }
        std::unique_ptr<const aiScene, decltype(&aiReleaseImport)> owned(rawScene, aiReleaseImport);
        const auto*                                                scene = owned.get();
        if (context.Failed)
            return AssetError {AssetErrorCode::IoError, "Model source IO callback failed"};
        if (!context.DeniedFiles.empty())
            return AssetError {AssetErrorCode::NotFound,
                               "Model input is outside the declared input set: " +
                                   context.DeniedFiles.front().u8string()};
        if (!context.MissingMaterials.empty())
            return AssetError {AssetErrorCode::NotFound,
                               "Cannot read model material input: " + context.MissingMaterials.front().u8string()};
        if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mNumMeshes)
            return AssetError {AssetErrorCode::InvalidData, "Model parse failed: " + parseError};
        // Unsupported content is skipped so the static geometry still imports.
        ImportedModelData result;
        result.SourceFiles = context.Files;
        if (scene->HasAnimations())
            result.Warnings.push_back(std::to_string(scene->mNumAnimations) + " animation(s) were not imported");
        if (scene->HasTextures())
            result.Warnings.push_back(std::to_string(scene->mNumTextures) + " embedded texture(s) were not imported");
        for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
        {
            const auto*          material = scene->mMaterials[i];
            ImportedMaterialData data;
            data.Name = material->GetName().C_Str();
            aiColor4D color(1, 1, 1, 1);
            if (material->Get(AI_MATKEY_BASE_COLOR, color) != AI_SUCCESS)
                material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
            data.BaseColor = {color.r, color.g, color.b, color.a};
            int shading    = 0;
            material->Get(AI_MATKEY_SHADING_MODEL, shading);
            if (shading == aiShadingMode_PBR_BRDF)
            {
                material->Get(AI_MATKEY_METALLIC_FACTOR, data.Metallic);
                material->Get(AI_MATKEY_ROUGHNESS_FACTOR, data.Roughness);
            }
            else
            {
                // Phong-style material: approximate roughness from the specular exponent.
                float shininess = 0;
                if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0)
                    data.Roughness = std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.04f, 1.0f);
            }
            float opacity = 1.0f;
            material->Get(AI_MATKEY_OPACITY, opacity);
            aiString   alphaMode; // glTF: OPAQUE, MASK or BLEND
            const bool gltfAlpha = material->Get("$mat.gltf.alphaMode", 0, 0, alphaMode) == AI_SUCCESS;
            data.Opaque          = opacity >= 1.0f && (!gltfAlpha || std::string(alphaMode.C_Str()) == "OPAQUE");
            if (!std::all_of(data.BaseColor.begin(), data.BaseColor.end(), [](float v) { return std::isfinite(v); }) ||
                !std::isfinite(data.Metallic) || !std::isfinite(data.Roughness))
                return AssetError {AssetErrorCode::InvalidData, "Model material contains non-finite data"};
            for (uint32_t type = 1; type <= static_cast<uint32_t>(AI_TEXTURE_TYPE_MAX); ++type)
            {
                const uint32_t engineSemantic = EngineTextureSemantic(type);
                if (!engineSemantic)
                    continue;
                const auto semantic = static_cast<aiTextureType>(type);
                for (uint32_t slot = 0; slot < material->GetTextureCount(semantic); ++slot)
                {
                    aiString     source;
                    unsigned int uv = 0;
                    if (material->GetTexture(semantic, slot, &source, nullptr, &uv) != AI_SUCCESS)
                        return AssetError {AssetErrorCode::InvalidData, "Cannot read model texture reference"};
                    data.Textures.push_back({engineSemantic, uv, source.C_Str()});
                }
            }
            result.Materials.push_back(std::move(data));
        }

        for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
        {
            const auto* mesh = scene->mMeshes[i];
            const std::string name = mesh->mName.C_Str();
            if (mesh->HasBones())
                result.Warnings.push_back("mesh '" + name + "' imported in its bind pose without skinning");
            if (mesh->GetNumUVChannels() > 1 || mesh->GetNumColorChannels() > 1)
                result.Warnings.push_back("mesh '" + name + "' keeps only its first UV and color channel");
            if (mesh->mNumAnimMeshes > 0)
                result.Warnings.push_back("mesh '" + name + "' morph targets were not imported");
            if (!mesh->HasPositions() || !mesh->HasNormals() || mesh->mMaterialIndex >= result.Materials.size())
                return AssetError {AssetErrorCode::InvalidData, "Model mesh has invalid geometry or material"};
            ImportedMeshData data;
            data.Name          = mesh->mName.C_Str();
            data.MaterialIndex = mesh->mMaterialIndex;
            data.Vertices.reserve(mesh->mNumVertices);
            for (uint32_t vertex = 0; vertex < mesh->mNumVertices; ++vertex)
            {
                ImportedVertex value;
                const auto&    p = mesh->mVertices[vertex];
                const auto&    n = mesh->mNormals[vertex];
                value.Position   = {p.x, p.y, p.z};
                value.Normal     = {n.x, n.y, n.z};
                if (mesh->HasTextureCoords(0))
                    value.UV = {mesh->mTextureCoords[0][vertex].x, mesh->mTextureCoords[0][vertex].y};
                if (mesh->HasVertexColors(0))
                {
                    const auto& c = mesh->mColors[0][vertex];
                    value.Color   = {c.r, c.g, c.b, c.a};
                }
                if (mesh->HasTangentsAndBitangents())
                {
                    const auto& t = mesh->mTangents[vertex];
                    const auto& b = mesh->mBitangents[vertex];
                    value.Tangent = {t.x, t.y, t.z, ((n ^ t) * b) < 0 ? -1.0f : 1.0f};
                }
                const auto finite = [](const auto& values) {
                    return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
                };
                if (!finite(value.Position) || !finite(value.Normal) || !finite(value.UV) || !finite(value.Color) ||
                    !finite(value.Tangent))
                    return AssetError {AssetErrorCode::InvalidData, "Model vertex contains non-finite data"};
                data.Vertices.push_back(value);
            }
            for (uint32_t face = 0; face < mesh->mNumFaces; ++face)
            {
                const auto& triangle = mesh->mFaces[face];
                if (triangle.mNumIndices != 3)
                    return AssetError {AssetErrorCode::InvalidData, "Model contains non-triangle geometry"};
                for (uint32_t corner = 0; corner < 3; ++corner)
                {
                    if (triangle.mIndices[corner] >= data.Vertices.size())
                        return AssetError {AssetErrorCode::InvalidData, "Model triangle index is out of range"};
                    data.Indices.push_back(triangle.mIndices[corner]);
                }
            }
            result.Meshes.push_back(std::move(data));
        }

        // Iterative traversal avoids consuming the call stack for deep hierarchies.
        std::vector<std::pair<const aiNode*, int64_t>> pending {{scene->mRootNode, -1}};
        while (!pending.empty())
        {
            const auto entry = pending.back();
            pending.pop_back();
            ImportedNodeData node;
            node.Name           = entry.first->mName.C_Str();
            node.Parent         = entry.second;
            const auto& m       = entry.first->mTransformation;
            node.LocalTransform = {
                m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4};
            for (float value : node.LocalTransform)
                if (!std::isfinite(value))
                    return AssetError {AssetErrorCode::InvalidData, "Model node transform is non-finite"};
            for (uint32_t i = 0; i < entry.first->mNumMeshes; ++i)
            {
                const auto mesh = entry.first->mMeshes[i];
                if (mesh >= result.Meshes.size())
                    return AssetError {AssetErrorCode::InvalidData, "Model node mesh reference is out of range"};
                node.Meshes.push_back(mesh);
            }
            const auto index = static_cast<int64_t>(result.Nodes.size());
            result.Nodes.push_back(std::move(node));
            for (uint32_t i = entry.first->mNumChildren; i > 0; --i)
                pending.emplace_back(entry.first->mChildren[i - 1], index);
        }
        // The engine works in meters. FBX records its unit as centimeters per unit.
        double unit = 0;
        if (scene->mMetaData)
        {
            float single = 0;
            if (!scene->mMetaData->Get("UnitScaleFactor", unit) && scene->mMetaData->Get("UnitScaleFactor", single))
                unit = single;
        }
        if (std::isfinite(unit) && unit > 0 && unit != 100 && !result.Nodes.empty())
            ScaleModel(result, static_cast<float>(unit / 100.0));
        return result;
    }

    void ScaleModel(ImportedModelData& model, float scale)
    {
        // Scaling the root node scales the whole hierarchy (row-major: scale rows 0-2).
        for (auto& node : model.Nodes)
            if (node.Parent < 0)
                for (size_t i = 0; i < 12; ++i)
                    node.LocalTransform[i] *= scale;
    }

    std::variant<ImportedModelData, AssetError> ParseModelCpu(const std::filesystem::path& path)
    {
        return ParseModelCpuImpl(path, nullptr);
    }

    std::variant<ImportedModelData, AssetError> ParseModelCpu(const std::filesystem::path&              path,
                                                              const std::vector<std::filesystem::path>& allowedFiles)
    {
        if (allowedFiles.empty() || allowedFiles.size() > 1024)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid model input set size"};
        std::vector<std::filesystem::path> normalized;
        normalized.reserve(allowedFiles.size());
        for (const auto& file : allowedFiles)
        {
            if (file.empty())
                return AssetError {AssetErrorCode::InvalidArgument, "Empty model input path"};
            normalized.push_back(std::filesystem::absolute(file).lexically_normal());
        }
        return ParseModelCpuImpl(path, &normalized);
    }
    std::variant<std::filesystem::path, AssetError> ResolveModelTexture(const std::filesystem::path& modelFile,
                                                                        const std::string&           reference)
    {
        namespace fs           = std::filesystem;
        std::string normalized = reference;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        if (normalized.empty() || normalized.size() > 4096 || normalized.find('\0') != std::string::npos)
            return AssetError {AssetErrorCode::InvalidData, "Invalid model texture reference"};
        const auto      referenced = fs::u8path(normalized);
        const auto      directory  = modelFile.parent_path();
        std::error_code error;
        // Authoring tools often store absolute paths from another machine, so an
        // absolute reference falls back to its file name next to the model.
        std::vector<fs::path> candidates;
        if (!referenced.is_absolute() && !referenced.has_root_name() && !referenced.has_root_directory())
            candidates.push_back((directory / referenced).lexically_normal());
        candidates.push_back((directory / referenced.filename()).lexically_normal());
        for (const auto& candidate : candidates)
            if (fs::is_regular_file(fs::symlink_status(candidate, error)))
                return candidate;

        // Case-insensitive file name search below the model directory (bounded).
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            });
            return text;
        };
        const auto            wanted = lower(referenced.filename().u8string());
        std::vector<fs::path> matches;
        size_t                visited = 0;
        for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end && visited < 20000;
             it.increment(error), ++visited)
        {
            if (it.depth() > 4)
                it.disable_recursion_pending();
            if (it->is_regular_file(error) && !it->is_symlink(error) &&
                lower(it->path().filename().u8string()) == wanted)
                matches.push_back(it->path().lexically_normal());
        }
        if (matches.empty())
            return AssetError {AssetErrorCode::NotFound, "Model texture not found: " + reference};
        // Deterministic choice: the shallowest, then lexicographically first match.
        std::sort(matches.begin(), matches.end(), [](const fs::path& a, const fs::path& b) {
            const auto da = std::distance(a.begin(), a.end()), db = std::distance(b.begin(), b.end());
            return da != db ? da < db : a < b;
        });
        return matches.front();
    }
} // namespace Kans
