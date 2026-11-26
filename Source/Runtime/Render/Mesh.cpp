#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Physics/BodySetup.h"
#include <algorithm>
#include <cmath>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
    // 바운드가 바뀌었을 수 있으므로 충돌 박스도 다음 GetBodySetup에서 새로 만든다.
    InvalidateBodySetup();
}

UBodySetup* UStaticMesh::GetBodySetup()
{
	if (BodySetup)
		return BodySetup;

	const FBox& Bounds = MeshData.AABB;
	if (Bounds.Max.X < Bounds.Min.X)
		return nullptr;   // 비어 있는 메시

	BodySetup = NewObject<UBodySetup>(this);

	// Static으로 배치한 메시는 삼각형 그대로 막는다 (Movable 바디는 아래 볼록 껍질을 쓴다).
	BodySetup->CollisionTraceFlag = ECollisionTraceFlag::UseComplexAsSimple;

	// Complex 충돌용 삼각형. Cooking은 Static 바디가 처음 요청할 때 한다.
	BodySetup->TriMeshIndices = MeshData.Indices;
	BodySetup->TriMeshVertices.Reserve(MeshData.Vertices.Num());
	for (const FVertex& Vertex : MeshData.Vertices)
		BodySetup->TriMeshVertices.Add(Vertex.Position);

	// 감김 순서 판정: 삼각형 면 법선(외적)이 정점 법선과 대체로 반대면 뒤집는다.
	float Agreement = 0.0f;
	for (int32 Index = 0; Index + 2 < MeshData.Indices.Num(); Index += 3)
	{
		const FVertex& V0 = MeshData.Vertices[MeshData.Indices[Index]];
		const FVertex& V1 = MeshData.Vertices[MeshData.Indices[Index + 1]];
		const FVertex& V2 = MeshData.Vertices[MeshData.Indices[Index + 2]];
		const FVector FaceNormal = (V1.Position - V0.Position).Cross(V2.Position - V0.Position);
		Agreement += FaceNormal.Dot(V0.Normal + V1.Normal + V2.Normal);
	}
	BodySetup->bFlipTriMeshNormals = Agreement < 0.0f;

	// 메시 정점 전체를 감싸는 볼록 껍질 하나.
	FKConvexElem Convex;
	Convex.VertexData.Reserve(MeshData.Vertices.Num());
	for (const FVertex& Vertex : MeshData.Vertices)
		Convex.VertexData.Add(Vertex.Position);
	BodySetup->AggGeom.ConvexElems.Add(std::move(Convex));

	if (BodySetup->CreatePhysicsMeshes())
		return BodySetup;

	// 평면처럼 부피가 없어 Cooking이 실패하면 AABB 박스. 반 크기는 최소 1cm.
	BodySetup->ClearPhysicsMeshes();
	BodySetup->AggGeom.ConvexElems.Empty();

	FKBoxElem& Box = BodySetup->AggGeom.BoxElems.Emplace_GetRef();
	Box.Center = (Bounds.Min + Bounds.Max) * 0.5f;
	Box.Extent = (Bounds.Max - Bounds.Min) * 0.5f;
	constexpr float MinHalfExtent = 0.01f;
	Box.Extent = FVector(std::max(Box.Extent.X, MinHalfExtent), std::max(Box.Extent.Y, MinHalfExtent), std::max(Box.Extent.Z, MinHalfExtent));
	return BodySetup;
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    for (const FStaticMeshSection& Section : MeshData.Sections)
    {
        UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
            ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
        if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}
