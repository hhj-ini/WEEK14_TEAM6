#include "EnginePCH.h"
#include "SkyboxRenderer.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"
#include "ImageLoader.h"
#include "Engine/SceneManagement.h"

#include <algorithm>
#include <cmath>

namespace
{
	// 라이트 각반경이 0에 가까워도 화면에서 점이 아니라 원판으로 보이게 하는 하한
	constexpr float MinSunRadiusRadians = 0.5f * 3.14159265f / 180.0f;
	// Intensity 1 라이트는 하늘과 밝기가 비슷해 묻히므로, 원판만 따로 밝혀 플레어 Threshold를 넘기게 한다
	constexpr float SunDiskBrightness = 50.0f;

	// 샘플링 결과가 이미 선형인 포맷인지 판단한다.
	// - _SRGB 포맷: 하드웨어가 샘플링할 때 sRGB -> 선형으로 디코딩한다 (예: Sky.dds = BC7_UNORM_SRGB)
	// - float 포맷: .hdr처럼 원래 선형 값이다
	// 그 외 8bit/BC UNORM 포맷(.jpg/.png, 비-sRGB DDS)은 sRGB 인코딩 값이 그대로 나오므로 셰이더에서 디코딩한다.
	bool IsSampledAsLinear(DXGI_FORMAT Format)
	{
		switch (Format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
		case DXGI_FORMAT_R32G32B32_FLOAT:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
			return true;
		default:
			return false;
		}
	}
}

bool FSkyboxRenderer::Init(const FString& PanoramaPath)
{
	Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/SkyboxShader.hlsl");
	if (!Shader)
	{
		KRS_LOG(Error, "[Skybox] shader not found");
		return false;
	}

	FImageData Image = ImageLoader::LoadAuto(PanoramaPath);
	if (!Image.IsValid())
	{
		KRS_LOG(Error, "[Skybox] panorama load failed: {}", PanoramaPath);
		return false;
	}

	// 포맷은 Image를 따른다(.hdr이면 R32G32B32A32_FLOAT).
	FTextureDesc Desc{};
	Desc.Width = Image.Width;
	Desc.Height = Image.Height;
	Desc.Usage = ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	PanoramaTexture = RenderCommand::CreateTexture2D(Desc, Image);
	if (!PanoramaTexture) return false;

	bDecodeSRGBInShader = !IsSampledAsLinear(Image.Format);

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FSkyboxConstants));

	PipelineState.Shader = Shader;
	PipelineState.Topology = EPrimitiveTopology::TriangleList;
	// 전체 화면 삼각형이라 컬링 방향을 따질 필요가 없다.
	PipelineState.RasterizerState = ERasterizerState::SolidNone;
	PipelineState.BlendState = EBlendState::Opaque;
	// 깊이 테스트/쓰기 모두 끈다. 배경을 먼저 깔고 물체가 그 위에 그려지게 한다.
	PipelineState.DepthStencilState = EDepthStencilState::Disabled;

	KRS_LOG(Info, "[Skybox] loaded: {} ({}x{}, format {}, shader sRGB decode {})",
		PanoramaPath, Image.Width, Image.Height, static_cast<uint32>(Image.Format), bDecodeSRGBInShader ? "on" : "off");
	return true;
}

void FSkyboxRenderer::OnRender(const FMatrix& ViewProjection, const FVector& CameraPosition, const FLightRenderParameters* SunLight)
{
	if (!IsValid()) return;

	FSkyboxConstants Constants;
	// 업로드할 때 전치한다. HLSL은 mul(벡터, 행렬) 규약을 쓴다 (Renderer.cpp와 동일).
	Constants.InverseViewProjection = ViewProjection.Inverse();
	Constants.CameraPosition = CameraPosition;
	Constants.DecodeSRGB = bDecodeSRGBInShader ? 1.0f : 0.0f;
	if (SunLight)
	{
		// 라이트 Direction은 빛이 나아가는 방향이라, 태양이 있는 쪽은 그 반대다.
		Constants.SunDirection = -SunLight->Direction;
		// SourceSize는 tan(각반경)으로 저장돼 있어 각도로 되돌린 뒤 cos로 바꾼다.
		const float SunRadius = std::max(std::atan(SunLight->SourceSize), MinSunRadiusRadians);
		Constants.CosSunRadius = std::cos(SunRadius);
		Constants.SunRadiance = SunLight->Color * (SunLight->Intensity * SunDiskBrightness);
		Constants.bHasSun = 1.0f;
	}

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants);
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Pixel);
	RenderCommand::BindShaderResource(0, PanoramaTexture.get(), EShaderBindFlagBits::Pixel);
	// 경도(U)가 0<->1에서 감기므로 Clamp가 아니라 Wrap이어야 이음매가 안 보인다.
	RenderCommand::BindSamplerState(0, ESamplerState::LinearWrap, EShaderBindFlagBits::Pixel);

	// 정점 버퍼 없이 3개. VS가 SV_VertexID로 삼각형을 만든다.
	RenderCommand::Draw(3);
}                               
