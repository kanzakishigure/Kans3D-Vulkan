#include "kspch.h"
#include "MaterialUtils.h"
namespace Kans::Utils
{
	void MaterialUtils::InitMaterial(const Ref<MaterialTable> materialTable)
	{

		int matCount = materialTable->GetMaterialCount();
#if 0
		for (uint32_t i = 0; i < matCount; i++)
		{
			auto& mat = materialTable->GetMaterialAsset(i)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
		}
#endif


		//tone shader test data

#if 1
		{

		}
		//Face
		{
			auto& mat = materialTable->GetMaterialAsset(0)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Hair
		{
			auto& mat = materialTable->GetMaterialAsset(1)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Clothes
		{
			auto& mat = materialTable->GetMaterialAsset(2)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}
		//Skin
		{
			auto& mat = materialTable->GetMaterialAsset(3)->GetMaterial();
			mat->Set("U_ShadowMultColor", glm::vec4(1.0));
			mat->Set("U_DarkShadowMultColor", glm::vec4(1.0));
			mat->Set("U_ShadowArea", 0.5f);
			mat->Set("U_DarkShadowArea", 0.5f);
			mat->Set("U_FixDarkShadow", 0.5f);
			mat->Set("U_ShadowSmooth", 0.5f);
			mat->Set("U_DarkShadowSmooth", 0.5f);
			mat->Set("U_RampShadowRange", 0.5f);
			mat->Set("U_UseRamp", true);
			mat->Set("U_RampSkin", glm::vec2{ 0.95 });
			mat->Set("U_RampTights", glm::vec2{ 0.45 });
			mat->Set("U_RampMetal", glm::vec2{ 0.35 });
			mat->Set("U_RampSoft", glm::vec2{ 0.15 });
			mat->Set("U_RampSilk", glm::vec2{ 0.05 });
		}


#endif 
	}
}
