#include "kspch.h"
#include "VulkanRHI.h"
#include "VulkanBase.h"

#include "Kans3D/FileSystem/FileSystem.h"
//temp
#include "VulkanShader.h"
#include "ShaderCompiler/VulkanShaderCompiler.h"
namespace Kans
{
#ifdef KS_DEBUG
	static  bool s_Validation = true;
#else
	static  bool s_Validation = false;
#endif// will use for all namespace
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallBack(
		VkDebugUtilsMessageSeverityFlagBitsEXT  messageSeverity,//信息严重性
		VkDebugUtilsMessageTypeFlagsEXT messageType,//信息类型
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,//行为的相关信息
		void* pUserData);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vulkan Utils
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	inline static std::vector<const char*> validationLayers =
	{
		"VK_LAYER_KHRONOS_validation"
	};
	namespace Utils
	{
		void GetRequiredExtensions(std::vector<const char*>& extentions)
		{
			extentions.clear();
			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
			extentions.resize(glfwExtensionCount);
			extentions.assign(glfwExtensions, glfwExtensions + glfwExtensionCount);
			//std::vector<const char*> extentions = std::vector<const char*>(glfwExtensions, glfwExtensions + glfwExtensionCount);
			//std::vector(char* firstP,char* lastP,std::allocator ai)

			if (s_Validation)
			{
				extentions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			}
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan Validation Layer
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerCreateInfoEXT* pcreateinfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
		{
			auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
			if (func != nullptr) 
			{
				return func(instance, pcreateinfo, pAllocator, pDebugMessenger);
			}
			else 
			{
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}	
		}

		void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) 
		{
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr) 
			{
				func(instance, debugMessenger, pAllocator);
			}
		}

		void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
		{
			createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			createInfo.pfnUserCallback = DebugCallBack;
		}
	}
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallBack(
		VkDebugUtilsMessageSeverityFlagBitsEXT  messageSeverity,//信息严重性
		VkDebugUtilsMessageTypeFlagsEXT messageType,//信息类型
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,//行为的相关信息
		void* pUserData)
	{

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan Debug Callback
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//pCallbackData所指向的信息为
		//pMessage 调试信息
		//pObjects 相关对象数组
		//objectCount 数组的大小

		if (messageSeverity >= VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		{
			std::string logdata = pCallbackData->pMessage;
			CORE_ERROR("{0} {1} Message: \t\n {2}", Utils::VulkanMessageSeverityToString(messageSeverity), Utils::VulkanMessageTypeToString(messageType), logdata);
		}


		//返回值决定该回调函数是否应该终止程序，我们不希望终止程序，返回VK_FALSE
		return VK_FALSE;
	}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vulkan RHI
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	VulkanRHI::VulkanRHI()
		:m_WindowHandle(nullptr)
	{

	}

	VulkanRHI::VulkanRHI(const Scope<Window>& window)
		:m_WindowHandle(window.get())
	{

	}

	VulkanRHI::~VulkanRHI()
	{
		//release the resource In reverse order
		m_Device->Destroy();
		if (s_Validation) {
			Utils::DestroyDebugUtilsMessengerEXT(s_VulkanInstance, m_DebugUtilsMessenger, nullptr);
		}
		vkDestroyInstance(s_VulkanInstance, nullptr);
	}

	void VulkanRHI::Init()
	{
	
		CreateInstance();
		SelectPhysicalDevice();
		CreateLogicalDevice();

		// create the vulkan pipline
		//create the pipline dynamic state
		std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };

		VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{};
		dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();

		//create the vertex input state,we just keep this now
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions = nullptr;
		vertexInputInfo.vertexBindingDescriptionCount = 0;
		vertexInputInfo.pVertexBindingDescriptions = nullptr;

		//create input assembly describes how Vulkan use the vertex data
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo{};
		inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCreateInfo.primitiveRestartEnable = false;// it's possible to break up lines and triangles in the _STRIP topology modes by using a special index 
		inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;// select the topology of the vertex data;

		//this property of viewport will use for the frameBuffer
		WindowSpecification spec = m_WindowHandle->GetWindowSpecification();
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)spec.Width;
		viewport.height = (float)spec.Height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

	}

	void VulkanRHI::Shutdown()
	{
		m_SwapChain.Cleanup();
	}
	void VulkanRHI::CreateInstance()
	{
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan Validation
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		if (s_Validation && !Kans::Utils::checkValidationLayerSupport(validationLayers))
		{
			throw std::runtime_error("validation layers requested, but not available!");
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Application Info
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Kans3D_Vulkan_development";
		appInfo.pEngineName = "Kans3D";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 3, 1);
		appInfo.apiVersion = VK_API_VERSION_1_2; //make sure the version is same to spir-v


		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vulkan Instance Info
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		VkInstanceCreateInfo instanceInfo = {};
		instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instanceInfo.pApplicationInfo = &appInfo;

		//RequiredExtensions
		std::vector<const char*> extentions;
		Utils::GetRequiredExtensions(extentions);
		instanceInfo.enabledExtensionCount = (uint32_t)extentions.size();
		instanceInfo.ppEnabledExtensionNames = extentions.data();

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if (s_Validation)
		{
			instanceInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
			instanceInfo.ppEnabledLayerNames = validationLayers.data();

			//填充debugmessenger的createInfo
			Utils::PopulateDebugMessengerCreateInfo(debugCreateInfo);
			instanceInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
		}
		else
		{
			instanceInfo.enabledLayerCount = 0;
			instanceInfo.pNext = nullptr;
		}

		VK_CHECK_RESULT(vkCreateInstance(&instanceInfo, nullptr, &s_VulkanInstance));

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// DebugMessenger
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		if (s_Validation)
		{
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// DebugInfo
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			VkDebugUtilsMessengerCreateInfoEXT createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			//希望调用回调函数的信息严重性等级
			createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			//希望调用回调函数的信息类型
			createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			//调用的回调函数
			createInfo.pfnUserCallback = DebugCallBack;
			createInfo.pUserData = nullptr; // Optional
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			// vkCreateDebugUtilsMessengerEXT 
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			//因为vkCreateDebugUtilsMessengerEXT 是一个扩展函数，所以我们需要自行查找该函数的函数指针地址
			//使用我们自己的代理函数，再在代理函数中调用实际的创建函数
			VK_CHECK_RESULT(Utils::CreateDebugUtilsMessengerEXT(s_VulkanInstance, &createInfo, nullptr, &m_DebugUtilsMessenger))
		}

	}

	void VulkanRHI::SelectPhysicalDevice()
	{
		m_PhysicalDevice = VulkanPhysicalDevice::Select();
	}

	void VulkanRHI::CreateLogicalDevice()
	{
		m_EnabledFeatures = VkPhysicalDeviceFeatures{ false };
		m_EnabledFeatures.samplerAnisotropy = true;
		// support inefficient readback storage buffer
		m_EnabledFeatures.fragmentStoresAndAtomics = true;
		// support independent blending
		m_EnabledFeatures.independentBlend = true;
		// support geometry shader
		m_EnabledFeatures.geometryShader = true;


		m_EnabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		

		m_Device = CreateRef<VulkanDevice>(m_PhysicalDevice);
		m_Device->Create(m_EnabledFeatures, m_EnabledDeviceExtensions);

	}

	void VulkanRHI::CreateSwapChain()
	{
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Create SwapChain
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		m_SwapChain.Connect(s_VulkanInstance, m_Device);
		m_SwapChain.InitSurface((GLFWwindow*)m_WindowHandle->GetNativeWindow());

		WindowSpecification spec = m_WindowHandle->GetWindowSpecification();
		m_SwapChain.Create(&spec.Width, &spec.Height, m_WindowHandle->IsVSync());
		m_WindowHandle->SetWindowSpecification(spec);
	}


}