#pragma once
#include "Kans3D/Core/Base.h"
#ifdef  HZ_PLATFORM_WINDOWS

extern Kans::Application* Kans::createApplication();
//因为我们不知道实际的应用类型，所以我们定义一个能返回类指针的函数，并在其他地方实现它，在此引擎的情况下，我们会在用户端将其实例化

int main(int argc,char** argv)
{
	Kans::Log::Init();

	HZ_PROFILE_BEGIN_SESSION("startup","ProfileSpecication/startup-profile.json");
	auto app = Kans::createApplication();
	HZ_PROFILE_END_SESSION();

	//谨慎使用void*和this指针，以及认识到构造函数编写的严谨性
	//auto x = app->getthisPoint();
	HZ_PROFILE_BEGIN_SESSION("runtime", "ProfileSpecication/runtime-profile.json");
	app->run();
	HZ_PROFILE_END_SESSION();

	HZ_PROFILE_BEGIN_SESSION("shutdown", "ProfileSpecication/shutdown-profile.json");
	delete app;
	HZ_PROFILE_END_SESSION();
}

#endif //  HZ_PLATFORM_WINDOWS