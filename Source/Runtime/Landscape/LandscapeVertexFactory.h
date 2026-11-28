#pragma once

#include "Render/VertexFactory.h"

class FTexture2D;

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
	}
	EVertexFactoryType GetType() const override { return EVertexFactoryType::Landscape; }
	void Bind() const override;

private:
	FTexture2D* HeightmapTexture = nullptr;
};
