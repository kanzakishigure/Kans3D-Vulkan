#include "hzpch.h"
#include "Kans3D/Renderer/SceneRenderer.h"
#include "Kans3D/Renderer/Renderer.h"
//test
#include "imgui.h"
namespace Kans 
{

	SceneRenderer::SceneRenderer(Ref<Scene> scene, SceneRendererSpecification spec)
		:m_Scene(scene),m_Specification(spec)
	{
		Init();
	}

	void SceneRenderer::Init()
	{
		//Init the shader 
		//Set the shader buffer
	}

	void SceneRenderer::BeginScene(SceneInfo info)
	{
		m_Active = true;
		m_SceneInfo = info;
	}

	void SceneRenderer::EndScene()
	{
		m_Active = false;
	}

	void SceneRenderer::SetScene(Ref<Scene> scene)
	{
		HZ_ASSERT(scene, "Handel a nullptr scene");
		m_Scene = scene;
	}

	void SceneRenderer::SubmitStaticMesh(Ref<StaticMesh> mesh, Ref<MaterialTable> material, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = mesh->GetMeshSource()->GetMeshShader();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix()*m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto VA = VAOs[mesh];
			VA->Bind();
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();

			//SetLight
			{
				//Dir
				subMtl->Set("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				subMtl->Set("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				subMtl->Set("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				subMtl->Set("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//Point
				subMtl->Set("pointLight.Position", m_SceneInfo.pointLight.Position);
				subMtl->Set("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				subMtl->Set("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				subMtl->Set("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				subMtl->Set("U_ViewPos", m_SceneInfo.sceneCamera.Position);
			}


			subMtl->Invalidate();
			RenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshStencil(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("StencilShader");
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto VA = VAOs[mesh];
			VA->Bind();
			RenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshPostEffect(Ref<StaticMesh> mesh, Ref<Texture2D> attachment, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("PostShader");
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);
		attachment->Bind();
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto VA = VAOs[mesh];
			VA->Bind();
			RenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshOutLine(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("OutLineShader");
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);
		auto& material = mesh->GetMaterials();

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			subMtl->SetShader(shader);
			subMtl->Invalidate();
			auto& VA = VAOs[mesh];
			VA->Bind();
			RenderCommand::DrawIndexed(VA);
		}

	}

	void SceneRenderer::SubmitStaticMeshToneshading(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("ToneShader");
		auto& material = mesh->GetMaterials();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);


		{
			//SetLight
			{
				//Dir
				shader->SetFloat3("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->SetFloat3("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->SetFloat3("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->SetFloat3("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//Point
				shader->SetFloat3("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->SetFloat3("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->SetFloat3("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->SetFloat3("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->SetFloat3("U_ViewPos", m_SceneInfo.sceneCamera.Position);

				

			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto VA = VAOs[mesh];
			VA->Bind();
			subMtl->SetShader(shader);
			subMtl->Invalidate();
			RenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitStaticMeshDebugNormal(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& shader = Renderer::GetShaderLibrary()->Get("DebugNormalShader");
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		shader->Bind();
		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);

		auto& material = mesh->GetMaterials();

		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto VA = VAOs[mesh];
			VA->Bind();
			subMtl->SetShader(shader);
			subMtl->Invalidate();
			RenderCommand::DrawIndexed(VA);
		}

	}

	void SceneRenderer::SubmitStaticMeshDebug(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("DebugShader");;
		auto& material = mesh->GetMaterials();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);

		
		{
			//SetLight
			{
				//Dir
				shader->SetFloat3("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->SetFloat3("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->SetFloat3("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->SetFloat3("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//Point
				shader->SetFloat3("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->SetFloat3("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->SetFloat3("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->SetFloat3("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->SetFloat3("U_ViewPos", m_SceneInfo.sceneCamera.Position);
			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto VA = VAOs[mesh];
			VA->Bind();
			subMtl->SetShader(shader);
			subMtl->Invalidate();
			RenderCommand::DrawIndexed(VA);
		}
	}

	void SceneRenderer::SubmitToneCharactorShader(Ref<StaticMesh> mesh, glm::mat4 transform)
	{
		auto& VAOs = mesh->GetMeshSource()->GetVertexArray();
		auto& shader = Renderer::GetShaderLibrary()->Get("ToneCharactorShader");;
		auto& material = mesh->GetMaterials();
		shader->Bind();

		glm::mat4 viewprojection = m_SceneInfo.sceneCamera.camera.GetProjectMatrix() * m_SceneInfo.sceneCamera.viewMatrix;
		shader->SetMat4("U_ViewProjection", viewprojection);
		shader->SetMat4("U_Transform", transform);


		{
			//SetLight
			{
				//Dir
				shader->SetFloat3("dirLight.LightDir", m_SceneInfo.dirLight.Dirction);
				shader->SetFloat3("dirLight.Ambient_Intensity", m_SceneInfo.dirLight.Ambient_Intensity);
				shader->SetFloat3("dirLight.Diffuse_Intensity", m_SceneInfo.dirLight.Diffuse_Intensity);
				shader->SetFloat3("dirLight.Specular_Intensity", m_SceneInfo.dirLight.Specular_Intensity);

				//Point
				shader->SetFloat3("pointLight.Position", m_SceneInfo.pointLight.Position);
				shader->SetFloat3("pointLight.Ambient_Intensity", m_SceneInfo.pointLight.Ambient_Intensity);
				shader->SetFloat3("pointLight.Diffuse_Intensity", m_SceneInfo.pointLight.Diffuse_Intensity);
				shader->SetFloat3("pointLight.Specular_Intensity", m_SceneInfo.pointLight.Specular_Intensity);
			}
			//SetScene
			{
				shader->SetFloat3("U_ViewPos", m_SceneInfo.sceneCamera.Position);
			}
		}
		for (auto& mesh : mesh->GetSubMesh())
		{
			auto subMtl = material->GetMaterialAsset(mesh)->GetMaterial();
			auto VA = VAOs[mesh];
			VA->Bind();
			//subMtl->SetShader(shader);
			subMtl->Invalidate();
			RenderCommand::DrawIndexed(VA);
		}
	}

}