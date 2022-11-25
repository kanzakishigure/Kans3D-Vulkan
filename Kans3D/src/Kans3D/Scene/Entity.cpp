#include "kspch.h"
#include "Kans3D/Scene/Entity.h"

namespace Kans
{
	Entity::Entity(entt::entity handle, Scene* scene)
		:m_EntityHandle(handle),m_Scene(scene)
	{

	}

}