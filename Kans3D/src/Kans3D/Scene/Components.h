#pragma once
#include <string.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Kans3D/Core/UUID.h"

#include "Kans3D/Renderer/Resource/SceneCamera.h"
#include "Kans3D/Renderer/Resource/Texture.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
namespace Kans 
{
	class ScriptableEntity;



	struct IDComponent
	{
		UUID ID = 0;
		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
	};
	
	struct TagComponent
	{
		std::string Tag;
		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(std::string tag)
			:Tag(tag)
		{}
		operator std::string& () { return Tag; }
	};
	
	struct TransformComponent
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
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));
			glm::mat4 scale = glm::scale(glm::mat4(1.0f), Scale);
			return translation * rotation * scale;
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
		float TilingFactor = 1.0f;
		//目前未实现texture
		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent& ) = default;
		SpriteRendererComponent(glm::vec4 color,Ref<Texture2D> texture = nullptr )
			:Color(color),Texture(texture)
		{
			//Set The WhiteTexture
			if (texture==nullptr)
			{
				TextureSpecification spec;
				spec.Width = 1;
				spec.Height = 1;
				spec.Format = RHIFormat::RHI_FORMAT_R8G8B8A8_SRGB;
				uint32_t data = 0xffffffff;
				Texture = Texture2D::Create(spec, Buffer(&data, sizeof(uint32_t)));
			}

		}
	};

	//can't be serialize
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

	struct ScriptComponent
	{
		ScriptComponent() = default;
		ScriptComponent(const ScriptComponent&) = default;
		ScriptComponent(const std::string& name)
			:ClassName(name)
		{

		}

		std::string ClassName;
	};



	//Component Type Register
	template <typename ... Component>
	struct ComponentGroup
	{
	};
	using AllComponents = ComponentGroup<IDComponent,TagComponent,TransformComponent,CameraComponent,SpriteRendererComponent,
										NativeScriptComponent,StaticMeshComponent,DirLightComponent, PointLightComponent, 
										MaterialComponent, ScriptComponent >;
}