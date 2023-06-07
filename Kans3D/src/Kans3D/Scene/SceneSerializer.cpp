#include "kspch.h"
#include "SceneSerializer.h"
#include "Entity.h"
#include "Components.h"

#include "Kans3D/Core/UUID.h"

#include <yaml-cpp/yaml.h>
namespace Kans
{
	
	SceneSerializer::SceneSerializer(const Ref<Scene>& Scene)
		:m_Scene(Scene)
	{

	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Serialize 
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	

	void SceneSerializer::Serialize(const std::filesystem::path& path)
	{
		//TODO:
		//Now we Have to write all component serialize
		//if we have reflection system we maybe fix this,know we just keep this
		//KANZAKI

		YAML::Emitter yamlos;
		yamlos << YAML::BeginMap;
		yamlos << YAML::Key   << "Scene";
		yamlos << YAML::Value <<  m_Scene->GetName();
		
		auto entities = m_Scene->m_Registry.view<TagComponent>();
		for (entt::entity entity : entities)
		{
			Entity entt = { entity,m_Scene.get() };
			yamlos << YAML::BeginMap;
			yamlos << YAML::Key << "Entity";
			yamlos << YAML::Value << (uint64_t)entt.GetComponent<IDComponent>().ID;
			SerializeEntity(yamlos, entt);
			yamlos << YAML::EndMap;
		}

		yamlos << YAML::EndMap;

		std::ofstream fout(path);
		fout << yamlos.c_str();
	}

	void SceneSerializer::SerializeEntity(YAML::Emitter& out, Entity entity)
	{
		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Tag";
			out << YAML::Value << entity.GetComponent<TagComponent>().Tag;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Position" << YAML::Value << entity.GetComponent<TransformComponent>().Position;
			out << YAML::Key << "Rotation" << YAML::Value << entity.GetComponent<TransformComponent>().Rotation;
			out << YAML::Key << "Scale" << YAML::Value << entity.GetComponent<TransformComponent>().Scale;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<CameraComponent>())
		{
			auto& sceneCamera = entity.GetComponent<CameraComponent>().SceneCamera;
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "ProjectionType" << YAML::Value <<(int)sceneCamera.GetProjectionType();
			out << YAML::Key << "PerspectiveFOV" << YAML::Value << sceneCamera.GetPerspectiveVerticalFOV();
			out << YAML::Key << "PerspectiveFarClip" << YAML::Value << sceneCamera.GetPerspectiveFarClip();
			out << YAML::Key << "PerspectiveNearClip" << YAML::Value << sceneCamera.GetPerspectiveNearClip();

			out << YAML::Key << "OrthographicSize" << YAML::Value << sceneCamera.GetOrthographicSize();
			out << YAML::Key << "OrthographicFarClip" << YAML::Value << sceneCamera.GetOrthographicFarClip();
			out << YAML::Key << "OrthographicNearClip" << YAML::Value << sceneCamera.GetOrthographicNearClip();
			out << YAML::Key << "AspectRatio" << YAML::Value << sceneCamera.GetAspectRatio();

			out << YAML::Key << "Primary" << YAML::Value << entity.GetComponent<CameraComponent>().Primary;
			out << YAML::Key << "FixedAspectRatio" << YAML::Value << entity.GetComponent<CameraComponent>().FixedAspectRatio;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			const auto& spriteCMP = entity.GetComponent<SpriteRendererComponent>();
			const auto& texture = spriteCMP.Texture;
			out << YAML::Key << "SpriteRendererComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Color" << YAML::Value << spriteCMP.Color;
			out << YAML::Key << "Texture" << YAML::Value << texture->GetPath();
			out << YAML::Key << "TilingFactor" << YAML::Value << spriteCMP.TilingFactor;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<StaticMeshComponent>())
		{
			const auto& meshCMP = entity.GetComponent<StaticMeshComponent>();
			out << YAML::Key << "StaticMeshComponent";
			out << YAML::BeginMap;

			out << YAML::EndMap;
		}
		if (entity.HasComponent<DirLightComponent>())
		{
			const auto& dirlightCMP = entity.GetComponent<DirLightComponent>();
			out << YAML::Key << "DirLightComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Direction" << YAML::Value << dirlightCMP.Diffuse_Intensity;
			out << YAML::Key << "Diffuse_Intensity" << YAML::Value << dirlightCMP.Diffuse_Intensity;
			out << YAML::Key << "Specular_Intensity" << YAML::Value << dirlightCMP.Specular_Intensity;
			out << YAML::Key << "Ambient_Intensity" << YAML::Value << dirlightCMP.Ambient_Intensity;

			out << YAML::EndMap;
		}
		if (entity.HasComponent<PointLightComponent>())
		{
			const auto& pointlightCMP = entity.GetComponent<PointLightComponent>();
			out << YAML::Key << "PointLightComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Diffuse_Intensity" << YAML::Value << pointlightCMP.Diffuse_Intensity;
			out << YAML::Key << "Specular_Intensity" << YAML::Value << pointlightCMP.Specular_Intensity;
			out << YAML::Key << "Ambient_Intensity" << YAML::Value << pointlightCMP.Ambient_Intensity;
			out << YAML::EndMap;
		}
		if (entity.HasComponent<MaterialComponent>())
		{
			const auto& materialCMP = entity.GetComponent<MaterialComponent>();
			out << YAML::Key << "MaterialComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "MaterialTable" << YAML::Value <<"";
			out << YAML::EndMap;
		}
		if (entity.HasComponent<ScriptComponent>())
		{
			const auto& scriptCMP = entity.GetComponent<ScriptComponent>();
			out << YAML::Key << "ScriptComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "ClassName" << YAML::Value << scriptCMP.ClassName;
			out << YAML::EndMap;
		}
	}
	void SceneSerializer::SerializeRuntime(const std::filesystem::path& path)
	{
		
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DeSerialize
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void SceneSerializer::Deserialize(const std::filesystem::path& path)
	{

	}

	void SceneSerializer::DeserializeEntity(YAML::Node& entitiesNode, Ref<Scene> scene)
	{

	}

	

}