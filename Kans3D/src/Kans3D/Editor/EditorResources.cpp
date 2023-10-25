#include "kspch.h"
#include "EditorResources.h"

namespace Kans
{

	
	void EditorResources::Init()
	{
		FbxFileIcon = LoadTexture("ContentBrowser/FBX.png");
		FolderIcon = LoadTexture("ContentBrowser/Folder.png");
		BorderShadow = LoadTexture("Border/Translucency.png");
	}

	void EditorResources::ShutDown()
	{
		FbxFileIcon.reset();
		FolderIcon.reset();
	}

	Ref<Texture2D> EditorResources::LoadTexture(const std::string& relativePath)
	{
		std::filesystem::path resourcePath = KansFileSystem::GetResoucesFolder();
		resourcePath /= "Editor";
		resourcePath /= relativePath;

		TextureSpecification spec;
		spec.Format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;
		return Texture2D::Create(spec,resourcePath.string());
	}

}