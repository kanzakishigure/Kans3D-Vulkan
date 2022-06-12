#pragma once
#include "Kans3D/Core/Base.h"
#ifdef  HZ_PLATFORM_WINDOWS

extern Kans::Application* Kans::createApplication();
//��Ϊ���ǲ�֪��ʵ�ʵ�Ӧ�����ͣ��������Ƕ���һ���ܷ�����ָ��ĺ��������������ط�ʵ�������ڴ����������£����ǻ����û��˽���ʵ����

int main(int argc,char** argv)
{
	Kans::Log::Init();

	HZ_PROFILE_BEGIN_SESSION("startup","ProfileSpecication/startup-profile.json");
	auto app = Kans::createApplication();
	HZ_PROFILE_END_SESSION();

	//����ʹ��void*��thisָ�룬�Լ���ʶ�����캯����д���Ͻ���
	//auto x = app->getthisPoint();
	HZ_PROFILE_BEGIN_SESSION("runtime", "ProfileSpecication/runtime-profile.json");
	app->run();
	HZ_PROFILE_END_SESSION();

	HZ_PROFILE_BEGIN_SESSION("shutdown", "ProfileSpecication/shutdown-profile.json");
	delete app;
	HZ_PROFILE_END_SESSION();
}

#endif //  HZ_PLATFORM_WINDOWS