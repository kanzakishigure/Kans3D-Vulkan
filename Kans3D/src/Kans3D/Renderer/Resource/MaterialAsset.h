#pragma once
#include "Kans3D/Renderer/Resource/Texture.h"
#include "Kans3D/Renderer/Resource/Material.h"
#include "Kans3D/Asset/Asset.h"
namespace Kans
{
	enum class MaterialType
	{
		BlingPhongMaterial = 0, PBRMaterial
	};


	//BlingPhong Material
	//TODO use texture compress	
	// value
		static const std::string MaterialName = "material.";
		//漫反射项
		static const std::string s_DiffuseUniform = MaterialName + "U_Diffuse";
		//高光项			
		static const std::string s_SpecularUniform = MaterialName + "U_Specular";
		//自发光项		  
		static const std::string s_EmissionUniform = MaterialName + "U_Emission";
		//高光度			
		static const std::string s_ShininessUniform = MaterialName + "U_Shininess";
		
	//Texture	
			
			static const std::string s_NormalMapUnifrom		= MaterialName + "U_NormalTexture";   
			static const std::string s_EmissionMapUnifrom	= MaterialName + "U_EmissionTexture";
		// BlingPhong	  

			static const std::string s_DiffuseMapUniform	= MaterialName + "U_DiffuseTexture";
			static const std::string s_SpecularMapUniform	= MaterialName + "U_SpecularTexture";
			
		//PBR
			static const std::string s_AlbedoMapUnifrom		= MaterialName + "U_AlbedoTexture";
			static const std::string s_AOMapUnifrom			= MaterialName + "U_AOTexture";
			static const std::string s_RoughMapUnifrom		= MaterialName + "U_RoughTexture";
			static const std::string s_MetalMapUnifrom		= MaterialName + "U_MetalTexture";
			
		//ToneShader
			static const std::string s_ToneLightMapUnifrom	= MaterialName + "U_ToneLightMap";
			static const std::string s_ToneRampMapUnifrom	= MaterialName + "U_ToneRampMap";
	//Environment 		  
			static const std::string s_IrradianceMapUnifrom = "U_IrradianceMap";
			static const std::string s_PrefilterMapUnifrom	= "U_PrefilterMap";
			static const std::string s_BrdfLUTMapUnifrom	= "U_BrdfLUTMap";

	class MaterialAsset : public Asset
	{
	public: 
		MaterialAsset();
		MaterialAsset(const Ref<Material> material);
		MaterialAsset(const MaterialAsset& ) =default;
		~MaterialAsset();

		static const std::string& GetEmissionMapLocation() { return s_EmissionMapUnifrom; }
		static const std::string& GetNormalMapLocation() { return s_NormalMapUnifrom; }
		
		static const std::string& GetDiffuseMapLocation() { return s_DiffuseMapUniform; }
		static const std::string& GetSpecularMapLocation() { return s_SpecularMapUniform; }


		static const std::string& GetAlbedoMapLocation() { return s_AlbedoMapUnifrom; }
		static const std::string& GetAOMapLocation() { return s_AOMapUnifrom; }
		static const std::string& GetRoughMapLocation() { return s_RoughMapUnifrom; }
		static const std::string& GetMetalMapLocation() { return s_MetalMapUnifrom; }

		static const std::string& GetToneLightMapLocation() { return s_ToneLightMapUnifrom; }
		static const std::string& GetToneRampMapLocation() { return s_ToneRampMapUnifrom; }

		

		static const std::string& GetDiffuseLocation() { return s_DiffuseUniform; }
		static const std::string& GetSpecularLocation() { return s_SpecularUniform; }
		static const std::string& GetEmissionLocation() { return s_EmissionUniform; }
		static const std::string& GetShininessLocation() { return s_ShininessUniform; }


		static const std::string& GetIrradianceMapLocation() { return s_IrradianceMapUnifrom; }
		static const std::string& GetPrefilterMapLocation() { return s_PrefilterMapUnifrom; }
		static const std::string& GetBrdfLUTMapLocation() { return s_BrdfLUTMapUnifrom; }
		
	
		//BlingPhong
		//Set value
		void SetDiffuse(const glm::vec3& value);
		void SetSpecular(const glm::vec3& value);
		void SetEmission(const glm::vec3& value);
		void SetShininess(float value);
		//Set TexureMap
		void SetDiffuseMap(Ref<Texture2D> diffusemap);
		void SetSpecularMap(Ref<Texture2D> specularmap);
		void SetEmissionMap(Ref<Texture2D> emissionmap);
		void SetNormalMap(Ref<Texture2D> normalmap);

		void SetAlbedoMap(Ref<Texture2D> albedoMap);
		void SetAOMap(Ref<Texture2D> aoMap);
		void SetRoughMap(Ref<Texture2D> roughMap);
		void SetMetalMap(Ref<Texture2D> metalMap);

		void SetIrradianceMap(Ref<TextureCube> irradianceMap);
		void SetPrefilterMap(Ref<TextureCube> prefilterMap);
		void SetBrdfLUTMap(Ref<Texture2D> brdfLUTMap);

		Ref<Material> GetMaterial() const { return m_Material; }
	private:
		Ref<Material> m_Material;
		MaterialType m_MaterialType;
	};

	//MeshComponent will handle this;
	class MaterialTable : public RefCounter
	{
	public:
		MaterialTable(uint32_t materialCount = 1);
		~MaterialTable() = default;
		void SetMaterial(uint32_t index,const Ref<MaterialAsset> material);
		bool HasMaterial(uint32_t index);
		const Ref<MaterialAsset>& GetMaterialAsset(uint32_t index);
		void CleanMaterial(uint32_t index);
		void Clean();

		std::map<uint32_t, Ref<MaterialAsset>>& GetMaterials() { return m_MaterialTable; }
		const std::map<uint32_t, Ref<MaterialAsset>>& GetMaterials() const { return m_MaterialTable; }


		const uint32_t GetMaterialCount() { return m_MaterialCount; }
	private:
		uint32_t m_MaterialCount;
		std::map<uint32_t, Ref<MaterialAsset>> m_MaterialTable;

	};
}