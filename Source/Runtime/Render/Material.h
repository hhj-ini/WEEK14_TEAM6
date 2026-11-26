#pragma once

#include "MaterialInterface.h"
#include "RenderResourceManager.h"
#include "Render/RenderStates.h"
#include "Render/Texture2D.h"
#include "Render/Buffer.h"
#include "Material.generated.h"

class FShader;

enum class EMaterialParamLayout
{
	None,
	StaticMesh,
	Billboard
};

enum class EMaterialDomain : uint8
{
	Surface,
	Particle,
	Decal,
	UI
};

enum class EMaterialBlendMode : uint8
{
	Opaque,
	Masked,
	Translucent,
	Additive
};

enum class EMaterialShadingModel : uint8
{
	DefaultLit,
	Unlit
};

const char* MaterialDomainToString(EMaterialDomain Domain);
EMaterialDomain StringToMaterialDomain(const FString& Str);

const char* MaterialBlendModeToString(EMaterialBlendMode Mode);
EMaterialBlendMode StringToMaterialBlendMode(const FString& Str);

const char* MaterialShadingModelToString(EMaterialShadingModel Model);
EMaterialShadingModel StringToMaterialShadingModel(const FString& Str);

const char* SamplerStateToString(ESamplerState State);
ESamplerState StringToSamplerState(const FString& Str);

struct FStaticMeshMaterialParams
{
	FVector4 BaseColor;
	FVector2 UVOffset;
	FVector2 TextureFlags; // x: 컬러 텍스처 사용 여부(Translucent) 또는 노멀 인코딩, y: 노멀 반전
};
static_assert(sizeof(FStaticMeshMaterialParams) == 32);

UCLASS()
class UMaterial : public UMaterialInterface
{
	GENERATED_BODY()

public:
	UMaterial() : SortID(NextSortID++) {};
	virtual ~UMaterial() override = default;

	EMaterialParamLayout ParamLayout = EMaterialParamLayout::None;
	FShaderProgram* Shader = nullptr;
	TArray<UTexture2D*> Textures;
	TUniquePtr<FConstantBuffer> ParamBuffer;
	EPSOType PSOType = EPSOType::StaticMesh_Opaque;
	EBlendState BlendState = EBlendState::Opaque;
	EDepthStencilState DepthStencilState = EDepthStencilState::Default;
	ESamplerState SamplerState = ESamplerState::LinearClamp;

	// 머티리얼 렌더링 속성 (그리는 쪽 FMeshPassProcessor가 이 속성으로 PSO와 셰이더를 결정한다)
	EMaterialDomain MaterialDomain = EMaterialDomain::Surface;
	EMaterialBlendMode BlendMode = EMaterialBlendMode::Opaque;
	EMaterialShadingModel ShadingModel = EMaterialShadingModel::DefaultLit;
	bool bTwoSided = false;
	float OpacityMaskClipValue = 0.333f;

	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(1.0f, 1.0f, 1.0f);
	float Shininess = 32.0f;
	FVector2 UVScrollSpeed = FVector2(0.0f, 0.0f);
	bool bFlipNormalY = false;

	bool bIsInstance = false;

	// 인스턴스가 복제된 원본. 인스턴스는 경로가 없어서, 저장할 때 원본을 따라가 기준 에셋을 찾는다
	const UMaterial* Parent = nullptr;

	// Source의 내용을 복사한 편집용 복제본을 만든다
	static UMaterial* CreateInstance(const UMaterial* Source);

	// 다른 머티리얼의 속성을 복사한다 (Path, SortID, bIsInstance, Parent는 유지)
	void CopyPropertiesFrom(const UMaterial& Source);

	// BlendMode / MaterialDomain에 맞춰 레거시 상태(BlendState, DepthStencilState, PSOType)를 동기화한다
	void SyncLegacyStates();

	bool IsTranslucent() const
	{
		return BlendMode == EMaterialBlendMode::Translucent || BlendMode == EMaterialBlendMode::Additive;
	}
	bool IsMasked() const { return BlendMode == EMaterialBlendMode::Masked; }
	bool IsOpaqueOrMasked() const { return !IsTranslucent(); }
	bool IsUnlit() const { return ShadingModel == EMaterialShadingModel::Unlit; }

	UTexture2D* GetTexture(int32 Slot) const;
	void SetTexture(int32 Slot, UTexture2D* Texture);
	UTexture2D* GetBaseColorTexture() const { return GetTexture(0); }
	UTexture2D* GetNormalTexture() const { return GetTexture(1); }
	void SetBaseColorTexture(UTexture2D* Texture) { SetTexture(0, Texture); }
	void SetNormalTexture(UTexture2D* Texture) { SetTexture(1, Texture); }

	// 경로가 있는(=에셋으로 등록된) 가장 가까운 원본. 자신이 에셋이면 자신
	const UMaterial* GetBaseAsset() const;

	const uint16 SortID;

	using Super::Serialize;
	// 머티리얼의 속성/파라미터. 객체는 이미 만들어져 있어야 한다.
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	// 머티리얼 참조 하나를 Slot에 읽고 쓴다.
	// 에셋이면 경로만, 인스턴스면 기준 에셋 + 값을 그 자리에 쓰고, 불러올 때 인스턴스를 새로 만든다.
	// null이면 빈 Record({})로 기록되고, 불러오면 nullptr이 된다.
	static void SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material);

private:
	inline static uint16 NextSortID = 0;
};
