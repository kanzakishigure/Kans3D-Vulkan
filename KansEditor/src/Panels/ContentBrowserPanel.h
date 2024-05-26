#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Editor/EditorResources.h"
#include "ContentBrowserItem.h"
namespace Kans
{
	

	class ContentBrowserPanel :EditorPanel
	{
	public:
		ContentBrowserPanel();
		virtual void onImGuiRender(bool isOpen) override;
		
		static ContentBrowserPanel* Get() { return s_Instance; }
		
	private:
		std::vector<ContentBrowserItem> m_ContentBrowserItemList;

		
		
		std::filesystem::path m_CurrentPath;
		int open_action = -1;
		float FramePadding = 8;
		float FrameBorderSize = 1.5;
		float ImageButtonSize = 96;
		glm::vec2 OuterSize = {0,0};
		glm::vec2 ItemInnerSpacing = {36,15};
		float FrameRounding = 8.0;

		bool NeedRefresh = true;
		static ContentBrowserPanel* s_Instance;
		friend class ContentBrowserItem;
	};
}