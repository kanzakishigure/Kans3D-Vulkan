#pragma once

#include "Scene.h"
#include <filesystem>

namespace Kans
{
	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& Scene);

		void Serialize(const std::filesystem::path& path);
		void SerializeRuntime(const std::filesystem::path& path);
	private:
		Ref<Scene> m_Scene;
	};
}