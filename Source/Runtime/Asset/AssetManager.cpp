#include "EnginePCH.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/MaterialFile.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Render/SkeletalMesh.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectGlobals.h"

#include "Audio/SoundWave.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemFile.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicalMaterialFile.h"
#include "Asset/AssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Asset/LandscapeHeightmap.h"
#include "stb_image.h"   // 구현(STB_IMAGE_IMPLEMENTATION)은 ImageLoader.cpp에 있다

#include "Render/ImageLoader.h"
#include "Asset/AssetLoadProfiler.h"
#include "Core/Misc/Paths.h"
#include <cctype>


namespace
{
	FString GetAssetExtension(const FString& Path) {
		const std::u8string UTF8Path{ Path.begin(), Path.end() };
		FString Extension{ std::filesystem::path{ UTF8Path }.extension().string() };
		for (char& Character : Extension) {
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		return Extension;
	}

	EAssetType GetAssetTypeFromPath(const FString& Path) {
		const FString Extension{ GetAssetExtension(Path) };
		if (Extension == ".png" || Extension == ".dds") {
			return EAssetType::Texture;
		}
		if (Extension == ".json") {
			return EAssetType::Font;
		}
		if (Extension == ".obj") {
			return EAssetType::StaticMesh;
		}
		if (Extension == ".bin" && FFbxMeshBake::IsBakedPath(Path)) {
			// 파일을 읽지 않고 구분하도록 FFbxImporter의 저장 이름 규칙을 쓴다
			return Path.ends_with("_Static.fbx.bin") ? EAssetType::StaticMesh : EAssetType::SkeletalMesh;
		}
		if (Extension == ".mat") {
			return EAssetType::Material;
		}
		return EAssetType::None;
	}

	// 엔진 콘텐츠로 옮긴 파일의 옛 키("Icons/Point.png")를 엔진 키("Engine/Icons/Point.png")로 바꾼다.
	// 옛 씬은 "Content/Game/Icons/..."를 저장하고 있으므로, 프로젝트에 그 파일이 없을 때 엔진 콘텐츠에서 찾는다.
	FString MakeEngineFallbackKey(const FString& Key)
	{
		if (Key.starts_with(FPaths::EngineContentPrefix)) return {};
		return FPaths::EngineContentPrefix + Key;
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData, const FString& OwnerMeshPath = {})
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return nullptr;

		FLoadStageTimer CopyTimer{ "Mesh/CopyData" };
		FStaticMeshData Optimized = *MeshData;
		CopyTimer.Stop();

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		{
			FLoadStageTimer Timer{ "Mesh/BuildTangentSpace" };
			Optimized.BuildTangentSpace();
		}
		{
			FLoadStageTimer Timer{ "Mesh/OptimizeTriangleOrder" };
			Optimized.OptimizeTriangleOrderForVertexCache();
		}

		FLoadStageTimer BufferTimer{ "Mesh/CreateGPUBuffers" };
		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertex) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		BufferTimer.Stop();
		if (VB == nullptr || IB == nullptr) return nullptr;

		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		Mesh->MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);

		// 빈 슬롯이면 위에서 Default 슬롯을 넣었으므로 원본(MeshData)이 아니라 Mesh->MeshData를 쓴다
		FLoadStageTimer MaterialTimer{ "Mesh/BuildMaterials (includes texture loads)" };
		if (!UAssetManager::BuildMaterialsFromSlots(Mesh->MeshData.MaterialSlots, Mesh->Materials, OwnerMeshPath))
		{
			return nullptr;
		}
		MaterialTimer.Stop();

		{
			FLoadStageTimer Timer{ "Mesh/BuildTriangleBVH" };
			Mesh->MeshData.BuildTriangleBVH();
		}
		{
			FLoadStageTimer Timer{ "Mesh/BuildOBBTree" };
			Mesh->MeshData.BuildOBBTree();
		}
		{
			FLoadStageTimer Timer{ "Mesh/RebuildRenderData" };
			Mesh->RebuildRenderData();
		}

		return Mesh;
	}

}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	{
		FLoadStageTimer Timer{ "Init/Defaults before scan" };
		FGeometryGenerator::CreateDefaultMeshDatas();
		// 머티리얼이 참조하므로 반드시 먼저 만든다
		Get().CreateDefaultTextures();
		Get().CreateDefaultMaterial();
	}
	{
		FLoadStageTimer Timer{ "Init/ScanAssets" };
		Get().ScanAssets(FPaths::ProjectContentDir(), OnProgress);
		// 엔진 콘텐츠는 엔진 폴더에 있고 키는 "Engine/..."이다.
		Get().ScanAssets(FPaths::EngineResourcesDir(), OnProgress, FPaths::EngineContentPrefix);
	}
	FLoadStageTimer DefaultsTimer{ "Init/Defaults after scan" };
	Get().CreateDefaultMeshes();
	Get().CreateBillboardMaterial();
	Get().CreateParticleMaterial();
	Get().CreateEditorArrowMaterial();
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress, const FString& KeyPrefix)
{
	if (!fs::exists(AssetRoot))
	{
		KRS_LOG(Error, "Asset root not found: {}", AssetRoot.generic_string());
		return;
	}

	FLoadStageTimer EnumerateTimer{ "Scan/EnumerateFiles" };
	TArray<fs::path> Files;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(AssetRoot))
	{
		if (Entry.is_regular_file())
		{
			Files.Add(Entry.path());
		}
	}
	EnumerateTimer.Stop();

	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		FString Key = FPaths::ToUtf8(fs::relative(Files[i], AssetRoot));
		FString Path = FPaths::ToUtf8(Files[i]);
		if (!KeyPrefix.empty())
		{
			// 엔진 콘텐츠는 경로도 "Engine/..." 그대로 둔다. 파일을 열 때 FPaths::ResolveContentPath로 바꾼다.
			// 이렇게 해야 로드 뒤 RegisterAsset이 같은 키로 등록하고, 씬에도 "Engine/..."으로 저장된다.
			Key = KeyPrefix + FPaths::ToUtf8(Files[i].lexically_relative(AssetRoot));
			Path = Key;
		}

		// 경로만 등록한다. 실제 로드는 처음 찾을 때 FindOrLoadAsset에서 한다.
		if (GetAssetTypeFromPath(Path) != EAssetType::None)
		{
			AssetPathMap.Add(Key, Path);
		}

		if (OnProgress)
		{
			FLoadStageTimer Timer{ "Scan/OnProgress (splash)" };
			OnProgress(i + 1, Total, Key);
		}
	}
}

void UAssetManager::LoadAsset(const FString& Key, const FString& Path) {
	// FBX Asset은 원본 .fbx 대신 Import로 저장한 .fbx.bin만 로드한다.
	// 원본 해석과 변경 여부 비교는 FFbxImporter::ImportToAssets에서만 수행하며,
	// 이 로드 경로는 원본 파일이나 FBX SDK 없이 저장된 데이터로 렌더 자원을 만든다.
	if (FFbxMeshBake::IsBakedPath(Path)) {
		FString Error;
		if (!LoadFbxMesh(Path, Error)) KRS_LOG(Error, "{}", Error);
		return;
	}

	switch (GetAssetTypeFromPath(Path)) {
	case EAssetType::Texture:
		LoadTexture(Path);
		break;
	case EAssetType::Font:
		LoadFontAtlas(Path, fs::path{ Path }.replace_extension(".png").string());
		break;
	case EAssetType::StaticMesh:
		LoadObjStaticMesh(Path);
		break;
	case EAssetType::Material:
		LoadMaterial(Path);
		break;
	default:
		break;
	}
}

URenderAsset* UAssetManager::FindOrLoadAsset(const FString& Path)
{
	// 렌더링 중에도 불리는 내장 에셋("DefaultMaterial" 등)은 키를 만들지 않고 바로 찾는다
	if (URenderAsset** Found = AssetMap.Find(Path))
	{
		return *Found;
	}

	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (URenderAsset** Found = AssetMap.Find(Key))
	{
		return *Found;
	}

	const FString* FoundSource = AssetPathMap.Find(Key);
	if (FoundSource == nullptr)
	{
		// 프로젝트에 없으면 엔진 콘텐츠에서 찾는다. 프로젝트에 같은 경로의 파일이 있으면 그쪽이 먼저다.
		const FString EngineKey = MakeEngineFallbackKey(Key);
		if (!EngineKey.empty() && AssetPathMap.Contains(EngineKey))
		{
			return FindOrLoadAsset(EngineKey);
		}
		return nullptr;
	}
	const FString SourcePath = *FoundSource;

	const uint64 LoadStart = FPlatformTime::GetCycles64();
	LoadAsset(Key, SourcePath);
	const double LoadMs = FPlatformTime::ToMilliseconds64(FPlatformTime::GetCycles64() - LoadStart);
	FAssetLoadProfiler::AddFile(SourcePath, LoadMs);
	FAssetLoadProfiler::AddStage("Lazy/LoadAsset " + GetAssetExtension(SourcePath), LoadMs);

	URenderAsset** Loaded = AssetMap.Find(Key);
	if (Loaded == nullptr)
	{
		// 찾을 때마다 파일을 다시 읽지 않도록 등록을 지운다
		KRS_LOG(Warning, "Asset load failed: {}", SourcePath);
		AssetPathMap.Remove(Key);
		return nullptr;
	}
	return *Loaded;
}

URenderAsset* UAssetManager::FindLoadedAsset(const FString& Path)
{
	URenderAsset** Found = Get().AssetMap.Find(FAssetUtils::GetAssetKey(Path));
	return Found ? *Found : nullptr;
}

EAssetType UAssetManager::GetUnloadedAssetType(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (Get().AssetMap.Contains(Key) || !Get().AssetPathMap.Contains(Key))
	{
		return EAssetType::None;
	}
	return GetAssetTypeFromPath(Key);
}

TArray<FString> UAssetManager::GetUnloadedAssetPaths(EAssetType Type)
{
	TArray<FString> Paths;
	for (const auto& [Key, Path] : Get().AssetPathMap)
	{
		if (GetAssetTypeFromPath(Key) == Type && !Get().AssetMap.Contains(Key))
		{
			Paths.Add(Path);
		}
	}
	return Paths;
}

void UAssetManager::CreateDefaultTextures() {
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel{ 0xFFFFFFFF };

	FTextureDesc Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
	Desc.Usage = ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource;

	TUniquePtr<FTexture2D> Resource{ RenderCommand::CreateTexture2D(Desc, &WhitePixel) };
	if (!Resource) {
		return;
	}

	UTexture2D* WhiteTexture{ NewObject<UTexture2D>() };
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	const FVector4 NormalPixel{ 0.5f, 0.5f, 1.0f, 1.0f };
	Desc.Format = ETextureFormat::R32G32B32A32_Float;
	TUniquePtr<FTexture2D> NormalResource{ RenderCommand::CreateTexture2D(Desc, &NormalPixel) };
	if (!NormalResource) {
		return;
	}

	UTexture2D* FlatNormalTexture{ NewObject<UTexture2D>() };
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");
	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* TorusData = FGeometryGenerator::GetMeshData("Torus");
	RegisterAsset("Torus", CreateStaticMesh(TorusData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	// Physics Asset 에디터의 바디·조인트 표시용
	for (const char* Name : { "PhysicsHemisphere", "PhysicsCylinder", "PhysicsFan" })
	{
		RegisterAsset(Name, CreateStaticMesh(FGeometryGenerator::GetMeshData(Name)));
	}

	const FBillboardVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FBillboardVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertex Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("BillboardQuad", Mesh);
}

void UAssetManager::CreateDefaultMaterial() {
	UMaterial* DefaultMat{ NewObject<UMaterial>() };
	DefaultMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/UberLit.hlsl");
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));
	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	DefaultMat->MaterialDomain = EMaterialDomain::Surface;
	DefaultMat->BlendMode = EMaterialBlendMode::Opaque;
	DefaultMat->ShadingModel = EMaterialShadingModel::DefaultLit;
	DefaultMat->bTwoSided = false;
	DefaultMat->SyncLegacyStates();
	RegisterAsset("DefaultMaterial", DefaultMat);
}

void UAssetManager::CreateBillboardMaterial()
{
	UMaterial* BillboardMat = NewObject<UMaterial>();
	BillboardMat->PSOType = EPSOType::Billboard;
	BillboardMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	BillboardMat->MaterialDomain = EMaterialDomain::Surface;
	BillboardMat->BlendMode = EMaterialBlendMode::Translucent;
	BillboardMat->ShadingModel = EMaterialShadingModel::Unlit;
	BillboardMat->BlendState = EBlendState::AlphaBlend;
	BillboardMat->DepthStencilState = EDepthStencilState::ReadOnly;
	BillboardMat->SamplerState = ESamplerState::LinearClamp;
	BillboardMat->ParamLayout = EMaterialParamLayout::Billboard;
	RegisterAsset("BillboardMaterial", BillboardMat);
}

void UAssetManager::CreateParticleMaterial()
{
	auto MakeSpriteMaterial = [this](EPSOType PSOType, EBlendState Blend, EMaterialBlendMode Mode, const char* Name)
		{
			UMaterial* Mat = NewObject<UMaterial>();
			Mat->PSOType = PSOType;
			Mat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/ParticleSpriteShader.hlsl");
			Mat->Textures.Add(GetAssetByPath<UTexture2D>("Engine/SubUV/StarParticle.png"));
			Mat->MaterialDomain = EMaterialDomain::Particle;
			Mat->BlendMode = Mode;
			Mat->ShadingModel = EMaterialShadingModel::Unlit;
			Mat->bTwoSided = true;
			Mat->BlendState = Blend;
			Mat->DepthStencilState = EDepthStencilState::ReadOnly;
			Mat->SamplerState = ESamplerState::LinearClamp;
			RegisterAsset(Name, Mat);
		};
	MakeSpriteMaterial(EPSOType::Particle_Additive, EBlendState::ParticleAdditive, EMaterialBlendMode::Additive, "ParticleSpriteAdditive");
	MakeSpriteMaterial(EPSOType::Particle_AlphaBlend, EBlendState::AlphaBlend, EMaterialBlendMode::Translucent, "ParticleSpriteAlphaBlend");

	// 빔, 리본용. U(길이 방향)는 반복, V(폭 방향)는 가장자리가 번지지 않게 Clamp
	// 셰이더는 스프라이트 것을 두어야 Trail 정점 타입일 때 Trail 셰이더로 바뀐다
	UMaterial* BeamMat = NewObject<UMaterial>();
	BeamMat->PSOType = EPSOType::Particle_Additive;
	BeamMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/ParticleSpriteShader.hlsl");
	BeamMat->Textures.Add(GetAssetByPath<UTexture2D>("Engine/Textures/BeamGlow.png"));
	BeamMat->MaterialDomain = EMaterialDomain::Particle;
	BeamMat->BlendMode = EMaterialBlendMode::Additive;
	BeamMat->ShadingModel = EMaterialShadingModel::Unlit;
	BeamMat->bTwoSided = true;
	BeamMat->BlendState = EBlendState::ParticleAdditive;
	BeamMat->DepthStencilState = EDepthStencilState::ReadOnly;
	BeamMat->SamplerState = ESamplerState::LinearWrapUClampV;
	RegisterAsset("ParticleBeamAdditive", BeamMat);

	// 메시 에미터의 머티리얼 오버라이드용. 스프라이트 셰이더는 메시 파티클 정점을 받지 못한다
	auto MakeMeshMaterial = [this](EPSOType PSOType, EBlendState Blend, EMaterialBlendMode Mode, const char* Name)
		{
			UMaterial* Mat = NewObject<UMaterial>();
			Mat->PSOType = PSOType;
			Mat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/StaticMeshTranslucentShader.hlsl");
			Mat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
			Mat->MaterialDomain = EMaterialDomain::Particle;
			Mat->BlendMode = Mode;
			Mat->ShadingModel = EMaterialShadingModel::Unlit;
			Mat->BlendState = Blend;
			Mat->DepthStencilState = EDepthStencilState::ReadOnly;
			Mat->SamplerState = ESamplerState::LinearClamp;
			Mat->ParamLayout = EMaterialParamLayout::StaticMesh;
			Mat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
			RegisterAsset(Name, Mat);
		};
	MakeMeshMaterial(EPSOType::StaticMesh_Translucent, EBlendState::AlphaBlend, EMaterialBlendMode::Translucent, "ParticleMeshTranslucent");
	MakeMeshMaterial(EPSOType::StaticMesh_Additive, EBlendState::ParticleAdditive, EMaterialBlendMode::Additive, "ParticleMeshAdditive");
}

void UAssetManager::CreateEditorArrowMaterial() {
	UMaterial* BaseMaterial{ GetAssetByPath<UMaterial>("DefaultMaterial") };
	UMaterial* ArrowMaterial{ UMaterial::CreateInstance(BaseMaterial) };
	ArrowMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	RegisterAsset("EditorArrowMaterial", ArrowMaterial);
}

void UAssetManager::Shutdown()
{
	Get().AssetMap.Empty();
	Get().SoundMap.Empty();
	Get().AnimSequenceMap.Empty();
	Get().BlendSpaceMap.Empty();
	for (auto& Pair : Get().ParticleSystemMap)
	{
		delete Pair.second;
	}
	Get().ParticleSystemMap.Empty();
	// PhysX SDK 정리(FPhysicsCore::Shutdown)보다 먼저 불리므로 PxMaterial도 여기서 안전하게 풀린다.
	for (auto& Pair : Get().PhysicalMaterialMap)
	{
		delete Pair.second;
	}
	Get().PhysicalMaterialMap.Empty();
	for (auto& Pair : Get().LandscapeHeightmapMap)
	{
		delete Pair.second;
	}
	Get().LandscapeHeightmapMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, URenderAsset* Asset)
{
	// 경로 표기는 씬 저장 형식이 바뀌지 않도록 받은 그대로 둔다
	Asset->SetPath(Key);
	AssetMap[FAssetUtils::GetAssetKey(Key)] = Asset;
}

void UAssetManager::RegisterAssetPath(const FString& Key, const FString& FullPath)
{
	const FString NormalizedKey = FAssetUtils::GetAssetKey(Key);
	if (!NormalizedKey.empty() && !FullPath.empty())
	{
		AssetPathMap[NormalizedKey] = FullPath;
	}
}

UMaterial* UAssetManager::LoadMaterial(const FString& InPath)
{
	const FString Key = FAssetUtils::GetAssetKey(InPath);
	if (Key.empty())
	{
		return nullptr;
	}
	if (URenderAsset** Found = AssetMap.Find(Key))
	{
		return Cast<UMaterial>(*Found);
	}

	const FString ResolvedPath = FAssetUtils::ResolveAssetFilePath(InPath);
	const fs::path FilePath = FPaths::FromUtf8(ResolvedPath.empty() ? InPath : ResolvedPath);

	UMaterial* Material = NewObject<UMaterial>();
	Material->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/UberLit.hlsl");
	Material->ParamLayout = EMaterialParamLayout::StaticMesh;
	Material->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	Material->SamplerState = ESamplerState::LinearWrap;

	FString Error;
	if (!MaterialFile::Read(FilePath, *Material, Error))
	{
		KRS_LOG(Warning, "Failed to load material: {} ({})", InPath, Error);
		return nullptr;
	}

	Material->bIsInstance = false;
	Material->Parent = nullptr;
	Material->SyncLegacyStates();

	RegisterAsset(Key, Material);
	RegisterAssetPath(Key, FPaths::ToUtf8(FilePath));
	return Material;
}

bool UAssetManager::SaveMaterial(UMaterial* Material, const FString& Path, FString& OutError)
{
	if (!Material)
	{
		OutError = "Material is null";
		return false;
	}

	const FString TargetPath = Path.empty() ? Material->GetPath() : Path;
	if (TargetPath.empty())
	{
		OutError = "Material save path is empty";
		return false;
	}

	const FString Key = FAssetUtils::GetAssetKey(TargetPath);
	fs::path FilePath = FPaths::FromUtf8(TargetPath);
	if (FilePath.is_relative())
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(TargetPath);
		if (!Resolved.empty())
		{
			FilePath = FPaths::FromUtf8(Resolved);
		}
		else
		{
			FilePath = FPaths::ProjectContentDir() / FPaths::FromUtf8(Key);
		}
	}

	Material->SyncLegacyStates();
	if (!MaterialFile::Write(FilePath, *Material, OutError))
	{
		return false;
	}

	Material->bIsInstance = false;
	Material->Parent = nullptr;
	Get().RegisterAsset(Key, Material);
	Get().RegisterAssetPath(Key, FPaths::ToUtf8(FilePath));
	return true;
}

UMaterial* UAssetManager::CreateMaterialAsset(const FString& Path, FString& OutError)
{
	OutError.clear();
	if (Path.empty())
	{
		OutError = "Material path is empty";
		return nullptr;
	}

	UMaterial* Material = NewObject<UMaterial>();
	Material->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/UberLit.hlsl");
	Material->ParamLayout = EMaterialParamLayout::StaticMesh;
	Material->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	Material->MaterialDomain = EMaterialDomain::Surface;
	Material->BlendMode = EMaterialBlendMode::Opaque;
	Material->ShadingModel = EMaterialShadingModel::DefaultLit;
	Material->bTwoSided = false;
	Material->OpacityMaskClipValue = 0.333f;
	Material->BaseColor = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
	Material->SpecularColor = FVector(1.0f, 1.0f, 1.0f);
	Material->Shininess = 32.0f;
	Material->SamplerState = ESamplerState::LinearWrap;
	Material->Textures.Add(nullptr);
	Material->Textures.Add(nullptr);
	Material->SyncLegacyStates();

	if (!SaveMaterial(Material, Path, OutError))
	{
		return nullptr;
	}

	return Material;
}

TArray<FString> UAssetManager::GetAllMaterialAssetPaths()
{
	TSet<FString> Seen;
	TArray<FString> Result;

	// 프로젝트/엔진 콘텐츠의 .mat 파일 (로드됨 + 미로드 모두 포함)
	for (const auto& [Key, FullPath] : Get().AssetPathMap)
	{
		if (GetAssetTypeFromPath(Key) == EAssetType::Material && !Seen.Contains(Key))
		{
			Seen.Add(Key);
			Result.Add(Key);
		}
	}

	// 이미 메모리에 등록된 .mat 에셋 및 기본 내장 머티리얼
	for (const auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UMaterial>()) continue;
		if (Key == "BillboardMaterial" || Key == "EditorArrowMaterial") continue;
		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			Result.Add(Key);
		}
	}

	// .mat 에셋을 먼저, 내장 머티리얼을 뒤에 정렬
	std::sort(Result.begin(), Result.end(), [](const FString& A, const FString& B)
	{
		const bool bAMat = MaterialFile::IsMaterialPath(A);
		const bool bBMat = MaterialFile::IsMaterialPath(B);
		if (bAMat != bBMat) return bAMat;
		return A < B;
	});

	return Result;
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool GenerateMips) {
	const FString Key = FAssetUtils::GetAssetKey(InPath);
	if (URenderAsset** Found{ AssetMap.Find(Key) }) {
		return Cast<UTexture2D>(*Found);
	}

	// 옛 경로("Content/Game/Icons/Decal.png")로 불렸는데 프로젝트에는 없고 엔진 콘텐츠에 있으면 그쪽을 쓴다.
	if (!AssetPathMap.Contains(Key))
	{
		const FString EngineKey = MakeEngineFallbackKey(Key);
		if (!EngineKey.empty() && AssetPathMap.Contains(EngineKey))
		{
			return Cast<UTexture2D>(FindOrLoadAsset(EngineKey));
		}
	}

	FLoadStageTimer DecodeTimer{ "Texture/Decode" };
	const FImageData Data = ImageLoader::LoadAuto(FAssetUtils::ResolveAssetFilePath(InPath));
	DecodeTimer.Stop();
	if (!Data.IsValid())
	{
		return nullptr;
	}

	const bool bUseDDSLevels = Data.MipRowPitches.Num() > 0;
	// BC 압축 DDS는 렌더 타깃으로 바인딩할 수 없으므로 GPU mip 생성 대상에서 제외한다.
	const bool bGenerateGPUmips = GenerateMips && !bUseDDSLevels && Data.BytesPerPixel != 0;

	FTextureDesc Desc{};
	Desc.Width = Data.Width;
	Desc.Height = Data.Height;
	Desc.MipLevels = bGenerateGPUmips ? 0 : Data.MipLevels;
	Desc.Usage = bGenerateGPUmips ? ETextureUsage::Default : ETextureUsage::Immutable;
	Desc.BindFlags = ETextureBindFlags::ShaderResource |
		(bGenerateGPUmips ? ETextureBindFlags::RenderTarget : ETextureBindFlags::None);
	Desc.bGenerateMips = bGenerateGPUmips;

	FLoadStageTimer CreateTimer{ "Texture/CreateTexture2D" };
	TUniquePtr<FTexture2D> Resource = RenderCommand::CreateTexture2D(Desc, Data);
	CreateTimer.Stop();

	if (!Resource || !Resource->GetSRV()) {
		return nullptr;
	}

	UTexture2D* Asset{ NewObject<UTexture2D>() };
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	TUniquePtr<UFont> Font(NewObject<UFont>());

	if (!Font->LoadFontAtlasJson(FPaths::ResolveContentPath(JsonPath)))
	{
		return nullptr;
	}

	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font.get());
	return Font.release();
}

USoundWave* UAssetManager::LoadSound(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (Key.empty()) return nullptr;
	if (USoundWave** Found = SoundMap.Find(Key)) return *Found;

	TUniquePtr<USoundWave> Wave(NewObject<USoundWave>());
	if (!Wave->LoadFromFile(FAssetUtils::ResolveAssetFilePath(Path)))
	{
		return nullptr;
	}
	SoundMap[Key] = Wave.get();
	return Wave.release();
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	// GetAssetByPath는 여기로 다시 들어오므로 쓰지 않는다
	if (URenderAsset* Cached = FindLoadedAsset(Key))
	{
		return Cast<UStaticMesh>(Cached);
	}

	FLoadStageTimer ReadTimer{ "Mesh/ReadBakedOrCook" };
	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(FAssetUtils::ResolveAssetFilePath(Path));
	ReadTimer.Stop();
	UStaticMesh* Mesh = CreateStaticMesh(Data.get(), Key);   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

URenderAsset* UAssetManager::LoadFbxMesh(const FString& Path, FString& OutError)
{
	OutError.clear();
	const FString Key = FAssetUtils::GetAssetKey(Path);
	// GetAssetByPath는 여기로 다시 들어오므로 쓰지 않는다
	if (URenderAsset* Cached = FindLoadedAsset(Key)) return Cached;
	FFbxBakedMesh Data;
	if (!FFbxMeshBake::IsBakedPath(Path) || !FFbxMeshBake::Read(FAssetUtils::ResolveAssetFilePath(Path), Data, OutError))
	{
		if (OutError.empty()) OutError = "FBX binary: expected .fbx.bin: " + Path;
		return nullptr;
	}
	URenderAsset* Asset = nullptr;
	if (Data.Kind == EFbxMeshKind::Static) Asset = CreateStaticMesh(&Data.StaticMesh, Key);
	else
	{
		auto* Mesh = NewObject<USkeletalMesh>();
		if (!Mesh->Build(std::move(Data.SkeletalMesh), OutError, Key)) return nullptr;
		Mesh->LoadClothingAssets(FAssetUtils::ResolveAssetFilePath(Path));
		Asset = Mesh;
	}
	if (!Asset) { OutError = "FBX binary: mesh resource creation failed: " + Path; return nullptr; }
	Get().RegisterAsset(Key, Asset);
	return Asset;
}

UAnimSequence* UAssetManager::LoadAnimSequence(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (UAnimSequence** Found = Get().AnimSequenceMap.Find(Key))
	{
		return *Found;
	}

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UAnimSequence* Sequence = NewObject<UAnimSequence>();
	FString Error;
	if (!AnimSequenceFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *Sequence, Error))
	{
		KRS_LOG(Warning, "Failed to load animation sequence: {} ({})", Path, Error);
		delete Sequence;    // 캐시에 넣지 않은 객체는 아무도 지우지 않으므로 여기서 지운다
		return nullptr;
	}

	Get().AnimSequenceMap[Key] = Sequence;
	return Sequence;
}

bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.MeshData.Vertices.IsEmpty())
		return false;

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertex& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;
	Data.BuildTangentSpace();

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertex));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	Data.BuildOBBTree();
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (const FString& Path : GetUnloadedAssetPaths(EAssetType::StaticMesh))
	{
		if (!FFbxMeshBake::IsBakedPath(Path))
		{
			GetAssetByPath<UStaticMesh>(Path);
		}
	}

	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !fs::path(Key).extension().string().ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

namespace
{
	FString SanitizeMaterialFileName(const FString& Raw, const FString& Fallback)
	{
		FString Out;
		Out.reserve(Raw.size());
		for (char C : Raw)
		{
			const unsigned char UC = static_cast<unsigned char>(C);
			if (UC < 32 || C == '\\' || C == '/' || C == ':' || C == '*' || C == '?' || C == '"' || C == '<' || C == '>' || C == '|')
			{
				Out.push_back('_');
			}
			else
			{
				Out.push_back(C);
			}
		}
		while (!Out.empty() && (Out.back() == ' ' || Out.back() == '.'))
		{
			Out.pop_back();
		}
		return Out.empty() ? Fallback : Out;
	}

	// 디퓨즈 텍스처에 클립 값보다 낮은 알파 픽셀이 있으면 컷아웃(Masked) 텍스처로 본다.
	// 눈꺼풀·속눈썹·머리카락 카드처럼 투명 영역의 RGB가 검정인 텍스처를 Opaque로 그리면 검게 보이기 때문.
	// 새 .mat을 처음 만들 때만 호출되므로 파일을 한 번 더 디코드하는 비용은 감수한다.
	bool TextureHasCutoutAlpha(const FString& TexturePath, float ClipValue)
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(TexturePath);
		if (Resolved.empty())
		{
			return false;
		}

		// HDR/DDS는 8비트 RGBA가 아니므로 검사하지 않는다.
		FString Ext = FPaths::ToUtf8(FPaths::FromUtf8(Resolved).extension());
		for (char& C : Ext) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		if (Ext == ".hdr" || Ext == ".dds")
		{
			return false;
		}

		const FImageData Data = ImageLoader::Load(Resolved);
		if (!Data.IsValid() || Data.BytesPerPixel != 4)
		{
			return false;
		}

		const uint8 Threshold = static_cast<uint8>(std::clamp(ClipValue, 0.0f, 1.0f) * 255.0f);
		const uint8* Pixels = Data.Pixels.GetData();
		const size_t PixelCount = static_cast<size_t>(Data.Width) * Data.Height;
		for (size_t i = 0; i < PixelCount; ++i)
		{
			if (Pixels[i * 4 + 3] < Threshold)
			{
				return true;
			}
		}
		return false;
	}

	FString ExtractMeshStemForMaterial(const fs::path& MeshPath)
	{
		FString FileName = FPaths::ToUtf8(MeshPath.filename());
		const auto StripSuffix = [&FileName](const char* Suffix) -> bool
		{
			const size_t Len = std::strlen(Suffix);
			if (FileName.size() <= Len) return false;
			FString Tail = FileName.substr(FileName.size() - Len);
			for (char& C : Tail) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
			if (Tail == Suffix)
			{
				FileName.resize(FileName.size() - Len);
				return true;
			}
			return false;
		};

		if (!StripSuffix("_static.fbx.bin") && !StripSuffix(".fbx.bin") && !StripSuffix(".obj"))
		{
			FileName = FPaths::ToUtf8(MeshPath.stem());
		}
		return SanitizeMaterialFileName(FileName, "Mesh");
	}

	template<typename TSlot>
	bool BuildMaterialsFromSlotsImpl(const TArray<TSlot>& Slots, TArray<UMaterial*>& OutMaterials, const FString& OwnerMeshPath)
	{
		// MaterialSlots를 실제 UMaterial로 변환
		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return false;
		}

		// 프로젝트 콘텐츠의 디스크 기반 메시(OBJ/FBX)면 Materials/ 하위에 .mat 에셋을 자동 생성하거나 기존 에셋을 로드한다.
		bool bUseMaterialAssets = false;
		fs::path MaterialDir;
		FString MeshStem;
		if (!OwnerMeshPath.empty() && !OwnerMeshPath.starts_with(FPaths::EngineContentPrefix))
		{
			const FString ResolvedMesh = FAssetUtils::ResolveAssetFilePath(OwnerMeshPath);
			if (!ResolvedMesh.empty())
			{
				const fs::path MeshFsPath = FPaths::FromUtf8(ResolvedMesh);
				std::error_code ExistsErr;
				if (fs::exists(MeshFsPath, ExistsErr))
				{
					MaterialDir = MeshFsPath.parent_path() / "Materials";
					MeshStem = ExtractMeshStemForMaterial(MeshFsPath);
					bUseMaterialAssets = true;
				}
			}
		}

		TSet<FString> UsedNames;
		for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex)
		{
			const TSlot& Slot = Slots[SlotIndex];

			if (bUseMaterialAssets)
			{
				const FString SlotName = SanitizeMaterialFileName(Slot.Name, "Slot_" + std::to_string(SlotIndex));
				FString BaseName = MeshStem + "_" + SlotName;
				FString UniqueName = BaseName;
				int32 Suffix = 1;
				auto ToLowerStr = [](FString S)
				{
					for (char& C : S) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
					return S;
				};
				while (UsedNames.Contains(ToLowerStr(UniqueName)))
				{
					UniqueName = BaseName + "_" + std::to_string(Suffix++);
				}
				UsedNames.Add(ToLowerStr(UniqueName));

				const fs::path MatFilePath = MaterialDir / FPaths::FromUtf8(UniqueName + ".mat");
				const FString MatFileUtf8 = FPaths::ToUtf8(MatFilePath);
				const FString MatKey = FAssetUtils::GetAssetKey(MatFileUtf8);

				// 이미 .mat 에셋이 존재하면 사용자 편집 내용을 보존하기 위해 덮어쓰지 않고 로드한다
				std::error_code ExistsErr;
				if (fs::exists(MatFilePath, ExistsErr))
				{
					if (UMaterial* Existing = UAssetManager::Get().LoadMaterial(MatFileUtf8))
					{
						OutMaterials.Add(Existing);
						continue;
					}
				}

				// 없으면 메시 슬롯 정보로 새 .mat 에셋을 생성하고 디스크에 저장한다
				UMaterial* Material = NewObject<UMaterial>();
				Material->CopyPropertiesFrom(*DefaultMaterial);
				Material->Textures.Reset();
				Material->Textures.Add(nullptr);
				Material->Textures.Add(nullptr);
				Material->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
				Material->bIsInstance = false;
				Material->Parent = nullptr;
				Material->BaseColor = Slot.BaseColor;
				// 임포트 메쉬의 UV는 0~1 밖의 타일을 쓰는 경우가 많아 반복 샘플링을 기본으로 한다.
				Material->SamplerState = ESamplerState::LinearWrap;
				Material->BlendMode = (Material->BaseColor.W < 1.0f)
					? EMaterialBlendMode::Translucent
					: EMaterialBlendMode::Opaque;
				Material->ShadingModel = EMaterialShadingModel::DefaultLit;
				Material->MaterialDomain = EMaterialDomain::Surface;
				Material->SyncLegacyStates();

				if (!Slot.DiffuseTexturePath.empty())
				{
					if (UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath))
					{
						Material->SetBaseColorTexture(Texture);

						if (Material->BlendMode == EMaterialBlendMode::Opaque &&
							TextureHasCutoutAlpha(Slot.DiffuseTexturePath, Material->OpacityMaskClipValue))
						{
							Material->BlendMode = EMaterialBlendMode::Masked;
							Material->SyncLegacyStates();
							KRS_LOG(Info, "Diffuse texture has alpha, using Masked blend mode: {}", MatKey);
						}
					}
				}
				if (!Slot.NormalTexturePath.empty())
				{
					if (UTexture2D* NormalTexture = UAssetManager::Get().LoadTexture(Slot.NormalTexturePath))
					{
						Material->SetNormalTexture(NormalTexture);
					}
				}

				FString SaveError;
				if (!UAssetManager::SaveMaterial(Material, MatFileUtf8, SaveError))
				{
					KRS_LOG(Warning, "Auto-save material failed for {}: {}", MatKey, SaveError);
				}
				else
				{
					KRS_LOG(Info, "Generated material asset: {}", MatKey);
				}

				OutMaterials.Add(Material);
				continue;
			}

			// 내장 기본 메시(Cube, Sphere 등): 디스크에 .mat를 쓰지 않고 메모리 인스턴스 사용
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return false;
			}
			Material->BaseColor = Slot.BaseColor;
			Material->SamplerState = ESamplerState::LinearWrap;
			if (Material->BaseColor.W < 1.0f)
			{
				Material->BlendMode = EMaterialBlendMode::Translucent;
				Material->SyncLegacyStates();
			}

			if (!Slot.DiffuseTexturePath.empty())
			{
				if (UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath))
				{
					Material->SetBaseColorTexture(Texture);
				}
			}

			if (!Slot.NormalTexturePath.empty())
			{
				if (UTexture2D* NormalTexture = UAssetManager::Get().LoadTexture(Slot.NormalTexturePath))
				{
					Material->SetNormalTexture(NormalTexture);
				}
			}

			OutMaterials.Add(Material);
		}
		return true;
	}

}

bool UAssetManager::BuildMaterialsFromSlots(const TArray<FStaticMaterialSlot>& Slots, TArray<UMaterial*>& OutMaterials, const FString& OwnerMeshPath)
{
	return BuildMaterialsFromSlotsImpl(Slots, OutMaterials, OwnerMeshPath);
}

bool UAssetManager::BuildMaterialsFromSlots(const TArray<FSkeletalMaterialSlot>& Slots, TArray<UMaterial*>& OutMaterials, const FString& OwnerMeshPath)
{
	return BuildMaterialsFromSlotsImpl(Slots, OutMaterials, OwnerMeshPath);
}

UBlendSpace1D* UAssetManager::LoadBlendSpace(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (UBlendSpace1D** Found = Get().BlendSpaceMap.Find(Key))
	{
		return *Found;
	}

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>();

	FString Error;
	if (!BlendSpaceFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *BlendSpace, Error))
	{
		KRS_LOG(Warning, "Failed to load BlendSpace: {} ({})", Path, Error);
		delete BlendSpace;
		return nullptr;
	}

	Get().BlendSpaceMap[Key] = BlendSpace;
	return BlendSpace;
}

UParticleSystem* UAssetManager::LoadParticleSystem(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (UParticleSystem** Found = Get().ParticleSystemMap.Find(Key))
	{
		return *Found;
	}

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UParticleSystem* ParticleSystem = NewObject<UParticleSystem>();

	FString Error;
	if (!ParticleSystemFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *ParticleSystem, Error))
	{
		KRS_LOG(Warning, "Failed to load ParticleSystem: {} ({})", Path, Error);
		delete ParticleSystem;
		return nullptr;
	}

	Get().ParticleSystemMap[Key] = ParticleSystem;
	return ParticleSystem;
}

UPhysicalMaterial* UAssetManager::LoadPhysicalMaterial(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (UPhysicalMaterial** Found = Get().PhysicalMaterialMap.Find(Key))
	{
		return *Found;
	}

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UPhysicalMaterial* Material = NewObject<UPhysicalMaterial>();

	FString Error;
	if (!PhysicalMaterialFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *Material, Error))
	{
		KRS_LOG(Warning, "Failed to load PhysicalMaterial: {} ({})", Path, Error);
		delete Material;
		return nullptr;
	}

	Get().PhysicalMaterialMap[Key] = Material;
	return Material;
}

UPhysicsAsset* UAssetManager::LoadPhysicsAsset(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (UPhysicsAsset** Found = Get().PhysicsAssetMap.Find(Key))
		return *Found;   // 같은 메시를 쓰는 캐릭터 여러 명이 에셋 하나를 공유한다

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();

	FString Error;
	if (!PhysicsAssetFile::Read(std::filesystem::path(FilePath.empty() ? Path : FilePath), *Asset, Error))
	{
		KRS_LOG(Warning, "Failed to load PhysicsAsset: {} ({})", Path, Error);
		delete Asset;
		return nullptr;
	}

	Get().PhysicsAssetMap[Key] = Asset;
	return Asset;
}

ULandscapeHeightmap* UAssetManager::LoadLandscapeHeightmap(const FString& Path)
{
	const FString Key = FAssetUtils::GetAssetKey(Path);
	if (ULandscapeHeightmap** Found = Get().LandscapeHeightmapMap.Find(Key))
		return *Found;   // 이미 읽은 하이트맵은 캐시에서 돌려준다

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Path);
	const FString& OpenPath = FilePath.empty() ? Path : FilePath;

	int W = 0, H = 0, Channels = 0;
	// 16비트로 읽는다. 마지막 인자 1 = 회색 1채널로 강제
	stbi_us* Pixels = stbi_load_16(OpenPath.c_str(), &W, &H, &Channels, 1);
	if (!Pixels)
	{
		KRS_LOG(Warning, "Failed to load landscape heightmap: {} ({})", Path, stbi_failure_reason());
		return nullptr;
	}

	ULandscapeHeightmap* Asset = NewObject<ULandscapeHeightmap>();
	Asset->SetPath(Key);

	FTerrainData& D = Asset->Data;
	D.SizeX = W;
	D.SizeY = H;
	D.Heights.SetNum(W * H);
	memcpy(D.Heights.GetData(), Pixels, sizeof(uint16) * W * H);
	stbi_image_free(Pixels);   // stb가 malloc한 메모리라 직접 해제

	uint16 MinH = 65535, MaxH = 0;
	for (int32 i = 0; i < W * H; ++i)
	{
		MinH = (std::min)(MinH, D.Heights[i]);
		MaxH = (std::max)(MaxH, D.Heights[i]);
	}
	KRS_LOG(Info, "Landscape heightmap {}: {}x{} (ch {}), min {} max {}", Path, W, H, Channels, MinH, MaxH);

	Get().LandscapeHeightmapMap[Key] = Asset;
	return Asset;
}
