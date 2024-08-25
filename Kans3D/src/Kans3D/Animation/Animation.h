#pragma once
#include "Kans3D/Renderer/Resource/Mesh.h"
namespace Kans
{

	struct Node {
		Node* parent;
		uint32_t				index;
		std::vector<Node*>		children;
		Mesh					mesh;
		int32_t					skin = -1;

		glm::vec3				translation{ 0.0f };
		glm::vec3				scale{ 1.0f };
		glm::quat				rotation{};
		glm::mat4				matrix;//must store the bindInitialMatrix



		glm::mat4				getLocalMatrix() { return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix; }
		~Node() {
			for (auto& child : children) {
				delete child;
			}
		}
	};

	struct AnimationChannel
	{
		int samplerIndex;              // required
		Node* node;          // target node pointer
		std::string path;  // required in ["translation", "rotation", "scale", "weights"]
	};
	struct AnimationSampler
	{
		std::vector<float>     inputs;
		std::vector<glm::vec4> outputsVec4;
		std::string interpolation;  // "LINEAR", "STEP","CUBICSPLINE" or user defined
		// string. default "LINEAR"
	};

	class Animation
	{
	public:

	private:
		std::string                   name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float                         start = std::numeric_limits<float>::max();
		float                         end = std::numeric_limits<float>::min();
		float                         currentTime = 0.0f;
	};
}