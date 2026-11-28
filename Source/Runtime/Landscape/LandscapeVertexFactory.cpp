#include "EnginePCH.h"
#include "Landscape/LandscapeVertexFactory.h"
#include "Render/RenderCommand.h"
#include "Render/Texture2D.h"

void FLandscapeVertexFactory::Init(FVertexBuffer* VB, FIndexBuffer* IB, FTexture2D* InHeightmap, const FLandscapeComponentParams& Parameter)
{
	VertexBuffer = VB;
	IndexBuffer = IB;
	HeightmapTexture = InHeightmap;

	if (!ParamsBuffer)
		ParamsBuffer = RenderCommand::CreateConstantBuffer(sizeof(FLandscapeComponentParams));
	if (ParamsBuffer)
		RenderCommand::UpdateBufferData(ParamsBuffer.get(), &Parameter);
}

void FLandscapeVertexFactory::Bind() const
{
	FVertexFactory::Bind();
	RenderCommand::BindShaderResource(LandscapeHeightmapSlot, HeightmapTexture, EShaderBindFlagBits::Vertex);
	RenderCommand::BindConstantBuffer(LandscapeParamsSlot, ParamsBuffer.get(), EShaderBindFlagBits::Vertex);
}
