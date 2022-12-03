#pragma once
#include <FileSystem>
#include "Kans3D/Scene/Scene.h"

extern "C" {

	typedef struct _MonoObject MonoObject;
	typedef struct _MonoClass MonoClass;
	typedef struct _MonoMethod MonoMethod;
	typedef struct _MonoAssembly MonoAssembly;
	typedef struct _MonoImage MonoImage;
}

namespace Kans
{
	
	class Scene;
	class ScriptEngine
	{
	public:
		static void Init();
		static void ShutDown();

		static void LoadAssembly(const std::filesystem::path& filepath);
		static bool EntityClassExists(const std::string& className);
		static void OnRuntimeStart(Scene* scene);
		static void OnRuntimeEnd();

		static void  OnCreateEntity(Entity entity);
		static void  OnUpdateEntity(Entity entity, TimeStep ts);

		static Scene* GetSceneContext();
		static MonoImage* GetCoreAssemblyImage();
	
	private:
		static void InitMono();
		static void ShutDownMono();
		static void LoadAssemblyClasses(MonoAssembly* assembly);

		static MonoObject* InstantiateClass(MonoClass* monoclass);
		

		friend class ScriptClass;
	};

	class ScriptClass
	{
	public:
		ScriptClass() = default;
		ScriptClass(const std::string& classNamespace, const std::string& className);

		MonoObject* Instantiate();
		MonoMethod* GetMethod(const std::string& name, uint32_t paramCount);
		MonoObject* InvokeMethod(MonoObject* instance, MonoMethod* method, void** parameter  = nullptr);


	private:
		std::string m_ClassNamespace;
		std::string m_ClassName;

		MonoClass* m_MonoClass = nullptr;;

	};
	class ScriptInstance
	{
	public:
		ScriptInstance(Ref<ScriptClass> scriptClass,Entity entity);
		void InvokeOnUpdate(float ts);
		void InvekeOnCreate();
	private:
		Ref<ScriptClass> m_ScriptClass;

		MonoObject* m_Instance =nullptr;
		MonoMethod* m_OnCreateMehod = nullptr;
		MonoMethod* m_OnUpdateMehod = nullptr;
		MonoMethod* m_Constructor = nullptr;
	};
}