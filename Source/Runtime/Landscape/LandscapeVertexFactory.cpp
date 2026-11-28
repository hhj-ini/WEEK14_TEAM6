#include "EnginePCH.h"
#include "Landscape/LandscapeVertexFactory.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"

void FLandscapeVertexFactory::Bind() const
{
	FVertexFactory::Bind();
	// VF_Landscape.hlsli의 LandscapeHeightmap(t17). 정점 셰이더만 읽는다
	RenderCommand::BindShaderResource(LandscapeHeightmapSlot, HeightmapTexture, EShaderBindFlagBits::Vertex);
	RenderCommand::BindConstantBuffer(LandscapeParamsSlot, ParamsBuffer.get(), EShaderBindFlagBits::Vertex);
}
