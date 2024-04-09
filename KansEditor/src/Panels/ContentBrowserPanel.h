#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Editor/EditorResources.h"
namespace Kans
{

	class ContentBrowserPanel :EditorPanel
	{
	public:
		ContentBrowserPanel();
		virtual void onImGuiRender(bool isOpen) override;
		
	private:
		void DrawButtonHeandle(const std::string& name);
		std::vector<std::function<void()>> m_FloderDrawList;
		std::filesystem::path m_CurrentPath;
		int open_action = -1;
		float FramePadding = 8;
		float FrameBorderSize = 1.5;
		float ImageButtonSize = 96;
		glm::vec2 OuterSize = {0,0};
		glm::vec2 ItemInnerSpacing = {36,15};
		float FrameRounding = 8.0;
	};
}