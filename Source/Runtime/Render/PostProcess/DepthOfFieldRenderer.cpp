#include "EnginePCH.h"
#include "Render/PostProcess/DepthOfFieldRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader)
	{
		FPipelineState State;
		State.Shader = Shader;
		State.Topology = EPrimitiveTopology::TriangleList;
		State.RasterizerState = ERasterizerState::SolidNone;
		// 화면 전체를 새 값으로 덮어쓰므로 섞지 않는다
		State.BlendState = EBlendState::Opaque;
		State.DepthStencilState = EDepthStencilState::Disabled;
		return State;
	}
}

void FDepthOfFieldRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	SetupShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldSetup.hlsl");
	VisualizeShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldVisualize.hlsl");
	DownsampleShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldDownsample.hlsl");
	GatherShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldGather.hlsl");
	PostFilterShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldPostFilter.hlsl");
	CompositeShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/DepthOfFieldComposite.hlsl");
	SetupPipelineState = MakeFullscreenPipeline(SetupShader);
	VisualizePipelineState = MakeFullscreenPipeline(VisualizeShader);
	DownsamplePipelineState = MakeFullscreenPipeline(DownsampleShader);
	GatherPipelineState = MakeFullscreenPipeline(GatherShader);
	PostFilterPipelineState = MakeFullscreenPipeline(PostFilterShader);
	CompositePipelineState = MakeFullscreenPipeline(CompositeShader);
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldData));
	GatherConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FDepthOfFieldGatherData));
	assert(SetupShader && VisualizeShader && DownsampleShader && GatherShader && PostFilterShader && CompositeShader && ConstantBuffer && GatherConstantBuffer);
}

bool FDepthOfFieldRenderer::RenderSetup(const FSceneView& View, FTexture2D* DepthTarget)
{
	if (!SetupShader || !ConstantBuffer || !DepthTarget) return false;

	FDepthOfFieldData Data{};
	// 셰이더가 식을 따로 갖지 않도록, 실제 투영 행렬에서 깊이 변환 값을 꺼낸다
	Data.ZScale = View.ProjectionMatrix.M[0][2];
	Data.ZOffset = View.ProjectionMatrix.M[3][2];
	Data.FocalDistance = View.FinalPostProcessSettings.DepthOfFieldFocalDistance;
	Data.FocusRange = View.FinalPostProcessSettings.DepthOfFieldFocusRange;
	Data.Fstop = View.FinalPostProcessSettings.DepthOfFieldFstop;
	Data.ViewWidthPx = View.ViewRect.Width;
	Data.FocalLength = View.FinalPostProcessSettings.DepthOfFieldSensorWidth / 2 * View.ProjectionMatrix.M[1][0] * 0.001f;
	Data.SensorWidth = View.FinalPostProcessSettings.DepthOfFieldSensorWidth * 0.001f;

	RenderCommand::BindPipelineState(SetupPipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, DepthTarget, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	// 다음 패스가 깊이를 다시 깊이 타깃으로 꽂을 때 충돌하지 않게 푼다
	RenderCommand::BindShaderResource(0, static_cast<FTexture2D*>(nullptr), EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::RenderVisualize(FTexture2D* CoCTexture, FTexture2D* SceneColorCopy)
{
	if (!VisualizeShader || !CoCTexture || !SceneColorCopy) return false;

	RenderCommand::BindPipelineState(VisualizePipelineState);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindShaderResource(0, CoCTexture, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(1, SceneColorCopy, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	RenderCommand::UnbindShaderResources(0, 2, EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::DrawFullscreen(const FPipelineState& Pipeline, std::initializer_list<FTexture2D*> Textures)
{
	if (!Pipeline.Shader) return false;
	for (FTexture2D* Texture : Textures)
		if (!Texture) return false;

	RenderCommand::BindPipelineState(Pipeline);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	uint32 Slot = 0;
	for (FTexture2D* Texture : Textures)
		RenderCommand::BindShaderResource(Slot++, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	// 다음 패스가 이 텍스처들을 렌더 타깃으로 쓸 수 있게 푼다
	RenderCommand::UnbindShaderResources(0, Slot, EShaderBindFlagBits::Pixel);
	return true;
}

bool FDepthOfFieldRenderer::RenderDownsample(FTexture2D* SceneColorCopy, FTexture2D* CoCTexture)
{
	return DrawFullscreen(DownsamplePipelineState, { SceneColorCopy, CoCTexture });
}

bool FDepthOfFieldRenderer::RenderGather(const FSceneView& View, FTexture2D* HalfResTexture)
{
	if (!GatherConstantBuffer) return false;

	FDepthOfFieldGatherData Data{};
	Data.BladeCount = static_cast<float>(View.FinalPostProcessSettings.DepthOfFieldBladeCount);
	RenderCommand::UpdateBufferData(GatherConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, GatherConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	return DrawFullscreen(GatherPipelineState, { HalfResTexture });
}

bool FDepthOfFieldRenderer::RenderPostFilter(FTexture2D* FarTexture, FTexture2D* NearTexture)
{
	return DrawFullscreen(PostFilterPipelineState, { FarTexture, NearTexture });
}

bool FDepthOfFieldRenderer::RenderComposite(FTexture2D* SceneColorCopy, FTexture2D* CoCTexture, FTexture2D* FarTexture, FTexture2D* NearTexture)
{
	return DrawFullscreen(CompositePipelineState, { SceneColorCopy, CoCTexture, FarTexture, NearTexture });
}
