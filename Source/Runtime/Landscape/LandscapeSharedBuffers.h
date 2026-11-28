#pragma once

#include "Core/Types.h"

class FVertexBuffer;
class FIndexBuffer;

// 컴포넌트 한 장이 맡는 칸 수 (가로·세로). UE 규격: 2^n - 1 = 63칸, 정점은 한 변에 64개.
// 63칸이어야 정점 수(64)가 2의 거듭제곱이라 밉을 반씩 줄일 때 끝 정점이 정확히 맞는다 (64→32→…→2)
inline constexpr int32 LandscapeComponentQuads = 63;
// 컴포넌트 하이트맵 텍스처 한 변 크기 = 정점 수
inline constexpr int32 LandscapeHeightmapSize = LandscapeComponentQuads + 1;
// 밉 단계 수: 64, 32, 16, 8, 4, 2 (정점 2개 = 칸 1개가 마지막). 7단계 LOD가 이 수만큼 쓴다
inline constexpr int32 LandscapeNumLODs = 6;

// 공유 격자의 정점. 컴포넌트 안 격자 좌표(0~63)만 가진다.
// 높이·기울기는 컴포넌트마다 다른 하이트맵 텍스처에서 VF_Landscape.hlsli가 읽는다.
struct FLandscapeVertex
{
	float X;
	float Y;
};

// 모든 ULandscapeComponent가 같이 쓰는 평평한 격자 (UE FLandscapeSharedBuffers 대응).
// 256개 컴포넌트가 정점·인덱스 버퍼 한 벌을 돌려 쓴다. 5단계는 LOD0 한 벌만 만든다
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
