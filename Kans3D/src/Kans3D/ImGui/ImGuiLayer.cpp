#include"hzpch.h"
#include"ImGuiLayer.h"

#include <imgui.h>
#include <examples/imgui_impl_glfw.h>
#include <examples/imgui_impl_opengl3.h>

#include "Kans3D/Core/Application.h"
//Temp
#include<GLFW/glfw3.h>
#include <glad/glad.h>


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

		//Set the default font
		{
			//io.FontDefault = io.Fonts->AddFontFromFileTTF("assets/fonts/OpenSans/OpenSans-VariableFont_wdth,wght.ttf", 18.0f);
			io.FontDefault = io.Fonts->AddFontFromFileTTF("J:/Vulkan_Engine/KansEditor/assets/fonts/Roboto/Roboto-Regular.ttf", 16.0f);
			io.Fonts->AddFontFromFileTTF("J:/Vulkan_Engine/KansEditor/assets/fonts/Roboto/Roboto-Bold.ttf", 16.0f);
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

		{	
			SetDarkThemeColors();
		}

		Application& app = Application::Get();
		GLFWwindow* window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForOpenGL(window, true);
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
		colors[ImGuiCol_FrameBg] = ImVec4(57 / 256.0f, 55 / 256.0f, 51 / 256.0f, 1.0f);
	}


}



