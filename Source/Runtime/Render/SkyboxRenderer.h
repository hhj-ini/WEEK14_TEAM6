#pragma once

#include "Buffer.h"
#include "PipelineState.h"
#include "Texture2D.h"

// HLSL의 cbuffer SkyboxConstants와 레이아웃이 일치해야 한다.
// float3 뒤의 한 칸은 16바이트 정렬용 자리를 sRGB 디코딩 플래그로 쓴다.
struct FSkyboxConstants
{
	FMatrix InverseViewProjection;
	FVector CameraPosition;
	float   DecodeSRGB = 0.0f;	// 1이면 셰이더에서 sRGB -> 선형 변환
	FVector SunDirection = FVector(0.0f, 0.0f, 1.0f);	// 카메라에서 태양을 보는 방향 (= -라이트 Forward)
	float   CosSunRadius = 1.0f;	// 내적이 이 값보다 크면 원판 안. 각도 비교를 픽셀마다 acos 없이 하려고 cos로 넘긴다
	FVector SunRadiance = FVector(0.0f, 0.0f, 0.0f);	// 원판 HDR 밝기 (색 x 세기)
	float   bHasSun = 0.0f;	// 디렉셔널 라이트가 없으면 0
};

struct FLightRenderParameters;

// 등장방형 파노라마 한 장을 배경으로 그린다.
// 정점 버퍼 없이 전체 화면 삼각형 하나만 그리므로 메시가 필요 없다.
class FSkyboxRenderer
{
public:
	FSkyboxRenderer() = default;
	~FSkyboxRenderer() = default;

	bool Init(const FString& PanoramaPath);

	// 불투명 물체보다 먼저 호출한다. 깊이를 쓰지 않으므로 이후 물체가 위에 그려진다.
	// SunLight가 nullptr이면 태양 원판을 그리지 않는다.
	void OnRender(const FMatrix& ViewProjection, const FVector& CameraPosition, const FLightRenderParameters* SunLight = nullptr);

	bool IsValid() const { return Shader != nullptr && PanoramaTexture != nullptr; }

private:
	FShaderProgram* Shader = nullptr;
	TUniquePtr<FConstantBuffer> ConstantBuffer;
	TUniquePtr<FTexture2D> PanoramaTexture;
	FPipelineState PipelineState;
	// 텍스처가 sRGB 인코딩 값을 그대로 돌려주는 포맷이라 셰이더에서 디코딩해야 하는지
	bool bDecodeSRGBInShader = false;
};
