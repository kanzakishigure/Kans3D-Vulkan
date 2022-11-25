#include "kspch.h"
#include "Renderer2D.h"

#include "VertexArray.h"
#include "Shader.h"
#include "Platform/OpenGL/OpenGLShader.h"
#include "RenderCommand.h"

#include <glm/ext/matrix_transform.hpp>

namespace Kans {

	
	struct  QuadVertex
	{
		glm::vec3 Position;
		glm::vec4 Color;
		glm::vec2 TexCroods;
		glm::float32_t TexIndex = 0;
		glm::float32_t TilingFactor = 0;

	};

	struct  Renderer2DData
	{
		static const uint32_t MaxQuads = 5000;
		static const uint32_t MaxVerteices = MaxQuads * 4;
		static const uint32_t MaxIndices = MaxQuads * 6;
		static const uint32_t MaxTextureSlots = 32;
		//此处为调用一个drawcall时由我们进行设置的属性

		Renderer2D::Statistics Stats;


		Ref<VertexArray> QuadVertexArray;
		Ref<VertexBuffer> QuadVertexBuffer;
		Ref<Shader> TextureShader;
		ShaderLibrary s_ShaderLibrary;
		Ref<Texture2D> WhiteTexture;
		std::array<glm::vec4, 4> QuadVertexPosition;


		QuadVertex* QuadVertexBufferBase = nullptr;
		QuadVertex* QuadVertexBufferPtr = nullptr;

		uint32_t QuadIndexCount = 0;

		std::array<Ref<Texture2D>, MaxTextureSlots> TextureSlots;
		uint32_t TextureSlotIndex = 1;
	};
	//并未进行跨程序集的函数调用，没有必要使用new申请堆内存
	static Renderer2DData s_Data;


	void Renderer2D::Init()
	{
		HZ_PROFILE_FUCTION();


		//vertex array
		s_Data.QuadVertexArray = VertexArray::Create();
		//动态申请内存用户存储数据
		s_Data.QuadVertexBufferBase = new QuadVertex[s_Data.MaxVerteices];
		//vertex buffer
		s_Data.QuadVertexBuffer = VertexBuffer::Create(s_Data.MaxVerteices * sizeof(QuadVertex));
		s_Data.QuadVertexBuffer->Bind();
		{
			BufferLayout layout = {
				{ShaderDataType::Float3,"a_Position"},
				{ShaderDataType::Float4,"a_Color"},
				{ShaderDataType::Float2,"a_TextureCroods"},
				{ShaderDataType::Float,"a_TexIndex"},
				{ShaderDataType::Float,"a_TilingFactor"}
			};
			s_Data.QuadVertexBuffer->SetBufferLayout(layout);
		}
		s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);
		//index buffer
		uint32_t* quadIndex = new uint32_t[s_Data.MaxIndices];
		
		uint32_t offset = 0;
		for (uint32_t i =0;i<s_Data.MaxIndices;i+=6)
		{
			quadIndex[i + 0] = offset + 0;
			quadIndex[i + 1] = offset + 1;
			quadIndex[i + 2] = offset + 2;
			quadIndex[i + 3] = offset + 2;
			quadIndex[i + 4] = offset + 3;
			quadIndex[i + 5] = offset + 0;

			offset += 4;

		}
		Ref<IndexBuffer> indexBuffer = IndexBuffer::Create(quadIndex, s_Data.MaxIndices);
		s_Data.QuadVertexArray->SetIndexBuffer(indexBuffer);

		delete[] quadIndex;
		//此处没有考虑到线程安全性，如果别渲染的线程上仍然有对该资源的引用，那么此处进行的delete操作就会导致该引用指向一个空资源

		//Shader Program
		s_Data.s_ShaderLibrary.Add(Kans::Shader::Create("Resources/Shaders/TextureShader.glsl"));
		
		s_Data.TextureShader = s_Data.s_ShaderLibrary.Get("TextureShader");
		
		//Texture2D
		s_Data.WhiteTexture = Texture2D::Create(1, 1);
		uint32_t whiteTextureData = 0xffffffff;
		s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(whiteTextureData));

		int32_t samplers[s_Data.MaxTextureSlots];
		for (uint32_t i = 0; i < s_Data.MaxTextureSlots; i++)
		{
			samplers[i] = (int)i;
		}
		s_Data.TextureShader->Bind();
		s_Data.TextureShader->SetIntArray("U_TextureSample", s_Data.MaxTextureSlots, samplers);
		s_Data.TextureSlots[0] = s_Data.WhiteTexture;
		s_Data.QuadVertexPosition[0] = {-0.5f,-0.5,0.0 ,1.0 };
		s_Data.QuadVertexPosition[1] = { 0.5f,-0.5,0.0 ,1.0 };
		s_Data.QuadVertexPosition[2] = { 0.5f, 0.5,0.0 ,1.0 };
		s_Data.QuadVertexPosition[3] = {-0.5f, 0.5,0.0 ,1.0 };
		
	}

	void Renderer2D::BeginScene(const OrthographicCamera& camera)
	{
		HZ_PROFILE_FUCTION();

		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
		s_Data.QuadIndexCount = 0;
		s_Data.TextureSlotIndex = 1;
		s_Data.TextureShader->Bind();
		s_Data.TextureShader->SetMat4("U_ViewProjection", camera.GetViewProjectionMatrtix());
		
		//TODO
		//when ever entity is delelte we show clean the batch buffer;
	}

	void Renderer2D::BeginScene(const Camera& camera, const glm::mat4& transform)
	{
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
		s_Data.QuadIndexCount = 0;
		s_Data.TextureSlotIndex = 1;
		s_Data.TextureShader->Bind();
		glm::mat4 viewprojection = camera.GetProjectMatrix() * glm::inverse(transform);
		s_Data.TextureShader->SetMat4("U_ViewProjection", viewprojection);
	}

	void Renderer2D::EndScene()
	{
		HZ_PROFILE_FUCTION();
		
		//采用了batchrendering之后，不在从每一次drawquad中调用绘制命令，而是在每一次endsecene中调用drawcall
		s_Data.TextureShader->Bind();
		s_Data.QuadVertexArray->Bind();
		uint32_t  datasize = (uint8_t*)s_Data.QuadVertexBufferPtr - (uint8_t*)s_Data.QuadVertexBufferBase;
		s_Data.QuadVertexBuffer->SetData(s_Data.QuadVertexBufferBase,datasize);

		Flush();
	}

	void Renderer2D::Shutdown()
	{
		HZ_PROFILE_FUCTION();
		delete[] s_Data.QuadVertexBufferBase;
		HZ_WARN(" render2d quadsbuffer memary is free");
	}

	void Renderer2D::Flush()
	{
		if (s_Data.QuadIndexCount == 0)
			return; // Nothing to draw

		for (uint32_t i = 0; i < s_Data.TextureSlotIndex; i++)
		{
			s_Data.TextureSlots[i]->Bind(i);
		}
		
		RenderCommand::DrawIndexed(s_Data.QuadVertexArray,s_Data.QuadIndexCount);
		s_Data.Stats.DrawCalls++;
	}

	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& tintcolor)
	{
		
		DrawQuad({ position.x,position.y, 0.0f}, size, tintcolor);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& tintcolor)
	{
		
			glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), position) *
			glm::scale(glm::mat4(1.0f), { size.x,size.y,1.0f });
			DrawQuad(transform, tintcolor);

	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& tintcolor)
	{
		HZ_PROFILE_FUCTION();

		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
		{
			FlushAndReset();
		}

		//bind whitetexture
		constexpr int quadsvertexcount = 4;
		constexpr float TilingFactor = 1.0f;
		constexpr float textureindex = 0.0f;
		constexpr glm::vec2 texturecroods[] = { { 0.0f,0.0f }, { 1.0f,0.0f } ,{ 1.0f,1.0f },{ 0.0f,1.0f } };		
		size_t i = 0;
		for (; i < quadsvertexcount; i++)
		{

			s_Data.QuadVertexBufferPtr->Color = tintcolor;
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[i];
			s_Data.QuadVertexBufferPtr->TexCroods = texturecroods[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
			s_Data.QuadVertexBufferPtr->TilingFactor = TilingFactor;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}


	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>&Texture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor )
	{
		DrawQuad({ position.x,position.y,0.0f }, size, Texture2D, tintcolor);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size,  const Ref<Texture2D>&Texture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor)
	{
		
	
			glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), position) *
			glm::scale(glm::mat4(1.0f), { size.x,size.y,1.0f });

			DrawQuad(transform, Texture2D, tintcolor, tilingFactor);

	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor /*= 1.0f*/)
	{
		HZ_PROFILE_FUCTION();
		constexpr int quadsvertexcount = 4;
		constexpr glm::vec2 croodsindex[4] = { { 0.0f,0.0f }, { 1.0f,0.0f } ,{ 1.0f,1.0f },{ 0.0f,1.0f } };
		float textureindex = 0.0f;


		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
		{
			FlushAndReset();
		}

		//为了保证相同的texture不会重复占用贴图slot，进行查询优化
		for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
		{
			if (*s_Data.TextureSlots[i].get() == *(Texture2D.get()))
			{
				textureindex = (float)i;
				break;
			}
		}
		if (textureindex == 0.0f)
		{
			textureindex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = Texture2D;
			s_Data.TextureSlotIndex++;
		}

		
		//默认情况下使用TexIndex的索引为1指向WhiteTexture

		for (size_t i = 0; i < quadsvertexcount; i++)
		{

			s_Data.QuadVertexBufferPtr->Color = tintcolor;
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[i];
			s_Data.QuadVertexBufferPtr->TexCroods = croodsindex[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;

	}


	void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor /*= 1.0f*/)
	{
		DrawQuad({ position.x,position.y,0.0f }, size, subtexture2D, tintcolor);
	}

	void Renderer2D::DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor /*= 1.0f*/)
	{

			glm::mat4 transform =
			glm::translate(glm::mat4(1.0f), position) *
			glm::scale(glm::mat4(1.0f), { size.x,size.y,1.0f });
			DrawQuad(transform, subtexture2D, tintcolor,tilingFactor);
		
	}

	void Renderer2D::DrawQuad(const glm::mat4& transform, const Ref<SubTexture2D>& subtexture2D, const glm::vec4& tintcolor /*= glm::vec4(1.0f)*/, float tilingFactor /*= 1.0f*/)
	{


		HZ_PROFILE_FUCTION();
		constexpr int quadsvertexcount = 4;
		const glm::vec2* texturecroods = subtexture2D->GetCroods();
		float textureindex = 0.0f;
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
		{
			FlushAndReset();
		}

		//为了保证相同的texture不会重复占用贴图slot，进行查询优化
		for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
		{
			if (*s_Data.TextureSlots[i].get() == *(subtexture2D->GetTexture().get()))
			{
				textureindex = (float)i;
				break;
			}
		}
		if (textureindex == 0.0f)
		{
			textureindex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = subtexture2D->GetTexture();
			s_Data.TextureSlotIndex++;
		}

		
		//默认情况下使用TexIndex的索引为1指向WhiteTexture

		for (size_t i = 0; i < quadsvertexcount; i++)
		{

			s_Data.QuadVertexBufferPtr->Color = tintcolor;
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[i];
			s_Data.QuadVertexBufferPtr->TexCroods = texturecroods[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
			s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
			s_Data.QuadVertexBufferPtr++;
		}

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;
	}


	void Renderer2D::DrawRotateQuad(const glm::vec2& position, float roration, const glm::vec2& size, const glm::vec4& tintcolor)
	{
		DrawRotateQuad({ position.x,position.y,1.0f }, roration, size, tintcolor);
	}

	void Renderer2D::DrawRotateQuad(const glm::vec3& position, float roration, const glm::vec2& size, const glm::vec4& tintcolor)
	{
		HZ_PROFILE_FUCTION();
		constexpr int quadsvertexcount = 4;
		constexpr float textureindex = 0.0f;
		constexpr float TilingFactor = 1.0f;

		constexpr glm::vec2 texturecroods[4] = { { 0.0f,0.0f }, { 1.0f,0.0f } ,{ 1.0f,1.0f },{ 0.0f,1.0f } } ;

		glm::mat4 transform = 
			glm::translate(glm::mat4(1.0f), position) * 
			glm::rotate(glm::mat4(1.0f),roration, glm::vec3(0.0f, 0.0f, 1.0f)) * 
			glm::scale(glm::mat4(1.0f), { size.x,size.y,1.0f });


		for (size_t i = 0; i< quadsvertexcount;i++)
		{

			s_Data.QuadVertexBufferPtr->Color = tintcolor;
			s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[i];
			s_Data.QuadVertexBufferPtr->TexCroods = texturecroods[i];
			s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
			s_Data.QuadVertexBufferPtr->TilingFactor = TilingFactor;
			s_Data.QuadVertexBufferPtr++;
		}

		


		s_Data.QuadIndexCount += 6;

	}

	void Renderer2D::DrawRotateQuad(const glm::vec2& position, float roration, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor, float tilingFactor)
	{
		DrawRotateQuad({ position.x,position.y,1.0f }, roration, size, Texture2D, tintcolor);
	}

	void Renderer2D::DrawRotateQuad(const glm::vec3& position, float roration, const glm::vec2& size, const Ref<Texture2D>& Texture2D, const glm::vec4& tintcolor, float tilingFactor)
	{
		HZ_PROFILE_FUCTION();
		if (s_Data.QuadIndexCount >= Renderer2DData::MaxIndices)
		{
			FlushAndReset();
		}
		float textureindex = 0.0f;
		glm::mat4 transform = 
			glm::translate(glm::mat4(1.0f), position) *
			glm::rotate(glm::mat4(1.0f), roration, glm::vec3(0.0f, 0.0f, 1.0f)) *
			glm::scale(glm::mat4(1.0f), { size.x,size.y,1.0f });

		for (uint32_t i = 1; i < s_Data.TextureSlotIndex; i++)
		{
			if (*s_Data.TextureSlots[i].get() == *(Texture2D.get()))
			{
				textureindex = (float)i;
				break;
			}
		}
		if (textureindex == 0.0f)
		{
			textureindex = (float)s_Data.TextureSlotIndex;
			s_Data.TextureSlots[s_Data.TextureSlotIndex] = Texture2D;
			s_Data.TextureSlotIndex++;
		}
		//默认情况下使用TexIndex的索引为1指向WhiteTexture
		s_Data.QuadVertexBufferPtr->Color = tintcolor;
		s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[0];
		s_Data.QuadVertexBufferPtr->TexCroods = { 0.0f,0.0f };
		s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
		s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = tintcolor;
		s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[1];
		s_Data.QuadVertexBufferPtr->TexCroods = { 1.0f,0.0f };
		s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
		s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = tintcolor;
		s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[2];
		s_Data.QuadVertexBufferPtr->TexCroods = { 1.0f,1.0f };
		s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
		s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadVertexBufferPtr->Color = tintcolor;
		s_Data.QuadVertexBufferPtr->Position = transform * s_Data.QuadVertexPosition[3];
		s_Data.QuadVertexBufferPtr->TexCroods = { 0.0f,1.0f };
		s_Data.QuadVertexBufferPtr->TexIndex = textureindex;
		s_Data.QuadVertexBufferPtr->TilingFactor = tilingFactor;
		s_Data.QuadVertexBufferPtr++;

		s_Data.QuadIndexCount += 6;
		s_Data.Stats.QuadCount++;

	}

	void Renderer2D::ResetStats()
	{
		memset(&s_Data.Stats, 0, sizeof(s_Data.Stats));
	}

	Kans::Renderer2D::Statistics& Renderer2D::GetStats()
	{
		return s_Data.Stats;
	}

	void Renderer2D::FlushAndReset()
	{
		EndScene();
		s_Data.QuadVertexBufferPtr = s_Data.QuadVertexBufferBase;
		s_Data.QuadIndexCount = 0;
		s_Data.TextureSlotIndex = 1;
	}

}