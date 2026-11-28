#include "EnginePCH.h"
#include "Landscape/LandscapeRender.h"
#include "Landscape/LandscapeActor.h"
#include "Landscape/LandscapeComponent.h"
#include "Landscape/LandscapeSharedBuffers.h"
#include "Landscape/LandscapeStats.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"
#include "Render/RenderDevice.h"
#include "Render/Material.h"
#include "Asset/AssetManager.h"
#include <algorithm>
#include <cmath>

FLandscapeComponentSceneProxy::FLandscapeComponentSceneProxy(ULandscapeComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	FLandscapeStats::AddProxy();
}

FLandscapeComponentSceneProxy::~FLandscapeComponentSceneProxy()
{
	FLandscapeStats::RemoveProxy();
}

void FLandscapeComponentSceneProxy::UpdateRenderState()
{
	// 부모는 StaticMesh 렌더 상태를 만들므로 부르지 않는다 (ParticleSystemSceneProxy와 같다)
	UpdateCommonFlags();

	// 그림자 패스는 RenderState가 있는 프록시만 본다. 섹션은 비워 두고 GetDynamicMeshElements로 그린다
	RenderState = std::make_shared<FMeshRenderState>();

	if (!Material)
		Material = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

	const ULandscapeComponent* Component = static_cast<const ULandscapeComponent*>(GetComponent());
	const ALandscapeActor* LandscapeActor = Component->GetLandscapeActor();
	if (!LandscapeActor || Component->SectionBaseX < 0 || Component->SectionBaseY < 0)
		return;   // AddComponent 직후, 아직 배치 전

	// 같은 조각을 이미 만들었으면 다시 만들지 않는다
	// 법선이 월드 비율로 구워져 있으므로 CellSize·HeightScale이 바뀌어도 다시 굽는다
	const FTerrainData& Data = LandscapeActor->GetTerrainData();
	if (HeightTexture && SharedBuffers == LandscapeActor->GetSharedBuffers()
		&& BuiltBaseX == Component->SectionBaseX && BuiltBaseY == Component->SectionBaseY
		&& BuiltCellSize == Data.CellSize && BuiltHeightScale == Data.HeightScale)
		return;

	SharedBuffers = LandscapeActor->GetSharedBuffers();
	if (!SharedBuffers || !SharedBuffers->IsValid())
		return;

	BuildHeightmapTexture(*Component, Data);
	if (!HeightTexture)
		return;

	VertexFactory.Init(SharedBuffers->GetVertexBuffer(), SharedBuffers->GetIndexBuffer(), HeightTexture.get());
	BuiltBaseX = Component->SectionBaseX;
	BuiltBaseY = Component->SectionBaseY;
	BuiltCellSize = Data.CellSize;
	BuiltHeightScale = Data.HeightScale;
}

namespace
{
	// 격자 좌표 (GX, GY)의 높이 (0~1). 정수가 아니면 그 칸의 삼각형 평면으로 보간한다.
	// 대각선(V00-V11)이 FTerrainData::GetHeight·공유 인덱스 버퍼와 같아야
	// 거친 밉의 점이 LOD0 메시 표면 위에 정확히 놓인다
	float SampleHeight01(const FTerrainData& Data, float GX, float GY)
	{
		GX = std::clamp(GX, 0.0f, static_cast<float>(Data.SizeX - 1));
		GY = std::clamp(GY, 0.0f, static_cast<float>(Data.SizeY - 1));

		const int32 IX = (std::min)(static_cast<int32>(GX), Data.SizeX - 2);
		const int32 IY = (std::min)(static_cast<int32>(GY), Data.SizeY - 2);
		const float FX = GX - IX;
		const float FY = GY - IY;

		auto H = [&](int32 X, int32 Y) { return Data.Heights[Y * Data.SizeX + X] / 65535.0f; };
		const float H00 = H(IX, IY);
		const float H10 = H(IX + 1, IY);
		const float H01 = H(IX, IY + 1);
		const float H11 = H(IX + 1, IY + 1);

		if (FX >= FY)
			return H00 + FX * (H10 - H00) + FY * (H11 - H10);   // 삼각형 A (V00, V10, V11)
		return H00 + FY * (H01 - H00) + FX * (H11 - H01);       // 삼각형 B (V00, V11, V01)
	}

	uint8 EncodeUNorm8(float Value01)
	{
		return static_cast<uint8>(std::clamp(Value01, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
}

void FLandscapeComponentSceneProxy::BuildHeightmapTexture(const ULandscapeComponent& Component, const FTerrainData& Data)
{
	// CellSize 드래그 중 다시 굽기가 얼마나 드는지 stat landscape에서 본다
	SCOPE_CYCLE_COUNTER(LandscapeCycleStats::STAT_LandscapeBakeHeightmap);

	// 정점 64x64 = 텍셀 64x64. 테두리가 없다: 법선을 CPU에서 옆 조각까지 보고 구워 넣기 때문이다.
	// UE와 같은 BGRA8 배치 (셰이더에서 .r .g .b .a로 읽는 이름 기준)
	//   R = 높이 상위 8bit
	//   G = 높이 하위 8bit      → 셰이더가 R*256 + G로 16bit 높이를 되살린다
	//   B = 월드 법선 X * 0.5 + 0.5
	//   A = 월드 법선 Y * 0.5 + 0.5  (Z는 셰이더가 sqrt(1 - X² - Y²)로 구한다)
	// 법선은 월드 비율(CellSize, HeightScale)로 굽는다. 그래서 두 값이 바뀌면 다시 구워야 한다
	const int32 Size0 = LandscapeHeightmapSize;
	const int32 NumMips = LandscapeNumLODs;
	const float Quads = static_cast<float>(LandscapeComponentQuads);
	// 높이(0~1) 기울기 → 월드 기울기. 칸 1개(CellSize cm) 갈 때 몇 cm 오르는가
	const float SlopeScale = Data.HeightScale / Data.CellSize;

	TArray<TArray<uint8>> Mips;
	Mips.SetNum(NumMips);

	for (int32 Mip = 0; Mip < NumMips; ++Mip)
	{
		const int32 Size = Size0 >> Mip;                 // 64, 32, 16, 8, 4, 2
		const float Step = Quads / (Size - 1);           // 이 밉 텍셀 사이 = LOD0 칸 1, 2.03, 4.2, 9, 21, 63
		TArray<uint8>& Texels = Mips[Mip];
		Texels.SetNum(Size * Size * 4);

		for (int32 TY = 0; TY < Size; ++TY)
		{
			for (int32 TX = 0; TX < Size; ++TX)
			{
				// 이 텍셀이 맡는 정점의 맵 전체 격자 좌표. 7단계 LOD k 격자의 정점 위치와 같다
				const float GX = Component.SectionBaseX + TX * Step;
				const float GY = Component.SectionBaseY + TY * Step;

				const float H = SampleHeight01(Data, GX, GY);

				// 중앙 차분. 간격 = 이 밉의 텍셀 간격 (거친 LOD일수록 넓게 보아 평균 경사가 된다).
				// 맵 끝에서는 안쪽 한쪽만 보도록 실제 거리로 나눈다
				const float MaxX = static_cast<float>(Data.SizeX - 1);
				const float MaxY = static_cast<float>(Data.SizeY - 1);
				const float XL = (std::max)(GX - Step, 0.0f), XR = (std::min)(GX + Step, MaxX);
				const float YD = (std::max)(GY - Step, 0.0f), YU = (std::min)(GY + Step, MaxY);
				const float DhDx = (SampleHeight01(Data, XR, GY) - SampleHeight01(Data, XL, GY)) / (std::max)(XR - XL, 1e-4f);
				const float DhDy = (SampleHeight01(Data, GX, YU) - SampleHeight01(Data, GX, YD)) / (std::max)(YU - YD, 1e-4f);

				// 월드 법선 (액터 회전 0 기준). (-dZ/dX, -dZ/dY, 1)을 정규화
				const float NX = -DhDx * SlopeScale;
				const float NY = -DhDy * SlopeScale;
				const float InvLen = 1.0f / std::sqrt(NX * NX + NY * NY + 1.0f);

				const uint32 H16 = static_cast<uint32>(std::clamp(H, 0.0f, 1.0f) * 65535.0f + 0.5f);

				// B8G8R8A8은 메모리 순서가 B, G, R, A다
				uint8* Out = &Texels[(TY * Size + TX) * 4];
				Out[0] = EncodeUNorm8(NX * InvLen * 0.5f + 0.5f);   // B
				Out[1] = static_cast<uint8>(H16 & 0xFF);             // G = 높이 하위
				Out[2] = static_cast<uint8>(H16 >> 8);               // R = 높이 상위
				Out[3] = EncodeUNorm8(NY * InvLen * 0.5f + 0.5f);   // A
			}
		}
	}

	// 초기 데이터를 주면 FTexture2D가 서브리소스 1개(밉 0)만 넘기는데,
	// D3D11은 MipLevels만큼 배열을 읽으므로 잘못된 메모리를 읽는다.
	// 그래서 빈 텍스처를 만든 뒤 밉마다 UpdateSubresource로 채운다 (Immutable은 초기 데이터가 꼭 필요해 Default로 둔다)
	FTextureDesc Desc;
	Desc.Width = Size0;
	Desc.Height = Size0;
	Desc.MipLevels = NumMips;
	Desc.Format = ETextureFormat::B8G8R8A8_UNorm;       // UE PF_B8G8R8A8과 같다
	Desc.BindFlags = ETextureBindFlags::ShaderResource;
	Desc.Usage = ETextureUsage::Default;
	HeightTexture = RenderCommand::CreateTexture2D(Desc, nullptr);
	if (!HeightTexture || !HeightTexture->GetRawPtr())
	{
		HeightTexture.reset();
		return;
	}

	ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();
	for (int32 Mip = 0; Mip < NumMips; ++Mip)
	{
		const uint32 RowPitch = static_cast<uint32>((Size0 >> Mip) * 4);
		Context->UpdateSubresource(HeightTexture->GetRawPtr(), static_cast<UINT>(Mip), nullptr,
			Mips[Mip].GetData(), RowPitch, 0);
	}
}

void FLandscapeComponentSceneProxy::GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View)
{
	if (!VertexFactory.IsValid() || !HeightTexture || !Material || !SharedBuffers)
		return;

	// 조각 하나 = 드로우 1개. 5단계는 LOD0 전체(63x63칸 = 7,938 삼각형)를 그린다. LOD는 7단계
	FRenderPacket& Packet = Queue.AddDefaulted_GetRef();
	Packet.Proxy = this;                 // 월드 행렬은 MeshPassProcessor가 Proxy->GetLocalToWorld()로 읽는다
	Packet.VertexFactory = &VertexFactory;
	Packet.Material = Material;
	Packet.StartIndex = 0;
	Packet.IndexCount = SharedBuffers->GetIndexCount();

	// 메인 뷰와 그림자 패스가 둘 다 여기로 온다. 그림자 패스(MeshPassProcessor)는 빈 FViewContext()를 넘기므로
	// 뷰 크기가 0이면 그림자로 센다
	if (View.Width > 0 && View.Height > 0)
		FLandscapeStats::AddMainDraw(0, Packet.IndexCount / 3);   // LOD는 7단계에서 실제 값으로
	else
		FLandscapeStats::AddShadowDraw();
}
