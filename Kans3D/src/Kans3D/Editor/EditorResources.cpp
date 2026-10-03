#include "kspch.h"
#include "EditorResources.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Kans
{

    void EditorResources::Init()
    {
        FbxFileIcon      = LoadTexture("ContentBrowser/FBX.png");
        ModelFileIcon    = LoadTexture("ContentBrowser/Model.png");
        ObjFileIcon      = LoadTexture("ContentBrowser/OBJ.png");
        GltfFileIcon     = LoadTexture("ContentBrowser/GLTF.png");
        GlbFileIcon      = LoadTexture("ContentBrowser/GLB.png");
        BlendFileIcon    = LoadTexture("ContentBrowser/Blend.png");
        DaeFileIcon      = LoadTexture("ContentBrowser/DAE.png");
        MeshFileIcon     = LoadTexture("ContentBrowser/Mesh.png");
        TextureFileIcon  = LoadTexture("ContentBrowser/Image.png");
        SceneFileIcon    = LoadTexture("ContentBrowser/Scene.png");
        MaterialFileIcon = LoadTexture("ContentBrowser/Material.png");
        ScriptFileIcon   = LoadTexture("ContentBrowser/Script.png");
        ShaderFileIcon   = LoadTexture("ContentBrowser/Shader.png");
        AudioFileIcon    = LoadTexture("ContentBrowser/Audio.png");
        FileIcon         = LoadTexture("ContentBrowser/File.png");
        FolderIcon       = LoadTexture("ContentBrowser/Folder.png");
        BorderShadow     = LoadTexture("Border/Translucency.png");
    }

    void EditorResources::ShutDown()
    {
        FbxFileIcon.reset();
        ObjFileIcon.reset();
        GltfFileIcon.reset();
        GlbFileIcon.reset();
        BlendFileIcon.reset();
        DaeFileIcon.reset();
        MeshFileIcon.reset();
        ModelFileIcon.reset();
        TextureFileIcon.reset();
        SceneFileIcon.reset();
        MaterialFileIcon.reset();
        ScriptFileIcon.reset();
        ShaderFileIcon.reset();
        AudioFileIcon.reset();
        FileIcon.reset();
        FolderIcon.reset();
        BorderShadow.reset();
    }

    Ref<Texture2D> EditorResources::GetFileIcon(const std::filesystem::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        // Store pointers to the resource slots so Init/ShutDown never leave stale
        // refs.
        static const std::unordered_map<std::string, const Ref<Texture2D>*> icons = {
            {".fbx", &FbxFileIcon},       {".obj", &ObjFileIcon},           {".gltf", &GltfFileIcon},
            {".glb", &GlbFileIcon},       {".dae", &DaeFileIcon},           {".blend", &BlendFileIcon},
            {".mesh", &MeshFileIcon},     {".png", &TextureFileIcon},       {".jpg", &TextureFileIcon},
            {".jpeg", &TextureFileIcon},  {".bmp", &TextureFileIcon},       {".tga", &TextureFileIcon},
            {".hdr", &TextureFileIcon},   {".exr", &TextureFileIcon},       {".dds", &TextureFileIcon},
            {".ktx", &TextureFileIcon},   {".ktx2", &TextureFileIcon},      {".tif", &TextureFileIcon},
            {".tiff", &TextureFileIcon},  {".webp", &TextureFileIcon},      {".gif", &TextureFileIcon},
            {".kans", &SceneFileIcon},    {".scene", &SceneFileIcon},       {".prefab", &SceneFileIcon},
            {".mat", &MaterialFileIcon},  {".material", &MaterialFileIcon}, {".mtl", &MaterialFileIcon},
            {".cs", &ScriptFileIcon},     {".lua", &ScriptFileIcon},        {".py", &ScriptFileIcon},
            {".cpp", &ScriptFileIcon},    {".h", &ScriptFileIcon},          {".hpp", &ScriptFileIcon},
            {".glsl", &ShaderFileIcon},   {".hlsl", &ShaderFileIcon},       {".vert", &ShaderFileIcon},
            {".frag", &ShaderFileIcon},   {".geom", &ShaderFileIcon},       {".comp", &ShaderFileIcon},
            {".tesc", &ShaderFileIcon},   {".tese", &ShaderFileIcon},       {".spv", &ShaderFileIcon},
            {".shader", &ShaderFileIcon}, {".wav", &AudioFileIcon},         {".mp3", &AudioFileIcon},
            {".ogg", &AudioFileIcon},     {".flac", &AudioFileIcon},        {".aac", &AudioFileIcon},
            {".m4a", &AudioFileIcon}};
        const auto it = icons.find(extension);
        return it != icons.end() && *it->second ? *it->second : FileIcon;
    }

    Ref<Texture2D> EditorResources::LoadTexture(const std::string& relativePath)
    {
        std::filesystem::path resourcePath = KansFileSystem::GetResoucesFolder();
        resourcePath /= "Editor";
        resourcePath /= relativePath;

        TextureSpecification spec;
        spec.Format       = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;
        spec.Minf         = RHIFilter::RHI_FILTER_LINEAR;
        spec.Maxf         = RHIFilter::RHI_FILTER_LINEAR;
        spec.Wrap         = RHISamplerAddressMode::RHI_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        spec.GenerateMips = true;
        return Texture2D::Create(spec, resourcePath.string());
    }

} // namespace Kans
