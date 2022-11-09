#include"hzpch.h"
#include"ImGuiLayer.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "Kans3D/Core/Application.h"

#include "Kans3D/ImGui/Colors.h"
//Temp
#include<GLFW/glfw3.h>
#include <glad/glad.h>
#include <FileSystem>


namespace Kans
{
	ImGuiLayer::ImGuiLayer()
		:Layer("ImGuiLayer")
	{
	}


	ImGuiLayer::~ImGuiLayer()
	{

	}



	void ImGuiLayer::OnAttach()
	{
		HZ_PROFILE_FUCTION();

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
		//io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

		//Kans:
		//Set the default font
		{
			//io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/OpenSans/OpenSans-VariableFont_wdth,wght.ttf", 18.0f);
			std::filesystem::path Dfontpath= "assets/fonts/Roboto/Roboto-Regular.ttf";
			io.FontDefault = io.Fonts->AddFontFromFileTTF(Dfontpath.generic_string().c_str(),16.0f);
			std::filesystem::path fontpath = "assets/fonts/Roboto/Roboto-Bold.ttf";
			io.Fonts->AddFontFromFileTTF(fontpath.generic_string().c_str(), 16.0f);
		}

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsClassic();
		// When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		//kans
		//use Default theme
		{	
			SetDarkThemeColors();
		}

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForOpenGL(window, true);
		//ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");


	}

	void ImGuiLayer::OnDetach()
	{
		HZ_PROFILE_FUCTION();

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::Begin()
	{
		HZ_PROFILE_FUCTION();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiLayer::End()
	{
		HZ_PROFILE_FUCTION();

		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();
		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}
	}

	void ImGuiLayer::OnImGuiRender()
	{

	}

	void ImGuiLayer::OnEvent(Event& e)
	{

		if (m_BlokEvents)
		{

			ImGuiIO& io = ImGui::GetIO();
			e.Handeled |= e.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			e.Handeled |= e.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;

		}

	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		
		auto& colors = ImGui::GetStyle().Colors;
		auto& style = ImGui::GetStyle();
		//========================================================
		/// Colors

			// Window Background
			colors[ImGuiCol_WindowBg]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
			colors[ImGuiCol_ChildBg]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::background);
			colors[ImGuiCol_PopupBg]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundPopup);
			colors[ImGuiCol_Border]				= ImColor(73, 80, 87, 220);
					
			
			// Headers						    
			colors[ImGuiCol_Header]			    = ImGui::ColorConvertU32ToFloat4(Colors::Theme::header);
			colors[ImGuiCol_HeaderHovered]      = ImGui::ColorConvertU32ToFloat4(Colors::Theme::header);
			colors[ImGuiCol_HeaderActive]       = ImGui::ColorConvertU32ToFloat4(Colors::Theme::header);
											    
			//Text							    
			colors[ImGuiCol_Text]               = ImGui::ColorConvertU32ToFloat4(Colors::Theme::text);
											    
			// Frame BG						    
			colors[ImGuiCol_FrameBg]		    = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);
			colors[ImGuiCol_FrameBgHovered]     = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);
			colors[ImGuiCol_FrameBgActive]	    = ImGui::ColorConvertU32ToFloat4(Colors::Theme::propertyField);
											    
			// Buttons						    
			colors[ImGuiCol_Button]             = ImColor(56, 56, 56, 200);
			colors[ImGuiCol_ButtonHovered]      = ImColor(70, 70, 70, 255);
			colors[ImGuiCol_ButtonActive]       = ImColor(56, 56, 56, 200);
											    
			// Title						    
			colors[ImGuiCol_TitleBg]            = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
			colors[ImGuiCol_TitleBgActive]      = ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
			colors[ImGuiCol_TitleBgCollapsed]   = ImGui::ColorConvertU32ToFloat4(Colors::Theme::gray_9);


			//Tab
			colors[ImGuiCol_Tab]				= ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
			colors[ImGuiCol_TabHovered]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::blue_4);
			colors[ImGuiCol_TabActive]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::blue_7);
			colors[ImGuiCol_TabUnfocused]		= ImGui::ColorConvertU32ToFloat4(Colors::Theme::titlebar);
			colors[ImGuiCol_TabUnfocusedActive] = ImGui::ColorConvertU32ToFloat4(Colors::Theme::blue_7);



			// Resize Grip
			colors[ImGuiCol_ResizeGrip]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::indigo_4);
			colors[ImGuiCol_ResizeGripHovered]	= ImGui::ColorConvertU32ToFloat4(Colors::Theme::indigo_6);
			colors[ImGuiCol_ResizeGripActive]	= ImGui::ColorConvertU32ToFloat4(Colors::Theme::indigo_7);

			//CheckBox
			colors[ImGuiCol_CheckMark]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::gray_5);
			colors[ImGuiCol_CheckMark]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::text);

			//Tabs
			colors[ImGuiCol_TableHeaderBg]		= ImGui::ColorConvertU32ToFloat4(Colors::Theme::header);
			colors[ImGuiCol_TableBorderLight]	= ImGui::ColorConvertU32ToFloat4(Colors::Theme::backgroundDark);

			//Slider
			colors[ImGuiCol_SliderGrab]			= ImVec4(0.51f, 0.51f, 0.51f, 0.7f);
			colors[ImGuiCol_SliderGrabActive]	= ImVec4(0.66f, 0.66f, 0.66f, 1.0f);

			colors[ImGuiCol_MenuBarBg]			= ImColor(0, 0, 0, 0);

			// Separator
			colors[ImGuiCol_Separator]			= ImGui::ColorConvertU32ToFloat4(Colors::Theme::gray_7);
			colors[ImGuiCol_SeparatorActive]	= ImGui::ColorConvertU32ToFloat4(Colors::Theme::highlight);
			colors[ImGuiCol_SeparatorHovered]	= ImColor(39, 185, 242, 150);
		//========================================================
		/// Style

			style.FrameRounding = 4.8f;
			style.FrameBorderSize = 1.2f;
			style.IndentSpacing = 11.0f;
			style.WindowPadding = ImVec2(1.1, 1.4);

			
	}


}



