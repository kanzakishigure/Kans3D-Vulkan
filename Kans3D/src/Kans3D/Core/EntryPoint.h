#pragma once
#include "Kans3D/Core/Base/Base.h"

extern Kans::Application* Kans::createApplication(int argc, char** argv);

namespace Kans
{
	int Main(int argc, char** argv)
	{
		Log::Init();
		
		PROFILE_BEGIN_SESSION("startup", "ProfileSpecication/startup-profile.json");
		Application* app = Kans::createApplication(argc,argv);
		PROFILE_END_SESSION();

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

#ifdef PLATFORM_WINDOWS

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
#ifdef PLATFORM_LINUX

int main(int argc, char** argv)
{
	return Kans::Main(argc, argv);
}

#endif
