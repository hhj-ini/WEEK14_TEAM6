#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include "Render/VertexFactory.h"
#include <unordered_map>
#include <vector>
#include "Mesh.generated.h"

class FPrimitiveSceneProxy;
class UBodySetup;

UCLASS()
class UStaticMesh : public URenderAsset
{
	GENERATED_BODY()
public:
    UStaticMesh() : SortID(NextSortID++) {}
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    FString SourceObjPath;
    FVertexBuffer* GetVertexBuffer() const { return VertexBuffer.get(); }
    FIndexBuffer* GetIndexBuffer() const { return IndexBuffer.get(); }
    // 버퍼를 다시 만들 수 있으므로 꺼낼 때마다 현재 버퍼로 맞춘다
    const FVertexFactory* GetVertexFactory() const
    {
        VertexFactory.Init(VertexBuffer.get(), IndexBuffer.get());
        return &VertexFactory;
    }
    void RebuildRenderData();
    const FBox& GetRenderBounds() const { return RenderBounds; }
    TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    // 충돌 형상. 처음 불릴 때 MeshData.AABB로 박스 하나를 만든다. 메시 데이터가 바뀌면 InvalidateBodySetup으로 버린다.
    UBodySetup* GetBodySetup();
    void InvalidateBodySetup() { BodySetup = nullptr; }

    const uint16 SortID;
private:
    UBodySetup* BodySetup = nullptr;   // 메시 데이터로 다시 만들 수 있으므로 저장하지 않는다
    static inline uint16 NextSortID = 0;
    struct FMaterialSlotsHash
    {
        size_t operator()(const std::vector<UMaterial*>& Slots) const;
    };
    FBox RenderBounds;
    mutable FLocalVertexFactory VertexFactory;
    std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
    TArray<FPrimitiveSceneProxy*> RenderProxies;
};
