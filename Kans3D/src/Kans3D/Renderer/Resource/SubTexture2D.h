#pragma once
#include "Kans3D/Renderer/Resource/Texture.h"
#include "glm/glm.hpp"
namespace Kans
{
	class SubTexture2D :public RefCounter
	{
	public:
		SubTexture2D(Ref<Texture2D> texture, glm::vec2 min, glm::vec2 max);
		static Ref<SubTexture2D> CreateFromCroods(Ref<Texture2D> texture, glm::vec2 croods, glm::vec2 cellsize, glm::vec2 sprtesize = {1.0f,1.0f});

		const Ref<Texture2D> GetTexture() const { return m_Texture; }
		const glm::vec2* GetCroods() const { return m_SubTexCroods; }
	private:
		Ref<Texture2D> m_Texture;
		glm::vec2 m_SubTexCroods[4];
	};
}