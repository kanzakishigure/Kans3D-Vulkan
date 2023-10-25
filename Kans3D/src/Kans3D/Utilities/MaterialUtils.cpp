#include "kspch.h"
#include "MaterialUtils.h"
namespace Kans::Utils
{
	void MaterialUtils::InitMaterial(const Ref<MaterialTable> materialTable)
	{

		int matCount = materialTable->GetMaterialCount();

		for (size_t i = 0; i < matCount; i++)
		{
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("_UseRamp",true);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_RampShadowRange", 0.540f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_RampAOShadowRange", 0.680f);

			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_ShadowIntensity", 0.920f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_BrightIntensity", 0.960f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_AOIntensity", 0.05f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_CharacterIntensity", 1.1240f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_RampIntensity", 1.0f);

			
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_SpecularIntensity_1", 1.0f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_SpecularIntensity_2", 1.0f);
			materialTable->GetMaterialAsset(i)->GetMaterial()->Set("U_SpecularIntensity_3", 1.0f);
			

			
		}


	}
}
