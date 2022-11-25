#include "kspch.h"
#include "ScriptEngine.h"
#include "FileSystem/FileSystem.h"

#include <mono/jit/jit.h>
#include<mono/metadata/assembly.h>
#include <mono/metadata/object.h>

namespace Kans
{

	namespace Utils
	{
		MonoAssembly* GetMonoAssemby(const std::string& path)
		{
			Buffer data = KansFileSystem::ReadBytes(path);
			MonoImageOpenStatus status;
			MonoImage* image = mono_image_open_from_data_full(data.As<char>(), data.Size, true, &status, false);

			if (status != MonoImageOpenStatus::MONO_IMAGE_OK)
			{
				std::string message = mono_image_strerror(status);
				HZ_CORE_ERROR("load core Assembly fail:{0}", message);
			}
			data.Release();
			MonoAssembly* monoAssembly  = mono_assembly_load_from_full(image, path.c_str(), &status, false);

			return monoAssembly;
		}
	}
	struct ScrtiptEngineContext
	{
		MonoDomain* RootDomain;
		MonoDomain* AppDomain;
		MonoAssembly* CoreAssembly;
		MonoObject* Instance;
	};
	static ScrtiptEngineContext* S_Context;
	void ScriptEngine::Init()
	{
		if(S_Context!=nullptr)
		{
			HZ_CORE_WARN("ScriptEinge arlley Init");
			return;
		}
		S_Context = new ScrtiptEngineContext();

		InitMono();
		
	}

	void ScriptEngine::ShutDown()
	{
		ShutDownMono();
		delete[] S_Context;
	}

	void ScriptEngine::InitMono()
	{
		mono_set_assemblies_path("mono/lib");
		S_Context->RootDomain = mono_jit_init("ScripteCore");

		HZ_ASSERT(S_Context->RootDomain, "mono_jit_init fail");

		S_Context->AppDomain = mono_domain_create_appdomain("Script-core", nullptr);
		mono_domain_set(S_Context->AppDomain, true);
		//Get the Assembly
		S_Context->CoreAssembly = Utils::GetMonoAssemby(KansFileSystem::GetScriptDLLPath().string());
		//Load Class
		MonoImage* image = mono_assembly_get_image(S_Context->CoreAssembly);
		MonoClass* klass = mono_class_from_name(image, "Kans", "Main"); //the mono naming is klass,is not naming mistake

		//create the instance of class
		S_Context->Instance = mono_object_new(S_Context->AppDomain, klass); //allocate memory 

		mono_runtime_object_init(S_Context->Instance);	// Call the parameterless (default) constructor

		//call the function of the Instance;

		MonoClass* instanceclass = mono_object_get_class(S_Context->Instance);

		if (1)
		{
			MonoMethod* printstringmethod = mono_class_get_method_from_name(instanceclass, "Printstring", 2);
			MonoString* string1 = mono_string_new(S_Context->AppDomain, "Kanzaki");
			MonoString* string2 = mono_string_new(S_Context->AppDomain, "200");
			void* args[] =
			{
				string1,
				string2
			};
			mono_runtime_invoke(printstringmethod, S_Context->Instance, args, nullptr);
		}

		{
			MonoMethod* printFloatVarmethod = mono_class_get_method_from_name(instanceclass, "PrintFloatVar", 0);
			mono_runtime_invoke(printFloatVarmethod, S_Context->Instance, nullptr, nullptr);
		}
	}

	void ScriptEngine::ShutDownMono()
	{
		S_Context->CoreAssembly = nullptr;
		S_Context->AppDomain = nullptr;
		S_Context->RootDomain = nullptr;
	}

}