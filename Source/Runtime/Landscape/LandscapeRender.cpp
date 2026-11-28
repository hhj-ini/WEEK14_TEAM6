#include "EnginePCH.h"
#include "Landscape/LandscapeRender.h"
#include "Landscape/Landscape.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include <algorithm>

FLandscapeComponentSceneProxy::FLandscapeComponentSceneProxy(ULandscapeComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
}

FLandscapeComponentSceneProxy::~FLandscapeComponentSceneProxy() = default;

void FLandscapeComponentSceneProxy::UpdateRenderState()
{
	// 부모는 StaticMesh 렌더 상태를 만들므로 부르지 않는다 (ParticleSystemSceneProxy와 같다)
	UpdateCommonFlags();

	// 그림자 패스는 RenderState가 있는 프록시만 본다. 섹션은 비워 두고 GetDynamicMeshElements로 그린다
	RenderState = std::make_shared<FMeshRenderState>();

	if (!Material)
		Material = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

	const ULandscapeComponent* Component = static_cast<const ULandscapeComponent*>(GetComponent());
	const ALandscape* Landscape = Component->GetLandscape();
	if (!Landscape || Component->SectionBaseX < 0 || Component->SectionBaseY < 0)
		return;   // AddComponent 직후, 아직 배치 전

	// 같은 조각을 이미 만들었으면 다시 만들지 않는다
	if (HeightTexture && SharedBuffers == Landscape->GetSharedBuffers()
		&& BuiltBaseX == Component->SectionBaseX && BuiltBaseY == Component->SectionBaseY)
		return;

	SharedBuffers = Landscape->GetSharedBuffers();
	if (!SharedBuffers || !SharedBuffers->IsValid())
		return;

	BuildHeightTexture(*Component, Landscape->GetTerrainData());
	if (!HeightTexture)
		return;

	VertexFactory.Init(SharedBuffers->GetVertexBuffer(), SharedBuffers->GetIndexBuffer(), HeightTexture.get());
	BuiltBaseX = Component->SectionBaseX;
	BuiltBaseY = Component->SectionBaseY;
}

void FLandscapeComponentSceneProxy::BuildHeightTexture(const ULandscapeComponent& Component, const FTerrainData& Data)
{
	// 정점 65x65에 테두리 1칸씩 붙여 67x67.
	// 셰이더가 법선을 구할 때 옆 조각의 높이까지 봐야 조각 경계에서 조명이 끊기지 않는다
	const int32 Size = LandscapeComponentQuads + 3;

	TArray<uint16> Texels;
	Texels.SetNum(Size * Size);
	for (int32 TY = 0; TY < Size; ++TY)
	{
		for (int32 TX = 0; TX < Size; ++TX)
		{
			// 텍셀 (1,1)이 이 조각의 정점 (0,0). 맵 바깥은 가장자리 값으로 채운다
			const int32 IX = std::clamp(Component.SectionBaseX + TX - 1, 0, Data.SizeX - 1);
			const int32 IY = std::clamp(Component.SectionBaseY + TY - 1, 0, Data.SizeY - 1);
			Texels[TY * Size + TX] = Data.Heights[IY * Data.SizeX + IX];
		}
	}

	FTextureDesc Desc;
	Desc.Width = Size;
	Desc.Height = Size;
	Desc.MipLevels = 1;
	Desc.Format = ETextureFormat::R16_UNorm;          // 셰이더에서 0~1 실수로 읽힌다
	Desc.BindFlags = ETextureBindFlags::ShaderResource;
	Desc.Usage = ETextureUsage::Immutable;            // 만든 뒤 바꾸지 않는다
	HeightTexture = RenderCommand::CreateTexture2D(Desc, Texels.GetData());
}

void FLandscapeComponentSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	if (!VertexFactory.IsValid() || !HeightTexture || !Material || !SharedBuffers)
		return;

	// 조각 하나 = 드로우 1개. 4단계는 LOD0 전체(64x64칸 = 8192 삼각형)를 그린다
	FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
	Packet.Proxy = this;                 // 월드 행렬은 MeshPassProcessor가 Proxy->GetLocalToWorld()로 읽는다
	Packet.VertexFactory = &VertexFactory;
	Packet.Material = Material;
	Packet.StartIndex = 0;
	Packet.IndexCount = SharedBuffers->GetIndexCount();
}
