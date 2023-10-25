#pragma once
namespace Kans
{

	// the abstract of render able entity
	class RenderResourceBase
	{
	public:
		virtual ~RenderResourceBase() {}

		virtual void Clear() = 0;

	};
}