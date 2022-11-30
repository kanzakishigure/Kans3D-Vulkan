#pragma once
#include <string.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Kans3D/Core/UUID.h"
#include "Kans3D/Renderer/SceneCamera.h"
#include "Kans3D/Renderer/Texture.h"
#include "Kans3D/Renderer/Mesh.h"
namespace Kans 
{
	struct IDComponent
	{
		UUID ID = 0;
		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
	};
	struct  TagComponent
	{
		std::string Tag;
		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(std::string tag)
			:Tag(tag)
		{}
		operator std::string& () { return Tag; }
	};
	struct  TransformComponent
	{

		glm::vec3 Position = glm::vec3(0.0f);
		//rotation here is radians
		glm::vec3 Rotation = glm::vec3(0.0f);

		glm::vec3 Scale = glm::vec3(1.0f);

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(glm::vec3 position , glm::vec3 rotation ,glm::vec3 scale )
			:Position(position),Rotation(rotation),Scale(scale)
		{}

		 glm::mat4 GetTransform () const
		{
			glm::mat4  translation = glm::translate(glm::mat4(1.0f), Position);

			glm::mat4  ratation = glm::rotate(glm::mat4(1.0f), (Rotation.x), { 1.0f,0.0f,0.0f });
			ratation *= glm::rotate(ratation,  (Rotation.y), { 0.0,1.0f,0.0f });
			ratation *= glm::rotate(ratation,  (Rotation.z), { 0.0f,0.0f,1.0f });

			glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);
			return translation * ratation * scale;
		}

	};
	struct CameraComponent
	{
		Kans::SceneCamera SceneCamera;
		bool Primary = true;
		bool FixedAspectRatio =false;
		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;
	
	};
	struct SpriteRendererComponent
	{
		glm::vec4 Color = { 1.0f ,1.0f ,1.0f ,1.0f };
		Ref<Texture2D> Texture;
		//目前未实现texture
		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent& ) = default;
		SpriteRendererComponent(glm::vec4 color,Ref<Texture2D> texture = nullptr )
			:Color(color),Texture(texture)
		{
			//Set The WhiteTexture
			if (texture==nullptr)
			{
				Texture =  Texture2D::Create(1, 1);
				uint32_t data = 0xffffffff;
				Texture->SetData(&data, sizeof(int));
			}

		}
	};


	class ScriptableEntity;
	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		ScriptableEntity* (*InstantiateFunction)();
		 void (*DestoryInstanceFunction)(NativeScriptComponent*);
	
		template<typename T>
		void Bind()
		{
			InstantiateFunction = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestoryInstanceFunction = [](NativeScriptComponent* nsc) {delete nsc->Instance; nsc->Instance = nullptr;  };
		}
	};

	struct StaticMeshComponent
	{
		//TODO
		//use subMeh vector;
		//MeshSource now have the material ,SubMesh 
		
		Ref<StaticMesh> StaticMesh;
		Ref<MaterialTable>  MaterialTable;

		StaticMeshComponent() = default;
		StaticMeshComponent(const StaticMeshComponent&) = default;
		//temp know We Don't have any asset system,should have not instance in the Component 
		//Ref<Material> MeshMaterial;

	};
	struct DirLightComponent
	{
		glm::vec3 Direction;
		glm::vec3 Diffuse_Intensity;
		glm::vec3 Specular_Intensity;
		glm::vec3 Ambient_Intensity;
		DirLightComponent() = default;
		DirLightComponent(const DirLightComponent&) = default;
	};
	struct PointLightComponent
	{
		glm::vec3 Diffuse_Intensity;
		glm::vec3 Specular_Intensity;
		glm::vec3 Ambient_Intensity;
		PointLightComponent() = default;
		PointLightComponent(const PointLightComponent&) = default;
	};
	struct MaterialComponent
	{
		MaterialComponent() = default;
		MaterialComponent(const MaterialComponent&) = default;

		Ref<MaterialTable>  MaterialTable;
	};

	struct ScriptCompoenet
	{
		ScriptCompoenet() = default;
		ScriptCompoenet(const ScriptCompoenet&) = default;
		ScriptCompoenet(const std::string& name)
			:ClassName(name)
		{

		}

		std::string ClassName;
	};
}