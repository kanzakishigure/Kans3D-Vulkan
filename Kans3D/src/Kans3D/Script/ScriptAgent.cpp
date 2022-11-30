#include "kspch.h"
#include "ScriptAgent.h"
#include "ScriptEngine.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Core/KeyCodes.h"
#include "Kans3D/Core/Input.h"

#include <mono/jit/jit.h>
namespace Kans
{

#define  ADD_INTERNAL_CALL(Name) mono_add_internal_call("Kans.InternalCalls::"#Name, Name);

	static void NativeLog(glm::vec3* parameter)
	{
		//HZ_CORE_WARN("vlaue :{0}", *parameter);
		std::cout << parameter->x << parameter->y << parameter->z << std::endl;
	}
	
	static float VectorDot(glm::vec3* parameter_1, glm::vec3* parameter_2)
	{
		return glm::dot(*parameter_1, *parameter_2);
	}
	static void EntityGetTranslation(uint64_t id, glm::vec3* parameter)
	{
		Scene* scene = ScriptEngine::GetSceneContext();
		UUID uuid = { id };
		auto& entity = scene->GetEntityByUUID(uuid);
		*parameter = entity.GetComponent<TransformComponent>().Position;

	}
	static void EntitySetTranslation(uint64_t id, glm::vec3* parameter)
	{
		Scene* scene = ScriptEngine::GetSceneContext();
		UUID uuid = { id };
		auto& entity =scene->GetEntityByUUID(uuid);
		entity.GetComponent<TransformComponent>().Position = *parameter;
	}

	static bool Input_IsKeyPressed(KeyCode keycode)
	{
		return Input::IsKeyPressed(keycode);
	}
	void ScriptAgent::RegisterFunctions()
	{
		ADD_INTERNAL_CALL(NativeLog);
		ADD_INTERNAL_CALL(VectorDot);
		ADD_INTERNAL_CALL(EntityGetTranslation);
		ADD_INTERNAL_CALL(EntitySetTranslation);
		ADD_INTERNAL_CALL(Input_IsKeyPressed);
	}

}