#pragma once
//------core-----------------
#include "Kans3D/Core/Application.h"
#include "Kans3D/Core/Math/Math.h"
#include "Kans3D/Core/Log/Log.h"
#include "Kans3D/Core/TimeStep.h"
#include "Kans3D/Core/Layer.h"


#include "Kans3D/ImGui/ImGuiLayer.h"
//------Input---------------
#include "Kans3D/Input/KeyCodes.h"
#include "Kans3D/Input/MouseCodes.h"
#include "Kans3D/Input/Input.h"
//------Entity/Scene---------------
#include "Kans3D/Scene/Scene.h"
#include "Kans3D/Scene/Entity.h"
#include "Kans3D/Scene/Components.h"
#include "Kans3D/Scene/SceneSerializer.h"
//------UI---------------
#include "Kans3D/Utilities/UI/KansUI.h"
//------renderer---------------
#include "Kans3D/Renderer/RHI/FrameBuffer.h"
#include "Kans3D/Renderer/RHI/Buffer.h"
#include "Kans3D/Renderer/RHI/OpenGL/OpenGLRenderCommand.h "
#include "Kans3D/Renderer/RHI/OpenGL/VertexArray.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Renderer/Renderer2D.h"
#include "Kans3D/Renderer/SceneRenderer.h"

#include "Kans3D/Renderer/Shader.h"
#include "Kans3D/Renderer/Texture.h"
#include "Kans3D/Renderer/OrthographicCamera.h"
#include "Kans3D/Renderer/OrthographicCameraController.h"

#include "Kans3D/Renderer/Mesh.h"
#include "Kans3D/Renderer/Material.h"
//-----script---------------
#include "Kans3D/Scene/ScriptableEntity.h"

//------渲染效果---------------
#include "Kans3D/Physics/ParticleSystem.h"
//------辅助工具---------------
#include "Kans3D/Debug/Instrumentor.h"
#include "Kans3D/Utilities/KansUtils.h"
//------程序入口点--------------

//#include"Kans3D/Core/EntryPoint.h"
//------------------------------
