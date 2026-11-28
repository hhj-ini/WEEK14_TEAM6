#pragma once

#include "Render/VertexFactory.h"

class FTexture2D;

struct FLandscapeComponentParams
{
	FVector2 SectionBase;		//	LandScape 전체 기준일때의 현재 위치, 조각 단위로 위치를 계산하므로 Splat을 위해 누적한 값이 필요함 ex) 1024*1024 일때 (192,320)
	FVector2 InvTerrainQuads;	//  SectionBase와 곱해 전체 기준 UV를 구할때 사용, 전체 텍스쳐 크기의 역수
};

inline constexpr uint32 LandscapeParamsSlot = 4;

// 공유 격자(정점·인덱스) + 컴포넌트 하이트맵 텍스처. UE FLandscapeVertexFactory 대응.
// 컴포넌트마다 하나씩 가진다 (버퍼는 같고 텍스처만 다르다).
class FLandscapeVertexFactory : public FVertexFactory
{
public:

	void Init(FVertexBuffer* VB, FIndexBuffer* IB, FTexture2D* InHeightmap)
	{
		VertexBuffer = VB;
		IndexBuffer = IB;
		HeightmapTexture = InHeightmap;
		if (!ParamsBuffer) ParamsBuffer = RenderCommand::CreateConstantBuffer(ParamsBuffer.get()->GetBufferSize());
	}

	EVertexFactoryType GetType() const override { return EVertexFactoryType::Landscape; }
	void Bind() const override;

private:
	FTexture2D* HeightmapTexture = nullptr;
	TUniquePtr<FConstantBuffer> ParamsBuffer;
};
