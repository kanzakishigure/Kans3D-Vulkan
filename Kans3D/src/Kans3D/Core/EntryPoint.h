#pragma once
#include "Kans3D/Core/Base/Base.h"
#ifdef  PLATFORM_WINDOWS

extern Kans::Application* Kans::createApplication(int argc, char** argv);
//因为我们不知道实际的应用类型，所以我们定义一个能返回类指针的函数，并在其他地方实现它，在此引擎的情况下，我们会在用户端将其实例化


namespace Kans
{
	int Main(int argc, char** argv)
	{
		Log::Init();
		
		PROFILE_BEGIN_SESSION("startup", "ProfileSpecication/startup-profile.json");
		Application* app = Kans::createApplication(argc,argv);
		PROFILE_END_SESSION();

		//谨慎使用void*和this指针，以及认识到构造函数编写的严谨性
		//auto x = app->getthisPoint();
		PROFILE_BEGIN_SESSION("runtime", "ProfileSpecication/runtime-profile.json");
		app->run();
		PROFILE_END_SESSION();

		PROFILE_BEGIN_SESSION("shutdown", "ProfileSpecication/shutdown-profile.json");
		delete app;
		PROFILE_END_SESSION();

		Log::ShutDown();
		return 0;
	}
}



#ifdef KS_DIST
INT APIENTRY WinMain(
	HINSTANCE hINSTANCE,
	HINSTANCE hPrevINSTANCE,
	PSTR lpCmdLine,
	INT nCmdshow)
{
	return Kans::Main(__argc, __argv);
}
#else 
int main(int argc, char** argv)
{
	return Kans::Main(argc, argv);
}
#endif	

#endif 