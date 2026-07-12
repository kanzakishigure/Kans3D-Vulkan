#include "AssimpMeshImporter.h"
#include "kspch.h"


#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Utilities/MeshUtils.h"


#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

namespace {
constexpr uint32_t kImportFlag = aiProcess_CalcTangentSpace // 计算切线空间
                                 | aiProcess_Triangulate    // 三角化
                                 |
                                 aiProcess_JoinIdenticalVertices // 合并重复顶点
                                 | aiProcess_SortByPType       // 按图元类型排序
                                 | aiProcess_GenSmoothNormals; // 生成平滑法线
}

namespace Kans {

AssimpMeshImporter::AssimpMeshImporter(const std::filesystem::path &path)
    : m_Path(path) {}

// ================================================================
// ImportToMeshSource — 主入口
// ================================================================
Ref<MeshSource> AssimpMeshImporter::ImportToMeshSource() {
  CORE_INFO_TAG("Mesh", "Loading mesh: {0}", m_Path.string());

  // ---- 基础检查 ----
  if (!std::filesystem::exists(m_Path)) {
    CORE_ERROR_TAG("Mesh", "File not found: {0}", m_Path.string());
    return nullptr;
  }

  if (std::filesystem::file_size(m_Path) < 64) {
    CORE_ERROR_TAG("Mesh", "File too small: {0}", m_Path.string());
    return nullptr;
  }

  // ---- 预校验 ----
  Assimp::Importer quickImporter;
  const aiScene *testScene =
      quickImporter.ReadFile(m_Path.string(), kImportFlag);
  if (!testScene || testScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !testScene->mRootNode) {
    CORE_ERROR_TAG("Mesh", "Failed to load: {0}", m_Path.string());
    CORE_ERROR_TAG("Mesh", quickImporter.GetErrorString());
    return nullptr;
  }
  if (testScene->mNumMeshes == 0) {
    CORE_ERROR_TAG("Mesh", "No meshes in file: {0}", m_Path.string());
    return nullptr;
  }

  CORE_INFO_TAG("Mesh", "Validated — {0} meshes, {1} materials",
                testScene->mNumMeshes, testScene->mNumMaterials);

  // ---- 完整导入 ----
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(m_Path.string(), kImportFlag);

  Ref<MeshSource> ms = CreateRef<MeshSource>();

  // 路径
  ms->m_LoadPath = m_Path.string();
  size_t slash = ms->m_LoadPath.find_last_of("/\\");
  if (slash != std::string::npos)
    ms->m_LoadPath = ms->m_LoadPath.substr(0, slash);

  // Shader
  ms->m_MeshShader = Renderer::GetShaderLibrary()->Get("StaticMeshShader");

  // 遍历节点树
  ms->m_SubMeshes.reserve(scene->mNumMeshes);
  ProcessNode(scene->mRootNode, scene, ms);

  // 构建材质表
  ms->m_MaterialTable = CreateRef<MaterialTable>(ms->m_Materials.size());
  for (size_t i = 0; i < ms->m_Materials.size(); ++i)
    ms->m_MaterialTable->SetMaterial(i, ms->m_Materials[i]);
  ms->m_Materials.clear();

  // GPU 缓冲
  GenVertexArrays(ms);

  CORE_INFO_TAG(
      "Mesh", "Imported: {0} ({1} submeshes, {2} verts, {3} indices, {4} VA)",
      m_Path.filename().string(), ms->m_SubMeshes.size(),
      ms->m_Verteices.size(), ms->m_Indices.size(), ms->m_VertexArray.size());

  ms->m_GpuReady = true;
  return ms;
}

// ================================================================
// ImportToMeshSourceCpu — CPU-only 导入（无 GL 调用）
// ================================================================
Ref<MeshSource> AssimpMeshImporter::ImportToMeshSourceCpu() {
  CORE_INFO_TAG("Mesh", "Loading mesh (CPU-only): {0}", m_Path.string());

  // ---- 基础检查 ----
  if (!std::filesystem::exists(m_Path)) {
    CORE_ERROR_TAG("Mesh", "File not found: {0}", m_Path.string());
    return nullptr;
  }

  if (std::filesystem::file_size(m_Path) < 64) {
    CORE_ERROR_TAG("Mesh", "File too small: {0}", m_Path.string());
    return nullptr;
  }

  // ---- 预校验 ----
  Assimp::Importer quickImporter;
  const aiScene *testScene =
      quickImporter.ReadFile(m_Path.string(), kImportFlag);
  if (!testScene || testScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !testScene->mRootNode) {
    CORE_ERROR_TAG("Mesh", "Failed to load: {0}", m_Path.string());
    CORE_ERROR_TAG("Mesh", quickImporter.GetErrorString());
    return nullptr;
  }
  if (testScene->mNumMeshes == 0) {
    CORE_ERROR_TAG("Mesh", "No meshes in file: {0}", m_Path.string());
    return nullptr;
  }

  // ---- 完整导入 ----
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(m_Path.string(), kImportFlag);

  Ref<MeshSource> ms = CreateRef<MeshSource>();

  // 路径
  ms->m_LoadPath = m_Path.string();
  size_t slash = ms->m_LoadPath.find_last_of("/\\");
  if (slash != std::string::npos)
    ms->m_LoadPath = ms->m_LoadPath.substr(0, slash);

  // Shader
  ms->m_MeshShader = Renderer::GetShaderLibrary()->Get("StaticMeshShader");

  // 设置为 CPU-only 模式
  m_CpuOnly = true;

  // 遍历节点树
  ms->m_SubMeshes.reserve(scene->mNumMeshes);
  ProcessNode(scene->mRootNode, scene, ms);

  m_CpuOnly = false;

  // 构建材质表
  ms->m_MaterialTable = CreateRef<MaterialTable>(ms->m_Materials.size());
  for (size_t i = 0; i < ms->m_Materials.size(); ++i)
    ms->m_MaterialTable->SetMaterial(i, ms->m_Materials[i]);
  ms->m_Materials.clear();

  // 注意：不调 GenVertexArrays！GPU 资源延迟到主线程创建
  // 注意：纹理在 ImportMaterialCpu 中存储为 PendingGpuTexture

  CORE_INFO_TAG(
      "Mesh", "Imported (CPU-only): {0} ({1} submeshes, {2} verts, {3} indices, {4} pending textures)",
      m_Path.filename().string(), ms->m_SubMeshes.size(),
      ms->m_Verteices.size(), ms->m_Indices.size(), ms->m_PendingTextures.size());

  return ms;
}

// ================================================================
// ProcessNode — 递归处理节点树
// ================================================================
void AssimpMeshImporter::ProcessNode(const aiNode *node, const aiScene *scene,
                                     Ref<MeshSource> &ms) {
  for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
    aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
    ms->m_SubMeshes.push_back(ProcessMesh(mesh, scene, ms));
  }

  for (unsigned int i = 0; i < node->mNumChildren; ++i)
    ProcessNode(node->mChildren[i], scene, ms);
}

// ================================================================
// ProcessMesh — 处理单个网格
// ================================================================
SubMesh AssimpMeshImporter::ProcessMesh(const aiMesh *mesh,
                                        const aiScene *scene,
                                        Ref<MeshSource> &ms) {
  SubMesh submesh;
  submesh.BaseVertex = static_cast<uint32_t>(ms->m_Verteices.size());
  submesh.BaseIndex = static_cast<uint32_t>(ms->m_Indices.size());
  submesh.VertexCount = 0;
  submesh.IndexCount = 0;
  submesh.MaterialIndex = mesh->mMaterialIndex;

  // ── 顶点 ──
  for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
    Vertex v;

    v.Position = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                  mesh->mVertices[i].z};
    v.Normal = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};

    // Tangent + bitangent → w 分量
    if (mesh->HasTangentsAndBitangents()) {
      glm::vec3 t = {mesh->mTangents[i].x, mesh->mTangents[i].y,
                     mesh->mTangents[i].z};
      glm::vec3 b = {mesh->mBitangents[i].x, mesh->mBitangents[i].y,
                     mesh->mBitangents[i].z};
      float w = glm::dot(glm::cross(v.Normal, t), b) > 0.0f ? 1.0f : -1.0f;
      v.Tangent = {t.x, t.y, t.z, w};
    } else {
      v.Tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    }

    // UV
    if (mesh->mTextureCoords[0])
      v.Texturecroods = {mesh->mTextureCoords[0][i].x,
                         mesh->mTextureCoords[0][i].y};
    else
      v.Texturecroods = {0.0f, 0.0f};

    // 顶点色
    if (mesh->HasVertexColors(0))
      v.BaseColor = {mesh->mColors[0][i].r, mesh->mColors[0][i].g,
                     mesh->mColors[0][i].b, mesh->mColors[0][i].a};
    else
      v.BaseColor = {0.0f, 0.0f, 0.0f, 0.0f};

    ms->m_Verteices.push_back(v);
    ++submesh.VertexCount;
  }

  // ── 索引 ──
  for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
    const aiFace &face = mesh->mFaces[i];
    for (unsigned int j = 0; j < face.mNumIndices; j += 3) {
      ms->m_Indices.push_back(
          {face.mIndices[j], face.mIndices[j + 1], face.mIndices[j + 2]});
      ++submesh.IndexCount;
    }
  }

  // ── 材质 ──
  if (scene->HasMaterials()) {
    aiMaterial *aiMat = scene->mMaterials[mesh->mMaterialIndex];
    if (m_CpuOnly)
      ImportMaterialCpu(aiMat, scene, ms);
    else
      ImportMaterial(aiMat, scene, ms);
  }

  return submesh;
}

// ================================================================
// ImportMaterial — 材质导入（Blinn-Phong + PBR）
// ================================================================
void AssimpMeshImporter::ImportMaterial(const aiMaterial *aiMat,
                                        const aiScene * /*scene*/,
                                        Ref<MeshSource> &ms) {
  std::string mtlName = aiMat->GetName().C_Str();
  Ref<Material> mtl = Material::Create(ms->m_MeshShader, mtlName);
  Ref<MaterialAsset> mtlAsset = CreateRef<MaterialAsset>(mtl);
  TextureSpecification spec;

  // ══════════════════════════════════════════════════════════
  // Blinn-Phong 贴图
  // ══════════════════════════════════════════════════════════

  // Normal Map
  if (aiMat->GetTextureCount(aiTextureType_NORMALS) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_NORMALS, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    CORE_TRACE("  [{}] Normal: {}", mtlName, texPath);
    mtlAsset->SetNormalMap(KansFileSystem::Exists(texPath)
                               ? Texture2D::Create(spec, texPath)
                               : Renderer::GetWhiteTexture());
  } else {
    mtlAsset->SetNormalMap(Renderer::GetWhiteTexture());
  }

  // Diffuse
  if (aiMat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    CORE_TRACE("  [{}] Diffuse: {}", mtlName, texPath);
    Ref<Texture2D> tex = Texture2D::Create(spec, texPath);
    mtlAsset->SetDiffuseMap(tex);

    // 自动检测 _Light 贴图
    {
      std::string lightPath = texPath;
      size_t dot = lightPath.find_last_of('.');
      if (dot != std::string::npos)
        lightPath.insert(dot, "_Light");
      if (KansFileSystem::Exists(lightPath)) {
        mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(),
                        Texture2D::Create(spec, lightPath));
        CORE_TRACE("  [{}] Light map found", mtlName);
      } else {
        mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(),
                        Renderer::GetWhiteTexture());
      }
    }
    // 自动检测 _Ramp 贴图
    {
      std::string rampPath = texPath;
      size_t dot = rampPath.find_last_of('.');
      if (dot != std::string::npos)
        rampPath.insert(dot, "_Ramp");
      if (KansFileSystem::Exists(rampPath)) {
        mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(),
                        Texture2D::Create(spec, rampPath));
        CORE_TRACE("  [{}] Ramp map found", mtlName);
      } else {
        mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(),
                        Renderer::GetWhiteTexture());
      }
    }
  } else {
    mtlAsset->SetDiffuseMap(Renderer::GetWhiteTexture());
  }

  // Specular
  if (aiMat->GetTextureCount(aiTextureType_SPECULAR) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_SPECULAR, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    CORE_TRACE("  [{}] Specular: {}", mtlName, texPath);
    mtlAsset->SetSpecularMap(Texture2D::Create(spec, texPath));
  } else {
    mtlAsset->SetSpecularMap(Renderer::GetBlackTexture());
  }

  // ══════════════════════════════════════════════════════════
  // PBR 贴图
  // ══════════════════════════════════════════════════════════

  // Albedo
  if (aiMat->GetTextureCount(aiTextureType_BASE_COLOR) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_BASE_COLOR, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    if (KansFileSystem::Exists(texPath)) {
      mtlAsset->SetAlbedoMap(Texture2D::Create(spec, texPath));
      CORE_TRACE("  [{}] Albedo: {}", mtlName, texPath);
    } else {
      mtlAsset->SetAlbedoMap(Renderer::GetWhiteTexture());
    }
  } else {
    mtlAsset->SetAlbedoMap(Renderer::GetWhiteTexture());
  }

  // AO
  if (aiMat->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    if (KansFileSystem::Exists(texPath)) {
      mtlAsset->SetAOMap(Texture2D::Create(spec, texPath));
      CORE_TRACE("  [{}] AO: {}", mtlName, texPath);
    } else {
      mtlAsset->SetAOMap(Renderer::GetWhiteTexture());
    }
  } else {
    mtlAsset->SetAOMap(Renderer::GetWhiteTexture());
  }

  // Roughness
  if (aiMat->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    if (KansFileSystem::Exists(texPath)) {
      mtlAsset->SetRoughMap(Texture2D::Create(spec, texPath));
      CORE_TRACE("  [{}] Roughness: {}", mtlName, texPath);
    } else {
      mtlAsset->SetRoughMap(Renderer::GetWhiteTexture());
    }
  } else {
    mtlAsset->SetRoughMap(Renderer::GetWhiteTexture());
  }

  // Metalness
  if (aiMat->GetTextureCount(aiTextureType_METALNESS) > 0) {
    aiString path;
    aiMat->GetTexture(aiTextureType_METALNESS, 0, &path);
    std::string texPath = ms->m_LoadPath + "/" + path.C_Str();
    if (KansFileSystem::Exists(texPath)) {
      mtlAsset->SetMetalMap(Texture2D::Create(spec, texPath));
      CORE_TRACE("  [{}] Metalness: {}", mtlName, texPath);
    } else {
      mtlAsset->SetMetalMap(Renderer::GetBlackTexture());
    }
  } else {
    mtlAsset->SetMetalMap(Renderer::GetBlackTexture());
  }

  ms->m_Materials.push_back(mtlAsset);
}

// ================================================================
// GenVertexArrays — GPU 缓冲生成
// ================================================================
void AssimpMeshImporter::GenVertexArrays(Ref<MeshSource> &ms) {
  const double offset = 1.0 / 8000.0;

  std::vector<glm::vec3> smoothNormals;
  Utils::MeshUtils::SmoothNormal(ms->m_Verteices, smoothNormals);
  Utils::MeshUtils::SetMeshOffset(ms->m_Verteices, ms->m_SubMeshes,
                                  static_cast<float>(offset));
  Utils::MeshUtils::BackMeshNormal(ms->m_Verteices, smoothNormals);

  BufferLayout layout = {
      {ShaderDataType::Float3, "a_Position"},
      {ShaderDataType::Float3, "a_Normal"},
      {ShaderDataType::Float2, "a_TextureCroods"},
      {ShaderDataType::Float4, "a_BaseColor"},
      {ShaderDataType::Float4, "a_Tangent"},
  };

  for (auto &submesh : ms->m_SubMeshes) {
    auto VA = VertexArray::Create();
    VA->Bind();

    auto VB = VertexBuffer::Create(
        reinterpret_cast<float *>(&ms->m_Verteices[submesh.BaseVertex]),
        submesh.VertexCount * sizeof(Vertex));
    VB->Bind();
    VB->SetBufferLayout(layout);
    VA->AddVertexBuffer(VB);

    auto IB = IndexBuffer::Create(
        reinterpret_cast<uint32_t *>(&ms->m_Indices[submesh.BaseIndex]),
        submesh.IndexCount * 3);
    IB->Bind();
    VA->SetIndexBuffer(IB);

    ms->m_VertexArray.push_back(VA);
  }
}

// ================================================================
// ImportMaterialCpu — 材质导入（CPU-only，无 GL 调用）
//
// 与 ImportMaterial 的区别：
//   1. 不调用 Texture2D::Create（会触发 GL）
//   2. 使用 stbi_load 将图片加载到 CPU 内存
//   3. 创建 PendingGpuTexture 存储原始数据
//   4. MaterialAsset 先使用占位纹理（white/black texture）
// ================================================================
void AssimpMeshImporter::ImportMaterialCpu(const aiMaterial *aiMat,
                                           const aiScene * /*scene*/,
                                           Ref<MeshSource> &ms) {
  std::string mtlName = aiMat->GetName().C_Str();
  Ref<Material> mtl = Material::Create(ms->m_MeshShader, mtlName);
  Ref<MaterialAsset> mtlAsset = CreateRef<MaterialAsset>(mtl);

  // 当前材质的索引 = 已存在的材质数
  uint32_t materialIndex = static_cast<uint32_t>(ms->m_Materials.size());

  // 辅助 lambda：加载纹理到 CPU 内存，创建 PendingGpuTexture
  auto LoadTextureCpu = [&](aiTextureType aiType, const std::string& uniformName,
                             RHIFormat defaultFormat, const char* texLabel) -> bool {
    if (aiMat->GetTextureCount(aiType) > 0) {
      aiString path;
      aiMat->GetTexture(aiType, 0, &path);
      std::string texPath = ms->m_LoadPath + "/" + path.C_Str();

      if (KansFileSystem::Exists(texPath)) {
        int width, height, channels;
        stbi_set_flip_vertically_on_load(1);

        Buffer imageBuffer;
        RHIFormat format = defaultFormat;

        if (stbi_is_hdr(texPath.c_str())) {
          imageBuffer.Data = stbi_loadf(texPath.c_str(), &width, &height, &channels, 0);
          imageBuffer.Size = width * height * channels * sizeof(float);
          switch (channels) {
            case 3: format = RHIFormat::RHI_FORMAT_R16G16B16_SFLOAT; break;
            case 4: format = RHIFormat::RHI_FORMAT_R16G16B16A16_SFLOAT; break;
          }
        } else {
          imageBuffer.Data = stbi_load(texPath.c_str(), &width, &height, &channels, 0);
          imageBuffer.Size = width * height * channels;
          switch (channels) {
            case 3: format = RHIFormat::RHI_FORMAT_R8G8B8_SRGB; break;
            case 4: format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB; break;
          }
        }

        if (imageBuffer.Data) {
          CORE_TRACE("  [{}] {} (CPU): {}", mtlName, texLabel, texPath);
          PendingGpuTexture pendingTex;
          pendingTex.PixelData = imageBuffer;
          pendingTex.Width = width;
          pendingTex.Height = height;
          pendingTex.Format = format;
          pendingTex.MaterialIndex = materialIndex;
          pendingTex.TextureUniformName = uniformName;
          ms->AddPendingTexture(std::move(pendingTex));
          return true;
        }
      } else {
        CORE_TRACE("  [{}] {} not found: {}", mtlName, texLabel, texPath);
      }
    }
    return false;
  };

  // ══════════════════════════════════════════════════════════
  // Blinn-Phong 贴图
  // ══════════════════════════════════════════════════════════

  bool hasNormal   = LoadTextureCpu(aiTextureType_NORMALS,  MaterialAsset::GetNormalMapLocation(),  RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Normal");
  bool hasDiffuse  = LoadTextureCpu(aiTextureType_DIFFUSE,   MaterialAsset::GetDiffuseMapLocation(), RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Diffuse");
  bool hasSpecular = LoadTextureCpu(aiTextureType_SPECULAR,  MaterialAsset::GetSpecularMapLocation(),RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Specular");

  if (!hasNormal)   mtlAsset->SetNormalMap(Renderer::GetWhiteTexture());
  if (!hasDiffuse)  mtlAsset->SetDiffuseMap(Renderer::GetWhiteTexture());
  if (!hasSpecular) mtlAsset->SetSpecularMap(Renderer::GetBlackTexture());

  // Diffuse 相关：自动检测 _Light / _Ramp 贴图
  if (hasDiffuse) {
    // 占位 Light/Ramp（这些贴图可选，不强制延迟加载）
    mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(), Renderer::GetWhiteTexture());
    mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(), Renderer::GetWhiteTexture());
  } else {
    mtl->SetTexture(MaterialAsset::GetToneLightMapLocation(), Renderer::GetWhiteTexture());
    mtl->SetTexture(MaterialAsset::GetToneRampMapLocation(), Renderer::GetWhiteTexture());
  }

  // ══════════════════════════════════════════════════════════
  // PBR 贴图
  // ══════════════════════════════════════════════════════════

  bool hasAlbedo    = LoadTextureCpu(aiTextureType_BASE_COLOR,         MaterialAsset::GetAlbedoMapLocation(),  RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Albedo");
  bool hasAO        = LoadTextureCpu(aiTextureType_AMBIENT_OCCLUSION,  MaterialAsset::GetAOMapLocation(),      RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "AO");
  bool hasRoughness = LoadTextureCpu(aiTextureType_DIFFUSE_ROUGHNESS,  MaterialAsset::GetRoughMapLocation(),   RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Roughness");
  bool hasMetalness = LoadTextureCpu(aiTextureType_METALNESS,          MaterialAsset::GetMetalMapLocation(),   RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB, "Metalness");

  if (!hasAlbedo)    mtlAsset->SetAlbedoMap(Renderer::GetWhiteTexture());
  if (!hasAO)        mtlAsset->SetAOMap(Renderer::GetWhiteTexture());
  if (!hasRoughness) mtlAsset->SetRoughMap(Renderer::GetWhiteTexture());
  if (!hasMetalness) mtlAsset->SetMetalMap(Renderer::GetBlackTexture());

  ms->m_Materials.push_back(mtlAsset);
}

} // namespace Kans
