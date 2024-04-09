#include "ContentBrowserPanel.h"
#include "Kans3D/ImGui/KansUI.h"
#include "Kans3D/ImGui/Colors.h"
#include <imgui.h>
#include <imgui_internal.h>

#define ADJUST_CONTRNTBROWSER false
namespace Kans
{
	ContentBrowserPanel::ContentBrowserPanel()
		:m_CurrentPath(KansFileSystem::GetAssetFolder().parent_path())
	{

	}

	void ContentBrowserPanel::onImGuiRender(bool isOpen)
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar;
		auto& style = ImGui::GetStyle();
			
		ImGui::Begin("ContentBrowser",nullptr, windowFlags);
#if ADJUST_CONTRNTBROWSER	
		{
			ImGui::SliderFloat("FramePadding,", &FramePadding, 0, 100);
			ImGui::SliderFloat("FrameBorderSize,", &FrameBorderSize, -100, 100);
			Kans::UI::DrawVec2Control("outerSize", OuterSize);
			Kans::UI::DrawVec2Control("ItemInnerSpacing", ItemInnerSpacing);
			ImGui::SliderFloat("ImageButtonSize,", &ImageButtonSize, 32, 198);
			ImGui::SliderFloat("FrameRounding", &FrameRounding, 0, 100);
		}
#endif	
		//Set Content Browser Size
		
		
		float minsize = style.WindowMinSize.x;
		style.WindowMinSize.x = 600;	
		style.WindowMinSize.y = 340;
				
		
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,  FrameBorderSize);
		

		//Content Layer
		{
			//
			static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody| ImGuiTableFlags_NoPadInnerX|ImGuiTableFlags_NoPadOuterX;

			ImVec2 TabelSize = ImGui::GetWindowSize();
			TabelSize.y = TabelSize.y * 0.94;
			float  contentSize   = TabelSize.x * 1.0 / 5.0;
			
			//ContentBrowser
			if (ImGui::BeginTable("ContentBrowser", 2, flags, { OuterSize.x,TabelSize.y }))
			{

				
				ImGui::TableSetupColumn(KansFileSystem::GetAssetFolder().filename().string().c_str(), ImGuiTableColumnFlags_NoHeaderLabel|ImGuiTableColumnFlags_NoHide);
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHide);
				ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
				
				{
					ImGui::TableSetColumnIndex(0);
					const char* column_name = ImGui::TableGetColumnName(0); // Retrieve name passed to TableSetupColumn()
					ImGui::PushID(column_name);
					ImGui::TableHeader(column_name);
					ImGui::PopID();
				}
				{
					ImGui::TableSetColumnIndex(1);
					const char* column_name = ImGui::TableGetColumnName(1); // Retrieve name passed to TableSetupColumn()
					ImGui::PushID(column_name);
					auto debugpath = KansFileSystem::GetAssetFolder();
					
					if (m_CurrentPath != KansFileSystem::GetAssetFolder().parent_path())
					{
						if(0)
						{
							//ImGui::TableNextColumn();
							std::string spiltPath = m_CurrentPath.string();
							size_t nop = spiltPath.find_first_of("\\");
							std::string name = spiltPath.substr(0, nop);
							while (nop != std::string::npos)
							{

								if (ImGui::Button(name.c_str()))
								{
									m_CurrentPath = m_CurrentPath.parent_path();
									CLIENT_WARN("current path:{}", m_CurrentPath.string());
								}
								CLIENT_INFO(spiltPath);
								name = spiltPath.substr(0, nop);
								nop = spiltPath.find_first_of("\\");
								spiltPath = spiltPath.substr(nop, spiltPath.length());
							}
						}
						if (ImGui::Button("<<---"))
						{
							m_CurrentPath = m_CurrentPath.parent_path();
							CLIENT_WARN("current path:{0}",m_CurrentPath.string());
						}
						
					}
					else
					{
						if (ImGui::Button(m_CurrentPath.string().c_str()))
						{
							
						}
					}
					//ImGui::TableHeader(column_name);
					ImGui::PopID();
				}

				//FloderList Window
				ImGui::TableNextColumn();
				//ImGuiTableFlags_ScrollY|ImGuiTableFlags_BordersOuterH |
				ImGuiTableFlags floderListflags = ImGuiTableFlags_NoBordersInBody|ImGuiTableFlags_ScrollY;

				if (ImGui::BeginTable("FloderList", 1, floderListflags,{ OuterSize.x,TabelSize.y }))
				{
					// The first column will use the default _WidthStretch when ScrollX is Off and _WidthFixed when ScrollX is On
					
						//ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 12.0f);
						//ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 18.0f);
						//ImGui::TableHeadersRow();

						// Simple storage to output a dummy file-system.
						struct MyTreeNode
					{
						const char* Name;
						const char* Type;
						int             Size;
						int             ChildIdx;
						int             ChildCount;
						static void DisplayNode(const MyTreeNode* node, const MyTreeNode* all_nodes)
						{
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							const bool is_folder = (node->ChildCount > 0);
							if (is_folder)
							{
								bool open = ImGui::TreeNodeEx(node->Name, ImGuiTreeNodeFlags_SpanFullWidth);
								//ImGui::TableNextColumn();
								//ImGui::TextDisabled("--");
								//ImGui::TableNextColumn();
								//ImGui::TextUnformatted(node->Type);
								if (open)
								{
									for (int child_n = 0; child_n < node->ChildCount; child_n++)
										DisplayNode(&all_nodes[node->ChildIdx + child_n], all_nodes);
									ImGui::TreePop();
								}
							}
							else
							{
								ImGui::TreeNodeEx(node->Name, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
								//ImGui::TableNextColumn();
								//ImGui::Text("%d", node->Size);
								//ImGui::TableNextColumn();
								//ImGui::TextUnformatted(node->Type);
							}
						}


					};
					static const MyTreeNode nodes[] =
					{
						{ "Root",                         "Folder",       -1,       1, 3    }, // 0
						{ "Music",                        "Folder",       -1,       4, 2    }, // 1
						{ "Textures",                     "Folder",       -1,       6, 3    }, // 2
						{ "desktop.ini",                  "System file",  1024,    -1,-1    }, // 3
						{ "File1_a.wav",                  "Audio file",   123000,  -1,-1    }, // 4
						{ "File1_b.wav",                  "Audio file",   456000,  -1,-1    }, // 5
						{ "Image001.png",                 "Image file",   203128,  -1,-1    }, // 6
						{ "Copy of Image001.png",         "Image file",   203256,  -1,-1    }, // 7
						{ "Copy of Image001 (Final2).png","Image file",   203512,  -1,-1    }, // 8
					};

					MyTreeNode::DisplayNode(&nodes[0], nodes);
					
					ImGui::EndTable();
				}



				
				ImGui::TableNextColumn();
				
				//Content Window
				//ImGuiTableFlags_BordersV|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable| ImGuiTableFlags_NoBordersInBody 
				ImGuiTableFlags contentListFlag =  ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_ScrollY| ImGuiTableFlags_BordersV| ImGuiTableFlags_NoBordersInBody;
				
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { FramePadding,FramePadding });
				ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {ItemInnerSpacing.x,ItemInnerSpacing .y});
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, FrameRounding);
				ItemInnerSpacing.y = ImageButtonSize * 0.25;
				int contentListCol = ImGui::GetContentRegionAvail().x / (ImageButtonSize +FramePadding+ ItemInnerSpacing.x);
				contentListCol = contentListCol > 1 ? contentListCol : 1;
				std::filesystem::directory_iterator fit(m_CurrentPath);
	

			
				if (ImGui::BeginTable("ContentList", contentListCol, contentListFlag,{ OuterSize.x,TabelSize.y }))
				{
					
					ImGui::BeginGroup();
					ImGui::PushItemWidth(ImageButtonSize + FramePadding + ItemInnerSpacing.x);
					std::filesystem::directory_iterator it(m_CurrentPath);
					for (auto& path : it)
					{

						std::string filename = path.path().filename().string();

						ImGui::PushID(filename.c_str());
						ImGui::TableNextColumn();
						if (path.is_directory())
						{
							ImTextureID IconID = (ImTextureID)EditorResources::FolderIcon->GetRenererID();
							if (ImGui::ImageButton(IconID, { ImageButtonSize,ImageButtonSize }, { 0,1 }, { 1,0 }))
							{
								m_CurrentPath = path;
							}
						}
						else
						{
							ImTextureID IconID = (ImTextureID)EditorResources::FbxFileIcon->GetRenererID();
							if (ImGui::ImageButton(IconID, { ImageButtonSize,ImageButtonSize }, { 0,1 }, { 1,0 }))
							{

							}
						}
						ImGui::Text("%s", filename.c_str());
						ImGui::PopID();
					}
					ImGui::PopItemWidth();
					ImGui::EndGroup();
					ImGui::EndTable();
				}
				ImGui::EndTable();
				ImGui::PopStyleColor(1);
				ImGui::PopStyleVar(4);
			}
			
			

			if (open_action != -1)
				ImGui::SetNextItemOpen(open_action != 0);
		}
		
		ImGui::PopStyleVar(1);
		ImGui::End();

	}

}