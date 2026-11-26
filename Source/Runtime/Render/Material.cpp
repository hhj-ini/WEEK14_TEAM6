#include "EnginePCH.h"

#include "RenderCommand.h"

#include "Material.h"

#include "Asset/AssetManager.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/TypeSerializer.h"
#include "Serialization/MathSerialization.h"

namespace
{
	// 저장된 경로로 텍스처를 찾고, 아직 로드 전이면 로드한다
	UTexture2D* FindOrLoadTexture(const FString& Path)
	{
		if (Path.empty())
		{
			return nullptr;
		}
		if (UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}
}

const char* MaterialDomainToString(EMaterialDomain Domain)
{
	switch (Domain)
	{
	case EMaterialDomain::Particle: return "Particle";
	case EMaterialDomain::Decal:    return "Decal";
	case EMaterialDomain::UI:       return "UI";
	case EMaterialDomain::Surface:
	default:                        return "Surface";
	}
}

EMaterialDomain StringToMaterialDomain(const FString& Str)
{
	if (Str == "Particle") return EMaterialDomain::Particle;
	if (Str == "Decal")    return EMaterialDomain::Decal;
	if (Str == "UI")       return EMaterialDomain::UI;
	return EMaterialDomain::Surface;
}

const char* MaterialBlendModeToString(EMaterialBlendMode Mode)
{
	switch (Mode)
	{
	case EMaterialBlendMode::Masked:      return "Masked";
	case EMaterialBlendMode::Translucent: return "Translucent";
	case EMaterialBlendMode::Additive:    return "Additive";
	case EMaterialBlendMode::Opaque:
	default:                              return "Opaque";
	}
}

EMaterialBlendMode StringToMaterialBlendMode(const FString& Str)
{
	if (Str == "Masked")      return EMaterialBlendMode::Masked;
	if (Str == "Translucent") return EMaterialBlendMode::Translucent;
	if (Str == "Additive")    return EMaterialBlendMode::Additive;
	return EMaterialBlendMode::Opaque;
}

const char* MaterialShadingModelToString(EMaterialShadingModel Model)
{
	switch (Model)
	{
	case EMaterialShadingModel::Unlit:      return "Unlit";
	case EMaterialShadingModel::DefaultLit:
	default:                                return "DefaultLit";
	}
}

EMaterialShadingModel StringToMaterialShadingModel(const FString& Str)
{
	if (Str == "Unlit") return EMaterialShadingModel::Unlit;
	return EMaterialShadingModel::DefaultLit;
}

const char* SamplerStateToString(ESamplerState State)
{
	switch (State)
	{
	case ESamplerState::LinearWrap:        return "LinearWrap";
	case ESamplerState::PointClamp:        return "PointClamp";
	case ESamplerState::LinearWrapUClampV: return "LinearWrapUClampV";
	case ESamplerState::LinearClamp:
	default:                               return "LinearClamp";
	}
}

ESamplerState StringToSamplerState(const FString& Str)
{
	if (Str == "LinearWrap")        return ESamplerState::LinearWrap;
	if (Str == "PointClamp")        return ESamplerState::PointClamp;
	if (Str == "LinearWrapUClampV") return ESamplerState::LinearWrapUClampV;
	return ESamplerState::LinearClamp;
}

void UMaterial::CopyPropertiesFrom(const UMaterial& Source)
{
	ParamLayout = Source.ParamLayout;
	Shader = Source.Shader;
	Textures = Source.Textures;
	BlendState = Source.BlendState;
	DepthStencilState = Source.DepthStencilState;
	SamplerState = Source.SamplerState;
	PSOType = Source.PSOType;

	MaterialDomain = Source.MaterialDomain;
	BlendMode = Source.BlendMode;
	ShadingModel = Source.ShadingModel;
	bTwoSided = Source.bTwoSided;
	OpacityMaskClipValue = Source.OpacityMaskClipValue;

	BaseColor = Source.BaseColor;
	SpecularColor = Source.SpecularColor;
	Shininess = Source.Shininess;
	UVScrollSpeed = Source.UVScrollSpeed;
	bFlipNormalY = Source.bFlipNormalY;
}

void UMaterial::SyncLegacyStates()
{
	switch (BlendMode)
	{
	case EMaterialBlendMode::Translucent:
		BlendState = EBlendState::AlphaBlend;
		DepthStencilState = EDepthStencilState::ReadOnly;
		if (PSOType != EPSOType::Billboard)
		{
			PSOType = (MaterialDomain == EMaterialDomain::Particle)
				? EPSOType::Particle_AlphaBlend
				: EPSOType::StaticMesh_Translucent;
		}
		break;
	case EMaterialBlendMode::Additive:
		BlendState = EBlendState::ParticleAdditive;
		DepthStencilState = EDepthStencilState::ReadOnly;
		if (PSOType != EPSOType::Billboard)
		{
			PSOType = (MaterialDomain == EMaterialDomain::Particle)
				? EPSOType::Particle_Additive
				: EPSOType::StaticMesh_Additive;
		}
		break;
	case EMaterialBlendMode::Masked:
	case EMaterialBlendMode::Opaque:
	default:
		BlendState = EBlendState::Opaque;
		DepthStencilState = EDepthStencilState::Default;
		if (PSOType != EPSOType::Billboard)
		{
			PSOType = EPSOType::StaticMesh_Opaque;
		}
		break;
	}
}

UTexture2D* UMaterial::GetTexture(int32 Slot) const
{
	if (Slot < 0 || Slot >= Textures.Num())
	{
		return nullptr;
	}
	return Textures[Slot];
}

void UMaterial::SetTexture(int32 Slot, UTexture2D* Texture)
{
	if (Slot < 0)
	{
		return;
	}
	while (Textures.Num() <= Slot)
	{
		Textures.Add(nullptr);
	}
	Textures[Slot] = Texture;
}

UMaterial* UMaterial::CreateInstance(const UMaterial* Source)
{
	if (Source == nullptr)
	{
		return nullptr;
	}

	UMaterial* Instance = NewObject<UMaterial>();
	Instance->CopyPropertiesFrom(*Source);

	// ParamBuffer는 TUniquePtr라 복사할 수 없다.
	// 원본이 갖고 있으면 같은 크기로 새로 만들어 준다. 내용은 매 프레임 갱신되므로 옮기지 않는다.
	if (Source->ParamBuffer)
	{
		Instance->ParamBuffer = RenderCommand::CreateConstantBuffer(Source->ParamBuffer->GetBufferSize());
	}

	Instance->bIsInstance = true;
	Instance->Parent = Source;

	return Instance;
}

const UMaterial* UMaterial::GetBaseAsset() const
{
	for (const UMaterial* Current = this; Current; Current = Current->Parent)
	{
		if (!Current->GetPath().empty())
		{
			return Current;
		}
	}
	return nullptr;
}

void UMaterial::SerializeMaterialReference(FStructuredArchive::FSlot Slot, UMaterial*& Material)
{
	FArchive& Ar = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	// ── 에셋 참조 ──
	// 저장과 불러오기가 같은 순서로 같은 함수를 부르게 TryEnterField를 쓴다 (바이너리 포맷에서도 맞도록)
	const bool bIsAsset = Ar.IsSaving() && Material && !Material->GetPath().empty();
	if (TOptional<FStructuredArchive::FSlot> AssetSlot = Record.TryEnterField("Asset", bIsAsset))
	{
		FString Path = Ar.IsSaving() ? Material->GetPath() : FString();
		AssetSlot.GetValue() << Path;
		if (Ar.IsLoading())
		{
			Material = UAssetManager::GetAssetByPath<UMaterial>(Path);
		}
		return;
	}

	// ── 인스턴스 ──
	const bool bIsInstance = Ar.IsSaving() && Material != nullptr;
	TOptional<FStructuredArchive::FSlot> BaseSlot = Record.TryEnterField("Base", bIsInstance);
	if (!BaseSlot.IsSet())
	{
		if (Ar.IsLoading())
			Material = nullptr;     // 빈 Record = 머티리얼 없음
		return;
	}

	FString BasePath;
	if (Ar.IsSaving())
	{
		const UMaterial* Base = Material->GetBaseAsset();
		BasePath = Base ? Base->GetPath() : FString("DefaultMaterial");
	}
	BaseSlot.GetValue() << BasePath;

	if (Ar.IsLoading())
	{
		UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>(BasePath);
		if (!Base)
		{
			KRS_LOG(Warning, "Load: base material '{}' not found, using DefaultMaterial", BasePath);
			Base = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		}
		Material = CreateInstance(Base);   // 먼저 만들고
	}

	if (Material)
		Material->Serialize(Record.EnterRecord("Instance"));   // 그다음 값을 채운다
}

void UMaterial::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	// ── 머티리얼 상태 속성 (신규 필드: TryEnterField로 옛 씬 파일과 하위 호환) ──
	bool bHasExplicitBlendMode = false;
	bool bHasExplicitDomain = false;

	if (TOptional<FStructuredArchive::FSlot> DomainSlot = Record.TryEnterField("MaterialDomain", true))
	{
		FString DomainStr = Ar.IsSaving() ? FString(MaterialDomainToString(MaterialDomain)) : FString();
		DomainSlot.GetValue() << DomainStr;
		if (Ar.IsLoading())
		{
			MaterialDomain = StringToMaterialDomain(DomainStr);
			bHasExplicitDomain = true;
		}
	}

	if (TOptional<FStructuredArchive::FSlot> BlendModeSlot = Record.TryEnterField("BlendMode", true))
	{
		FString BlendModeStr = Ar.IsSaving() ? FString(MaterialBlendModeToString(BlendMode)) : FString();
		BlendModeSlot.GetValue() << BlendModeStr;
		if (Ar.IsLoading())
		{
			BlendMode = StringToMaterialBlendMode(BlendModeStr);
			bHasExplicitBlendMode = true;
		}
	}

	if (TOptional<FStructuredArchive::FSlot> ShadingSlot = Record.TryEnterField("ShadingModel", true))
	{
		FString ShadingStr = Ar.IsSaving() ? FString(MaterialShadingModelToString(ShadingModel)) : FString();
		ShadingSlot.GetValue() << ShadingStr;
		if (Ar.IsLoading())
		{
			ShadingModel = StringToMaterialShadingModel(ShadingStr);
		}
	}

	if (TOptional<FStructuredArchive::FSlot> TwoSidedSlot = Record.TryEnterField("TwoSided", true))
	{
		TwoSidedSlot.GetValue() << bTwoSided;
	}

	if (TOptional<FStructuredArchive::FSlot> ClipSlot = Record.TryEnterField("OpacityMaskClipValue", true))
	{
		ClipSlot.GetValue() << OpacityMaskClipValue;
	}

	// ── 파라미터 ──
	Record << SA_VALUE("BaseColor", BaseColor)
		<< SA_VALUE("SpecularColor", SpecularColor)
		<< SA_VALUE("Shininess", Shininess)
		<< SA_VALUE("UVScrollSpeed", UVScrollSpeed)
		<< SA_VALUE("FlipNormalY", bFlipNormalY);

	FString Sampler = SamplerStateToString(SamplerState);
	Record << SA_VALUE("SamplerState", Sampler);
	if (Ar.IsLoading())
	{
		SamplerState = StringToSamplerState(Sampler);
	}

	// ── 레거시 필드 읽기 전용 호환 (옛 씬 파일의 BlendState / PSOType을 BlendMode / Domain으로 이관) ──
	if (TOptional<FStructuredArchive::FSlot> LegacyBlendSlot = Record.TryEnterField("BlendState", false))
	{
		FString LegacyBlend;
		LegacyBlendSlot.GetValue() << LegacyBlend;
		if (Ar.IsLoading() && !bHasExplicitBlendMode)
		{
			if (LegacyBlend == "AlphaBlend")
			{
				BlendMode = EMaterialBlendMode::Translucent;
			}
			else if (LegacyBlend == "Additive" || LegacyBlend == "ParticleAdditive")
			{
				BlendMode = EMaterialBlendMode::Additive;
			}
			else
			{
				BlendMode = EMaterialBlendMode::Opaque;
			}
		}
	}

	if (TOptional<FStructuredArchive::FSlot> LegacyPSOSlot = Record.TryEnterField("PSOType", false))
	{
		uint8 PSOVal = static_cast<uint8>(PSOType);
		LegacyPSOSlot.GetValue() << PSOVal;
		if (Ar.IsLoading())
		{
			const EPSOType LoadedPSO = static_cast<EPSOType>(PSOVal);
			if (LoadedPSO == EPSOType::Billboard)
			{
				PSOType = EPSOType::Billboard;
			}
			if (!bHasExplicitBlendMode)
			{
				if (LoadedPSO == EPSOType::StaticMesh_Additive || LoadedPSO == EPSOType::Particle_Additive)
				{
					BlendMode = EMaterialBlendMode::Additive;
				}
				else if (LoadedPSO == EPSOType::StaticMesh_Translucent || LoadedPSO == EPSOType::Particle_AlphaBlend)
				{
					BlendMode = EMaterialBlendMode::Translucent;
				}
			}
			if (!bHasExplicitDomain && (LoadedPSO == EPSOType::Particle_Additive || LoadedPSO == EPSOType::Particle_AlphaBlend))
			{
				MaterialDomain = EMaterialDomain::Particle;
			}
		}
	}

	// 텍스처: 경로 문자열 배열. 없는 칸은 빈 문자열
	int32 Num = Textures.Num();
	FStructuredArchive::FArray TextureArray = Record.EnterArray("Textures", Num);
	if (Ar.IsLoading())
	{
		Textures.Reset();
	}

	for (int32 i = 0; i < Num; ++i)
	{
		FString Path;
		if (Ar.IsSaving() && Textures[i])
		{
			Path = Textures[i]->GetPath();
		}

		TextureArray.EnterElement() << Path;

		if (Ar.IsLoading())
		{
			Textures.Add(Path.empty() ? nullptr : FindOrLoadTexture(Path));
		}
	}

	if (Ar.IsLoading())
	{
		SyncLegacyStates();
	}
}
