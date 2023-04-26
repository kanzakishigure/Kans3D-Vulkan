#include "kspch.h"
#include "ScriptAgent.h"
#include "ScriptEngine.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Input/KeyCodes.h"
#include "Kans3D/Input/Input.h"

#include <mono/jit/jit.h>
namespace Kans
{
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//Script Native Function
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define  ADD_INTERNAL_CALL(Name) mono_add_internal_call("Kans.InternalCalls::"#Name, Name);

#define  RegisterMaco(ClassType)

	static std::unordered_map<MonoType*, std::function<bool(Entity)>> s_HasComponentFuncs;

	static inline Entity GetEntity(uint64_t entityID)
	{
		Scene* scene = ScriptEngine::GetSceneContext();
		CORE_ASSERT(scene, "No active scene!");
		return scene->GetEntityByUUID(entityID);
	};
	static void NativeLog(glm::vec3* parameter)
	{
		//HZ_CORE_WARN("vlaue :{0}", *parameter);
		std::cout << parameter->x << parameter->y << parameter->z << std::endl;
	}
	
	static float VectorDot(glm::vec3* parameter_1, glm::vec3* parameter_2)
	{
		return glm::dot(*parameter_1, *parameter_2);
	}
	static void Entity_GetTranslation(uint64_t id, glm::vec3* parameter)
	{
		auto& entity = GetEntity(id);
		*parameter = entity.GetComponent<TransformComponent>().Position;

	}
	static void Entity_SetTranslation(uint64_t id, glm::vec3* parameter)
	{
		auto& entity = GetEntity(id);
		if (!entity)
		{
			CORE_WARN("Entity.SetTranslation - Invalid entity!");
			return;
		}
		entity.GetComponent<TransformComponent>().Position = *parameter;
	}
	static bool Entity_HasComponent(uint64_t id, MonoReflectionType* type)
	{
		auto& entity = GetEntity(id);
		if (!entity)
		{
			CORE_WARN("Entity.HasComponent - Invalid entity!");
			return false;
		}

		MonoType* managedType = mono_reflection_type_get_type(type);
		return s_HasComponentFuncs.at(managedType)(entity);
	}
	static bool Input_IsKeyPressed(KeyCode keycode)
	{
		return Input::IsKeyPressed(keycode);
	}
	
	
	template<typename... Component>
	static void RegisterComponent()
	{
		CORE_INFO("TypeCount: {}", sizeof...(Component));
		// use template lambda function ,expand ...Component
		([]()
			{
				//RegisterMaco(Component);
				std::string_view str = typeid(Component).name();
				size_t pos   = str.find_last_of(':');
				std::string_view componentName = str.substr(pos+1);
				std::string managedTypeName = fmt::format("Kans.{0}", componentName);

				MonoType* managedType= mono_reflection_type_from_name(managedTypeName.data(), ScriptEngine::GetCoreAssemblyImage());
				if (!managedType)
				{
					CORE_ERROR("cloud not find component type :{0}", managedTypeName);
				}
				else
				{
					CORE_INFO("reg Type :{0} ", managedTypeName);
				}
				
				s_HasComponentFuncs[managedType] = [](Entity entity) { return  entity.HasComponent<Component>(); };

			}(), ...);

		
	}
	template<typename... Component>
	static void RegisterComponent(ComponentGroup<Component ...>)
	{
		RegisterComponent<Component ...>();
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Script Agent
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	void ScriptAgent::RegisterFunctions()
	{
		ADD_INTERNAL_CALL(NativeLog);
		ADD_INTERNAL_CALL(VectorDot);
		ADD_INTERNAL_CALL(Entity_GetTranslation);
		ADD_INTERNAL_CALL(Entity_SetTranslation);
		ADD_INTERNAL_CALL(Input_IsKeyPressed);
		ADD_INTERNAL_CALL(Entity_HasComponent);
	}
	void ScriptAgent::RegisterComponents()
	{
		RegisterComponent(AllComponents()); 
	}

}