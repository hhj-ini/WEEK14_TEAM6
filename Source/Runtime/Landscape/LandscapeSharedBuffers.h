#pragma once

#include "Core/Types.h"

class FVertexBuffer;
class FIndexBuffer;

// 컴포넌트 한 장이 맡는 칸 수 (가로·세로). 정점은 한 변에 65개
inline constexpr int32 LandscapeComponentQuads = 64;

// 공유 격자의 정점. 컴포넌트 안 격자 좌표(0~64)만 가진다.
// 높이는 컴포넌트마다 다른 하이트맵 텍스처에서 VF_Landscape.hlsli가 읽는다.
struct FLandscapeVertex
{
	float X;
	float Y;
};

// 모든 ULandscapeComponent가 같이 쓰는 평평한 격자 (UE FLandscapeSharedBuffers 대응).
// 256개 컴포넌트가 정점·인덱스 버퍼 한 벌을 돌려 쓴다.
class FLandscapeSharedBuffers
{
public:
	explicit FLandscapeSharedBuffers(int32 InComponentQuads);
	~FLandscapeSharedBuffers();

	bool IsValid() const { return VertexBuffer && IndexBuffer; }
	FVertexBuffer* GetVertexBuffer() const { return VertexBuffer.get(); }
	FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
	uint32 GetIndexCount() const { return IndexCount; }
	int32 GetComponentQuads() const { return ComponentQuads; }

private:
	int32 ComponentQuads = 0;
	uint32 IndexCount = 0;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
};
