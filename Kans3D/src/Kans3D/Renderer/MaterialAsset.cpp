#include "hzpch.h"
#include "MaterialAsset.h"
#include "Kans3D/Renderer/Renderer.h"
namespace Kans
{

	MaterialTable::MaterialTable(uint32_t materialCount /*= 1*/)
		:m_MaterialCount(materialCount)
	{
		
	}

	void MaterialTable::SetMaterial(uint32_t index, const Ref<MaterialAsset> material)
	{
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
		m_MaterialTable[index] = material;
	}

	bool MaterialTable::HasMaterial(uint32_t index)
	{

		return m_MaterialTable.find(index) != m_MaterialTable.end();
	}

	const Kans::Ref<Kans::MaterialAsset>& MaterialTable::GetMaterialAsset(uint32_t index)
	{

			HZ_ASSERT(HasMaterial(index),"don't have material")
			return m_MaterialTable[index];

	}

	void MaterialTable::CleanMaterial(uint32_t index)
	{
		HZ_ASSERT(HasMaterial(index),"Can't clean Material,Material didn't exist ")
		m_MaterialTable.erase(index);
		if (index >= m_MaterialCount)
			m_MaterialCount = index + 1;
	}

	void MaterialTable::Clean()
	{
		//this is not a thread safe method
		m_MaterialTable.clear();
	}

	MaterialAsset::MaterialAsset()
	{
		Ref<Shader> shader = Shader::Create("F:/Kans3D/Kans/KansEditor/assets/shaders/StaticMeshShader.glsl");
		m_Material = Material::Create(shader);
		//set value
		SetShininess(0.0f);
		//map
		SetDiffuseMap(Renderer::GetWhiteTexture());
		SetSpecularMap(Renderer::GetWhiteTexture());
		SetEmissionMap(Renderer::GetWhiteTexture());
		SetNormalMap(Renderer::GetWhiteTexture());
		
		
	}

	MaterialAsset::MaterialAsset(const Ref<Material> material)
	{
		m_Material = material;
	}

	MaterialAsset::~MaterialAsset()
	{

	}

	void MaterialAsset::SetDiffuse(const glm::vec3& value)
	{
		m_Material->Set(s_DiffuseUniform, value);
	}

	void MaterialAsset::SetSpecular(const glm::vec3& value)
	{
		m_Material->Set(s_SpecularUniform, value);
	}

	void MaterialAsset::SetEmission(const glm::vec3& value)
	{
		m_Material->Set(s_EmissionUniform, value);
	}

	void MaterialAsset::SetShininess(float value)
	{
		m_Material->Set(s_ShininessUniform, value);
	}

	void MaterialAsset::SetDiffuseMap(Ref<Texture2D> diffusemap)
	{
		m_Material->Set(s_DiffuseMapUniform, diffusemap);
	}

	void MaterialAsset::SetSpecularMap(Ref<Texture2D> specularmap)
	{
		m_Material->Set(s_SpecularMapUniform, specularmap);
	}

	void MaterialAsset::SetEmissionMap(Ref<Texture2D> emissionmap)
	{
		m_Material->Set(s_EmissionUniform, emissionmap);
	}


	void MaterialAsset::SetNormalMap(Ref<Texture2D> normalmap)
	{
		m_Material->Set(s_NormalMapUnifrom, normalmap);
	}

}