#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Renderer/OrthographicCameraController.h"
namespace Kans
{
	struct  ParticleProps
	{
		glm::vec2 Position;
		glm::vec2 Velocity, VelocityVariation;//粒子速度与粒子加速度
		glm::vec4 ColorBegin,ColorEnd;
		float SizeBegin, SizeEnd, SizeVariation;
		float LifeTime = 1.0f;
	};
	//目前只支持2D粒子效果，粒子属性仍为3D属性
	class ParticleSystem
	{
	public: 
		ParticleSystem(uint32_t MaxParticle = 100000);
		void OnUpdate(TimeStep ts);
		void OnRender(OrthographicCamera& camera);
		void Emit(const ParticleProps& particleprops);
	private:
		struct  Particle
		{
			glm::vec2 Position;
			glm::vec2 Velocity;
			glm::vec4 ColorBegin, ColorEnd;
			float Rotation = 0.0f;
			float SizeBegin, SizeEnd;

			float LifeTime = 1.0f;
			float LifeRemaining = 0.0f;
			
			bool Active = false;
		};

		//核心思想在于对于粒子，我们不会在每次需要发射粒子的时候去生成，而是提前准备好粒子池，在需要发射粒子时，对池内粒子赋值
		std::vector<Particle> m_ParticlePool;
		uint32_t m_PoolIndex;
		
	
	};
}