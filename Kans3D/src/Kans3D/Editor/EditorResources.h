#pragma once

#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Renderer/Texture.h"

namespace Kans
{
	class EditorResources
	{
	public:
		inline static Ref<Texture2D> FbxFileIcon;
		inline static Ref<Texture2D> FolderIcon;
		inline static Ref<Texture2D> ObjFileIcon;
		inline static Ref<Texture2D> ShaderFileIcon;
		inline static Ref<Texture2D> TextureFileIcon;

		inline static Ref<Texture2D> BorderShadow;

		static void Init();
		static void	ShutDown();
	private:
		static Ref<Texture2D> LoadTexture(const std::string& relativePath);

	};
}