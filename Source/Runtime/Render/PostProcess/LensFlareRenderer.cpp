#include "EnginePCH.h"
#include "Render/PostProcess/LensFlareRenderer.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/Texture2D.h"
#include "Render/ImageLoader.h"
#include "Core/Misc/Paths.h"

namespace
{
	FPipelineState MakeFullscreenPipeline(FShaderProgram* Shader, EBlendState BlendState)
	{
		FPipelineState State;
		State.Shader = Shader;
		State.Topology = EPrimitiveTopology::TriangleList;
		State.RasterizerState = ERasterizerState::SolidNone;
		State.BlendState = BlendState;
		State.DepthStencilState = EDepthStencilState::Disabled;
		return State;
	}
}

void FLensFlareRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;
	BrightShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareBright.hlsl");
	GhostShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareGhost.hlsl");
	CompositeShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareComposite.hlsl");
	BlurShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareBlur.hlsl");
	BokehShader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/LensFlareBokeh.hlsl");
	BrightPipelineState = MakeFullscreenPipeline(BrightShader, EBlendState::Opaque);
	GhostPipelineState = MakeFullscreenPipeline(GhostShader, EBlendState::Opaque);
	BlurPipelineState = MakeFullscreenPipeline(BlurShader, EBlendState::Opaque);
	BokehPipelineState = MakeFullscreenPipeline(BokehShader, EBlendState::Opaque);
	// 장면을 복사하지 않고 플레어만 위에 얹으면 되므로 가산 블렌드로 그린다
	CompositePipelineState = MakeFullscreenPipeline(CompositeShader, EBlendState::Additive);
	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FLensFlareData));
	BlurConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FLensFlareBlurData));
	assert(BrightShader && GhostShader && CompositeShader && BlurShader && BokehShader && ConstantBuffer && BlurConstantBuffer);

	// 먼지는 렌즈에 붙어 있어 화면 UV에 고정이므로 한 번만 읽어 둔다. 없으면 먼지 없이 플레어만 나온다
	const FImageData DirtImage = ImageLoader::Load(FPaths::EngineFileUtf8("EngineResources/Textures/LensDirt.png"));
	if (DirtImage.IsValid())
	{
		FTextureDesc Desc{};
		Desc.Width = DirtImage.Width;
		Desc.Height = DirtImage.Height;
		Desc.Usage = ETextureUsage::Immutable;
		DirtTexture = RenderCommand::CreateTexture2D(Desc, DirtImage);
	}
}

void FLensFlareRenderer::UploadConstants(const FSceneView& View)
{
	const FPostProcessSettings& PP = View.FinalPostProcessSettings;
	FLensFlareData Data{};
	Data.Intensity = PP.LensFlareIntensity;
	Data.Threshold = PP.LensFlareThreshold;
	Data.GhostCount = static_cast<float>(PP.LensFlareGhostCount);
	Data.GhostDispersal = PP.LensFlareGhostDispersal;
	Data.HaloWidth = PP.LensFlareHaloWidth;
	Data.HaloIntensity = PP.LensFlareHaloIntensity;
	Data.ChromaticDistortion = PP.LensFlareChromaticDistortion;
	Data.DirtIntensity = PP.LensFlareDirtIntensity;
	// 같은 렌즈의 같은 조리개라 DOF 보케와 날 수를 함께 쓴다
	Data.BladeCount = static_cast<float>(PP.DepthOfFieldBladeCount);
	Data.BokehRadius = PP.LensFlareBokehSize;
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
}

bool FLensFlareRenderer::DrawFullscreen(const FPipelineState& Pipeline, FTexture2D* Texture)
{
	if (!Pipeline.Shader || !Texture) return false;

	RenderCommand::BindPipelineState(Pipeline);
	RenderCommand::BindVertexBuffer(nullptr);
	RenderCommand::BindSamplerState(0, ESamplerState::LinearClamp, EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, Texture, EShaderBindFlagBits::Pixel);
	RenderCommand::Draw(6);
	// 다음 패스가 이 텍스처를 렌더 타깃으로 쓸 수 있게 푼다
	RenderCommand::UnbindShaderResources(0, 1, EShaderBindFlagBits::Pixel);
	return true;
}

bool FLensFlareRenderer::RenderBrightPass(const FSceneView& View, FTexture2D* SceneColor)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	return DrawFullscreen(BrightPipelineState, SceneColor);
}

bool FLensFlareRenderer::RenderBokeh(const FSceneView& View, FTexture2D* BrightTexture)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	return DrawFullscreen(BokehPipelineState, BrightTexture);
}

bool FLensFlareRenderer::RenderGhosts(const FSceneView& View, FTexture2D* BokehTexture, FTexture2D* BrightTexture)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	if (BrightTexture)
		RenderCommand::BindShaderResource(1, BrightTexture, EShaderBindFlagBits::Pixel);
	const bool bOk = DrawFullscreen(GhostPipelineState, BokehTexture);
	RenderCommand::UnbindShaderResources(1, 1, EShaderBindFlagBits::Pixel);
	return bOk;
}

bool FLensFlareRenderer::RenderBlur(FTexture2D* SourceTexture, float DirectionX, float DirectionY)
{
	if (!BlurConstantBuffer) return false;
	FLensFlareBlurData Data{};
	Data.DirectionX = DirectionX;
	Data.DirectionY = DirectionY;
	RenderCommand::UpdateBufferData(BlurConstantBuffer.get(), &Data, sizeof(Data));
	RenderCommand::BindConstantBuffer(1, BlurConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	return DrawFullscreen(BlurPipelineState, SourceTexture);
}

bool FLensFlareRenderer::RenderComposite(const FSceneView& View, FTexture2D* GhostTexture)
{
	if (!ConstantBuffer) return false;
	UploadConstants(View);
	if (DirtTexture)
		RenderCommand::BindShaderResource(1, DirtTexture.get(), EShaderBindFlagBits::Pixel);
	const bool bOk = DrawFullscreen(CompositePipelineState, GhostTexture);
	RenderCommand::UnbindShaderResources(1, 1, EShaderBindFlagBits::Pixel);
	return bOk;
}
