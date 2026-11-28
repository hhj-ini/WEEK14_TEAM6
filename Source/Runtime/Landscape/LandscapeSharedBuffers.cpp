#include "EnginePCH.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Render/Buffer.h"
#include "Render/RenderCommand.h"

FLandscapeSharedBuffers::FLandscapeSharedBuffers(int32 InComponentQuads)
	: ComponentQuads(InComponentQuads)
{
	const int32 Q = ComponentQuads;
	const int32 Verts = Q + 1;   // 한 변 정점 수 (65)

	// 정점: (0,0) ~ (64,64) 격자 좌표
	TArray<FLandscapeVertex> Vertices;
	Vertices.SetNum(Verts * Verts);
	for (int32 IY = 0; IY < Verts; ++IY)
	{
		for (int32 IX = 0; IX < Verts; ++IX)
		{
			Vertices[IY * Verts + IX] = { static_cast<float>(IX), static_cast<float>(IY) };
		}
	}

	// 인덱스: FTerrainData::GetLOD0Geometry와 같은 대각선(V00-V11), 같은 감기 순서
	TArray<uint32> Indices;
	Indices.SetNum(Q * Q * 6);
	int32 K = 0;
	for (int32 IY = 0; IY < Q; ++IY)
	{
		for (int32 IX = 0; IX < Q; ++IX)
		{
			const uint32 V00 = IY * Verts + IX;
			const uint32 V10 = V00 + 1;
			const uint32 V01 = V00 + Verts;
			const uint32 V11 = V01 + 1;

			Indices[K++] = V00; Indices[K++] = V10; Indices[K++] = V11;   // 삼각형 A
			Indices[K++] = V00; Indices[K++] = V11; Indices[K++] = V01;   // 삼각형 B
		}
	}
	IndexCount = static_cast<uint32>(Indices.Num());

	VertexBuffer = RenderCommand::CreateStaticVertexBuffer(Vertices.GetData(),
		static_cast<uint32>(sizeof(FLandscapeVertex) * Vertices.Num()), sizeof(FLandscapeVertex));
	IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Indices.GetData(), IndexCount);
}

FLandscapeSharedBuffers::~FLandscapeSharedBuffers() = default;
