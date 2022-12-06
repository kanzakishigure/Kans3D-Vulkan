#include "kspch.h"
#include "ScriptEngine.h"
#include "FileSystem/FileSystem.h"
#include "Script/ScriptAgent.h"
#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Components.h"


#include <mono/jit/jit.h>
#include<mono/metadata/assembly.h>
#include <mono/metadata/object.h>
namespace Kans
{
	
	namespace Utils
	{
		static MonoAssembly* LoadMonoAssemby(const std::filesystem::path& path)
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
			std::string pathString = path.string();
			MonoAssembly* monoAssembly  = mono_assembly_load_from_full(image, pathString.c_str(), &status, false);

			return monoAssembly;
		}
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Script Engine
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	struct ScrtiptEngineContext
	{
		MonoDomain* RootDomain;
		MonoDomain* AppDomain;
		MonoAssembly* CoreAssembly;
		MonoImage* CoreAssemblyImage;
		ScriptClass EntityClass;
		Scene* SceneContext;

		std::unordered_map<std::string, Ref<ScriptClass>> EntityClasses;
		std::unordered_map<UUID, Ref<ScriptInstance>> EntityInstances;
	};
	static ScrtiptEngineContext* s_Context;

	void ScriptEngine::Init()
	{
		if(s_Context !=nullptr)
		{
			HZ_CORE_WARN("ScriptEinge arlley Init");
			return;
		}
		s_Context = new ScrtiptEngineContext();

		InitMono();
		//load MetaData from class
		LoadAssembly(KansFileSystem::GetScriptAssemblyPath());
		s_Context->EntityClass = ScriptClass("Kans", "Entity");
		LoadAssemblyClasses(s_Context->CoreAssembly);
		
		//reg Component
		ScriptAgent::RegisterComponents();
		//reg Internal call
		ScriptAgent::RegisterFunctions();
#if 0
		s_Context->CoreAssemblyImage = mono_assembly_get_image(s_Context->CoreAssembly);
		s_Context->EntityClass = ScriptClass("Kans","Entity");

		//create the instance of class
		MonoObject* instance = s_Context->EntityClass.Instantiate(); //allocate memory 	
		//call the function of the Instance;
		{
			MonoMethod* printstringmethod = s_Context->EntityClass.GetMethod("Printstring", 2);
			MonoString* string1 = mono_string_new(s_Context->AppDomain, "Kanzaki");
			MonoString* string2 = mono_string_new(s_Context->AppDomain, "200");
			void* args[] =
			{
				string1,
				string2
			};
			s_Context->EntityClass.InvokeMethod(instance, printstringmethod, args);
		}

		{
			MonoMethod* printFloatVarmethod = s_Context->EntityClass.GetMethod("PrintFloatVar", 0);
			s_Context->EntityClass.InvokeMethod(instance, printFloatVarmethod);
		}
#endif

		
	}

	void ScriptEngine::ShutDown()
	{
		ShutDownMono();

		delete s_Context;
	}

	void ScriptEngine::LoadAssembly(const std::filesystem::path& filepath)
	{

		s_Context->AppDomain = mono_domain_create_appdomain("Script-Core", nullptr);
		mono_domain_set(s_Context->AppDomain, true);

		//loadmonoAssembly to AppDomain
		s_Context->CoreAssembly = Utils::LoadMonoAssemby(filepath);
		s_Context->CoreAssemblyImage = mono_assembly_get_image(s_Context->CoreAssembly);

	}

	bool ScriptEngine::EntityClassExists(const std::string& className)
	{
		return s_Context->EntityClasses.find(className) != s_Context->EntityClasses.end();
	}

	void ScriptEngine::OnRuntimeStart(Scene* scene)
	{
		s_Context->SceneContext = scene;
	}

	void ScriptEngine::OnRuntimeEnd()
	{
		s_Context->SceneContext = nullptr;
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		auto&scriptCMP = entity.GetComponent<ScriptComponent>();
		if (EntityClassExists(scriptCMP.ClassName))
		{
			auto& id = entity.GetUUID();
			auto klass = s_Context->EntityClasses[scriptCMP.ClassName];
			Ref<ScriptInstance> instance = CreateRef<ScriptInstance>(klass,entity);
			s_Context->EntityInstances[id] = instance;
			instance->InvekeOnCreate();
		}
	
	}



	void ScriptEngine::OnUpdateEntity(Entity entity, TimeStep ts)
	{
		auto& scriptCMP = entity.GetComponent<ScriptComponent>();
		auto& id = entity.GetUUID();
		if (EntityClassExists(scriptCMP.ClassName))
		{
			
			Ref<ScriptInstance> instance = s_Context->EntityInstances.at(id);
			instance->InvokeOnUpdate((float)ts);
		}
		else
		{
			HZ_CORE_ERROR("Could not find ScriptInstance for entity {}", id);
		}
	}

	Kans::Scene* ScriptEngine::GetSceneContext()
	{
		return s_Context->SceneContext;
	}

	void ScriptEngine::InitMono()
	{
		mono_set_assemblies_path("mono/lib");

		MonoDomain* rootDomain = mono_jit_init("ScripteCore");
		HZ_ASSERT(rootDomain, "mono_jit_init fail");
		s_Context->RootDomain = rootDomain;
		
	}

	void ScriptEngine::ShutDownMono()
	{
		//todo mono can't release correctly

		mono_image_close(s_Context->CoreAssemblyImage);
		s_Context->CoreAssemblyImage = nullptr;

		mono_assembly_close(s_Context->CoreAssembly);
		s_Context->CoreAssembly = nullptr;
		
		//mono_domain_set(mono_get_root_domain(), false);

		//mono_domain_free(s_Context->AppDomain,false);
		//s_Context->AppDomain = nullptr;

		mono_jit_cleanup(s_Context->RootDomain);
		s_Context->RootDomain = nullptr;
	}

	
	void ScriptEngine::LoadAssemblyClasses(MonoAssembly* assembly)
	{
		MonoImage* image = mono_assembly_get_image(assembly);
		const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
		int32_t numTypes = mono_table_info_get_rows(typeDefinitionsTable);
		HZ_CORE_WARN("**************************************************************");
		HZ_CORE_WARN("Load Assembly Class:");
		for (int32_t i = 0; i < numTypes; i++)
		{
			uint32_t cols[MONO_TYPEDEF_SIZE];
			mono_metadata_decode_row(typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE);

			const char* nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);
			const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);
			MonoClass* klass = mono_class_from_name(image, nameSpace, name);
			MonoClass* entityklass = mono_class_from_name(image, "Kans", "Entity");
			std::string fullname;
			if (strlen(nameSpace) != 0)
			{
				fullname = fmt::format("{0}.{1}", nameSpace, name);
			}
			else
			{
				fullname = name;
			}
			
			bool isEntityClass = mono_class_is_subclass_of(klass, entityklass,false);
			if (isEntityClass)
			{

				s_Context->EntityClasses[fullname] = CreateRef<ScriptClass>(nameSpace, name);
			}

			std::string logstr= fmt::format("Assembly class: {:<32}  cacheed: {:>8}", fullname, isEntityClass);
			HZ_CORE_TRACE("{}", logstr);
			//HZ_CORE_TRACE("Assembly class: {0}  cacheed {1}", fullname, isEntityClass);
			//spdlog::info(spdlog::fmt_lib::format(std::locale("en_US.UTF-8"), "Multi threaded: {:L} threads, {:L} messages", threads, iters))
		}
		HZ_CORE_WARN("**************************************************************");
	}

	MonoObject* ScriptEngine::InstantiateClass(MonoClass* monoclass)
	{
		MonoObject* instance = mono_object_new(s_Context->AppDomain, monoclass);
		mono_runtime_object_init(instance);	// Call the parameterless (default) constructor
		return instance;
	}

	MonoImage* ScriptEngine::GetCoreAssemblyImage()
	{
		HZ_CORE_ASSERT(s_Context->CoreAssemblyImage, "Script Engine is not Inited");
		return s_Context->CoreAssemblyImage;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Script Class
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ScriptClass::ScriptClass(const std::string& classNamespace, const std::string& className)
		:m_ClassNamespace(classNamespace), m_ClassName(className)
	{
		m_MonoClass = mono_class_from_name(s_Context->CoreAssemblyImage, m_ClassNamespace.c_str(), m_ClassName.c_str());
	}

	MonoObject* ScriptClass::Instantiate()
	{
		return ScriptEngine::InstantiateClass(m_MonoClass);
	}

	MonoMethod* ScriptClass::GetMethod( const std::string& name, uint32_t paramCount)
	{
		return mono_class_get_method_from_name(m_MonoClass, name.c_str(), paramCount);
	}

	MonoObject* ScriptClass::InvokeMethod(MonoObject* instance, MonoMethod* method, void** parameter /*=nullptr*/)
	{
		return mono_runtime_invoke(method, instance, parameter, nullptr);
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Script Instance
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	ScriptInstance::ScriptInstance(Ref<ScriptClass> scriptClass, Entity entity)
		:m_ScriptClass(scriptClass)
	{
		m_Instance = m_ScriptClass->Instantiate();

		m_Constructor = s_Context->EntityClass.GetMethod(".ctor", 1);
		m_OnCreateMehod = m_ScriptClass->GetMethod("OnCreate", 0);
		m_OnUpdateMehod = m_ScriptClass->GetMethod("OnUpdate", 1);

		uint64_t uuid = entity.GetUUID();
		void* param = &uuid;
		m_ScriptClass->InvokeMethod(m_Instance, m_Constructor, &param);
	}

	void ScriptInstance::InvokeOnUpdate(float ts)
	{
		void* param = &ts;
		m_ScriptClass->InvokeMethod(m_Instance, m_OnUpdateMehod, &param);
	}

	void ScriptInstance::InvekeOnCreate()
	{
		m_ScriptClass->InvokeMethod(m_Instance, m_OnCreateMehod, nullptr);
	}

}