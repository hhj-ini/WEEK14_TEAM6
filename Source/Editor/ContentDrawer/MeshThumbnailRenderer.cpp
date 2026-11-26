#include "EnginePCH.h"
// imgui.h보다 먼저 EditorStyle.h(IMGUI_DEFINE_MATH_OPERATORS)를 포함해야 한다.
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/ContentDrawer/MeshThumbnailRenderer.h"

#include "Animation/AnimBlueprintData.h"
#include "Animation/AnimBlueprintFile.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/BonePose.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Misc/Paths.h"
#include "Editor/AssetEditor/FPreviewScene.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetDebugRenderer.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorMath.h"
#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Math/BoxSphereBounds.h"
#include "Render/Material.h"
#include "Render/MaterialFile.h"
#include "Render/Mesh.h"
#include "Render/RenderCommand.h"
#include "Render/Renderer.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneView.h"
#include "Render/SceneRenderer.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	std::string ToLowerAscii(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Text;
	}

	bool IsObjExtension(const fs::path& FilePath)
	{
		return ToLowerAscii(FilePath.extension().generic_string()) == ".obj";
	}

	bool AreBoundsFinite(const FBox& Box)
	{
		return std::isfinite(Box.Min.X) && std::isfinite(Box.Min.Y) && std::isfinite(Box.Min.Z)
			&& std::isfinite(Box.Max.X) && std::isfinite(Box.Max.Y) && std::isfinite(Box.Max.Z)
			&& Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	URenderAsset* LoadMeshAssetForThumbnail(const fs::path& FilePath, const FString& Utf8Path)
	{
		if (URenderAsset* Loaded = UAssetManager::FindLoadedAsset(Utf8Path))
		{
			return Loaded;
		}
		if (URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(Utf8Path))
		{
			return Asset;
		}

		const FString Resolved = FAssetUtils::ResolveAssetFilePath(Utf8Path);
		const FString LoadPath = Resolved.empty() ? Utf8Path : Resolved;

		if (IsObjExtension(FilePath))
		{
			return UAssetManager::LoadObjStaticMesh(LoadPath);
		}
		if (FFbxMeshBake::IsBakedPath(LoadPath))
		{
			FString Error;
			return UAssetManager::LoadFbxMesh(LoadPath, Error);
		}
		return nullptr;
	}

	bool HasCompatibleSkeletonTrack(const UAnimSequence& Sequence, const USkeletalMesh& Mesh)
	{
		const UAnimDataModel* Model = Sequence.GetDataModel();
		if (!Model)
		{
			return false;
		}
		const TArray<FBoneAnimationTrack>& Tracks = Model->GetBoneAnimationTracks();
		if (Tracks.IsEmpty())
		{
			return false;
		}
		for (const FBoneAnimationTrack& Track : Tracks)
		{
			if (Mesh.FindBoneIndex(Track.Name) >= 0)
			{
				return true;
			}
		}
		return false;
	}

	FBox ComputePosedSkeletonBounds(const USkeletalMeshComponent& Comp, const FBox& FallbackBindBox)
	{
		const int32 NumBones = Comp.GetBones().Num();
		if (NumBones <= 0)
		{
			return FallbackBindBox;
		}

		FBox PoseBox(
			FVector(FLT_MAX, FLT_MAX, FLT_MAX),
			FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX));
		bool bAnyBone = false;
		for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
		{
			const FVector4 Pos = Comp.GetBoneComponentMatrix(BoneIdx).GetOrigin();
			if (!std::isfinite(Pos.X) || !std::isfinite(Pos.Y) || !std::isfinite(Pos.Z))
			{
				continue;
			}
			PoseBox.Min.X = std::min(PoseBox.Min.X, Pos.X);
			PoseBox.Min.Y = std::min(PoseBox.Min.Y, Pos.Y);
			PoseBox.Min.Z = std::min(PoseBox.Min.Z, Pos.Z);
			PoseBox.Max.X = std::max(PoseBox.Max.X, Pos.X);
			PoseBox.Max.Y = std::max(PoseBox.Max.Y, Pos.Y);
			PoseBox.Max.Z = std::max(PoseBox.Max.Z, Pos.Z);
			bAnyBone = true;
		}

		if (!bAnyBone || !AreBoundsFinite(PoseBox))
		{
			return FallbackBindBox;
		}

		const FVector BindExtent = AreBoundsFinite(FallbackBindBox)
			? (FallbackBindBox.Max - FallbackBindBox.Min) * 0.5f
			: FVector(0.5f, 0.5f, 0.5f);
		const float Pad = std::max(0.2f, BindExtent.Size() * 0.2f);
		PoseBox.Min -= FVector(Pad, Pad, Pad);
		PoseBox.Max += FVector(Pad, Pad, Pad);
		return PoseBox;
	}

	USkeletalMesh* TryLoadSkeletalMeshCandidate(const FString& CandidatePath)
	{
		if (CandidatePath.empty())
		{
			return nullptr;
		}
		std::error_code Ec;
		if (!fs::is_regular_file(FPaths::FromUtf8(CandidatePath), Ec))
		{
			return nullptr;
		}
		FString Error;
		return Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(CandidatePath, Error));
	}

	// 다른 PC/경로에서 저장된 절대 경로(.../Content/...)도 현재 프로젝트 Content 기준으로 복원해 프리뷰 메시를 찾는다.
	USkeletalMesh* LoadPreviewSkeletalMesh(const FString& PreviewPath, const fs::path& AssetFilePath, const UAnimSequence* OptionalSeq)
	{
		if (!PreviewPath.empty())
		{
			if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FAssetUtils::ResolveAssetFilePath(PreviewPath)))
			{
				return Mesh;
			}

			std::string Normalized = PreviewPath;
			std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
			const std::string Lower = ToLowerAscii(Normalized);
			const size_t ContentPos = Lower.rfind("/content/");
			if (ContentPos != std::string::npos)
			{
				const FString RelPath = Normalized.substr(ContentPos + 9);
				if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FAssetUtils::ResolveAssetFilePath(RelPath)))
				{
					return Mesh;
				}
			}

			const fs::path PreviewFileName = FPaths::FromUtf8(Normalized).filename();
			if (!PreviewFileName.empty() && !AssetFilePath.empty())
			{
				const fs::path LocalSibling = AssetFilePath.parent_path() / PreviewFileName;
				if (USkeletalMesh* Mesh = TryLoadSkeletalMeshCandidate(FPaths::ToUtf8(LocalSibling)))
				{
					return Mesh;
				}
			}
		}

		// PreviewPath가 비어 있거나 이동된 경우 같은 폴더의 스켈레탈 메시에서 호환 메시를 찾는다.
		if (!AssetFilePath.empty())
		{
			std::error_code DirEc;
			const fs::path ParentDir = AssetFilePath.parent_path();
			for (const fs::directory_entry& Entry : fs::directory_iterator(ParentDir, DirEc))
			{
				if (DirEc || !Entry.is_regular_file())
				{
					continue;
				}
				const FString CandidateUtf8 = FPaths::ToUtf8(Entry.path());
				const std::string LowerCandidate = ToLowerAscii(CandidateUtf8);
				if (!FFbxMeshBake::IsBakedPath(CandidateUtf8)
					|| (LowerCandidate.size() >= 15 && LowerCandidate.compare(LowerCandidate.size() - 15, 15, "_static.fbx.bin") == 0))
				{
					continue;
				}
				FString Error;
				if (USkeletalMesh* SiblingMesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(CandidateUtf8, Error)))
				{
					if (!OptionalSeq || HasCompatibleSkeletonTrack(*OptionalSeq, *SiblingMesh))
					{
						return SiblingMesh;
					}
				}
			}
		}

		return nullptr;
	}

	UAnimSequence* FindPreviewSequenceInBlueprint(const FAnimBlueprintData& Data)
	{
		for (const FAnimBlueprintGraph& Graph : Data.Graphs)
		{
			for (const FAnimBlueprintNode& Node : Graph.Nodes)
			{
				if (Node.Type == EAnimBlueprintNodeType::SequencePlayer && !Node.SequencePath.empty())
				{
					if (UAnimSequence* Seq = UAssetManager::LoadAnimSequence(Node.SequencePath))
					{
						return Seq;
					}
				}
			}
		}
		return nullptr;
	}

	bool SetupSkeletalThumbnail(
		USkeletalMesh* SkeletalMesh,
		const UAnimSequence* OptionalPoseSeq,
		UStaticMeshComponent* StaticComp,
		USkeletalMeshComponent* SkelComp,
		FBox& OutBounds)
	{
		if (!SkeletalMesh)
		{
			return false;
		}
		const auto& LODs = SkeletalMesh->GetRenderData().LODRenderData;
		if (LODs.IsEmpty() || LODs[0].BindVertices.IsEmpty() || LODs[0].Indices.IsEmpty())
		{
			return false;
		}

		StaticComp->SetStaticMesh(nullptr);
		SkelComp->SetSkeletalMesh(SkeletalMesh);
		SkelComp->ResetPose();

		if (OptionalPoseSeq && HasCompatibleSkeletonTrack(*OptionalPoseSeq, *SkeletalMesh))
		{
			const float Length = OptionalPoseSeq->GetPlayLength();
			const float SampleTime = Length > 0.0f ? Length * 0.35f : 0.0f;
			FCompactPose Pose;
			OptionalPoseSeq->GetAnimationPose(SampleTime, Pose, SkeletalMesh->GetRefSkeleton());
			SkelComp->SetBoneSpaceTransforms(Pose);
			SkelComp->RefreshBoneTransformsNow();
			OutBounds = ComputePosedSkeletonBounds(*SkelComp, LODs[0].BindAABB);
		}
		else
		{
			SkelComp->RefreshBoneTransformsNow();
			OutBounds = LODs[0].BindAABB;
		}

		return AreBoundsFinite(OutBounds);
	}
}

class FThumbnailViewportClient final : public FEditorViewportClient
{
public:
	explicit FThumbnailViewportClient(FPreviewScene& InScene)
		: FEditorViewportClient(InScene)
	{
		EngineShowFlags.Fog = false;
		EngineShowFlags.AntiAliasing = true;
		EngineShowFlags.Vignette = false;
		EngineShowFlags.CameraOverlay = false;
		EngineShowFlags.DepthOfField = false;
		EngineShowFlags.SkeletalMeshes = true;
	}

	// 피직스 에셋 썸네일: 메시 위에 바디를 겹쳐 그린다. nullptr이면 그리지 않는다
	void SetPhysicsOverlay(const UPhysicsAsset* InPhysicsAsset, const USkeletalMeshComponent* InMeshComp)
	{
		OverlayPhysicsAsset = InPhysicsAsset;
		OverlayMeshComp = InMeshComp;
	}

	void ApplySettings(const FEditorSettings* Settings, bool bTranslucentPreview = false)
	{
		bUseCheckerBackground = !bTranslucentPreview;
		RenderSettings.bUsePreviewSkybox = bTranslucentPreview;
		if (!Settings)
		{
			return;
		}
		RenderSettings.LightingModel = Settings->LightingModel;
		RenderSettings.bGammaCorrection = Settings->bGammaCorrection;
		RenderSettings.DisplayGamma = Settings->DisplayGamma;
	}

private:
	bool bUseCheckerBackground = true;
	const UPhysicsAsset* OverlayPhysicsAsset = nullptr;
	const USkeletalMeshComponent* OverlayMeshComp = nullptr;
	TUniquePtr<FPhysicsAssetDebugRenderer> PhysicsDebugRenderer;
	// ThumbnailCheckerShader.hlsl의 cbuffer와 같은 배치
	struct FCheckerConstants
	{
		FVector4 ColorA;
		FVector4 ColorB;
		float CellSize = 16.0f;
		float Padding[3] = {};
	};
	static_assert(sizeof(FCheckerConstants) % 16 == 0);
	static constexpr uint32 ThumbnailCheckerFallbackSize = 128;

	TUniquePtr<FConstantBuffer> BackgroundConstants;

protected:
	void Draw(const FSceneView* View, FScenePassCallbacks& Callbacks) override
	{
		if (OverlayPhysicsAsset && OverlayMeshComp && View)
		{
			// 체커 배경(EditorGrid) 다음 패스에서 그려야 배경에 덮이지 않는다
			const FMatrix ViewProjection = View->ViewProjectionMatrix;
			Callbacks.Overlays.EditorOutline = [this, ViewProjection]() { DrawPhysicsBodies(ViewProjection); };
		}
		if (!bUseCheckerBackground)
		{
			return;
		}
		const float TargetWidth = View ? View->ViewRect.Width : static_cast<float>(ThumbnailCheckerFallbackSize);
		Callbacks.Overlays.EditorGrid = [this, TargetWidth]()
		{
			FShaderProgram* CheckerShader = FRenderResourceManager::GetShaderProgram(
				"EngineResources/Shaders/ThumbnailCheckerShader.hlsl");
			if (!CheckerShader || !CheckerShader->VertexShader || !CheckerShader->PixelShader)
			{
				return;
			}
			if (!BackgroundConstants)
			{
				BackgroundConstants = RenderCommand::CreateConstantBuffer(sizeof(FCheckerConstants));
			}
			if (!BackgroundConstants)
			{
				return;
			}
			// 콘텐츠 브라우저의 다른 타일과 같은 테마 색, 같은 칸 수의 체커. 테마가 바뀌면 썸네일을 다시 찍는다.
			const ImVec4 A = EditorColor::ToVec4(EditorColor::BgDeepest);
			const ImVec4 B = EditorColor::ToVec4(EditorColor::BgRaised);
			FCheckerConstants Background{};
			Background.ColorA = FVector4(A.x, A.y, A.z, 1.0f);
			Background.ColorB = FVector4(B.x, B.y, B.z, 1.0f);
			Background.CellSize = TargetWidth / static_cast<float>(FMeshThumbnailRenderer::CheckerCellsAcross);
			RenderCommand::UpdateBufferData(BackgroundConstants.get(), &Background, sizeof(Background));
			RenderCommand::BindConstantBuffer(0, BackgroundConstants.get(), EShaderBindFlagBits::Pixel);

			FPipelineState Pipeline{};
			Pipeline.Shader = CheckerShader;
			Pipeline.Topology = EPrimitiveTopology::TriangleList;
			Pipeline.RasterizerState = ERasterizerState::SolidNone;
			Pipeline.BlendState = EBlendState::Opaque;
			Pipeline.DepthStencilState = EDepthStencilState::ReadOnly;
			RenderCommand::BindPipelineState(Pipeline);
			RenderCommand::BindVertexBuffer(nullptr);
			RenderCommand::Draw(3);
		};
	}

private:
	void DrawPhysicsBodies(const FMatrix& ViewProjection)
	{
		const USkeletalMesh* Mesh = OverlayMeshComp->GetSkeletalMesh();
		if (!Mesh)
		{
			return;
		}
		if (!PhysicsDebugRenderer)
		{
			PhysicsDebugRenderer = MakeUnique<FPhysicsAssetDebugRenderer>();
		}

		using PhysicsAssetEditorMath::MakeMatrix;
		PhysicsDebugRenderer->Begin(ViewProjection, true);
		const FVector4& Color = PhysicsDebugColors::Body;
		for (const UBodySetup* Setup : OverlayPhysicsAsset->BodySetup)
		{
			const int32 Bone = Setup ? Mesh->FindBoneIndex(Setup->BoneName.ToString()) : -1;
			if (Bone < 0)
			{
				continue;
			}
			const FMatrix BoneToWorld = OverlayMeshComp->GetBoneWorldMatrix(Bone);
			for (const FKSphylElem& S : Setup->AggGeom.SphylElems)
				PhysicsDebugRenderer->DrawCapsule(MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, S.Length, Color);
			for (const FKBoxElem& B : Setup->AggGeom.BoxElems)
				PhysicsDebugRenderer->DrawBox(MakeMatrix(B.Center, B.Rotation) * BoneToWorld, B.Extent, Color);
			for (const FKSphereElem& S : Setup->AggGeom.SphereElems)
				PhysicsDebugRenderer->DrawSphere(MakeMatrix(S.Center, S.Rotation) * BoneToWorld, S.Radius, Color);
		}
	}
};

FMeshThumbnailRenderer::FMeshThumbnailRenderer() = default;

FMeshThumbnailRenderer::~FMeshThumbnailRenderer()
{
	Release();
}

bool FMeshThumbnailRenderer::IsSupportedMeshFile(const fs::path& FilePath, const FString& Utf8Path)
{
	return IsObjExtension(FilePath)
		|| FFbxMeshBake::IsBakedPath(Utf8Path)
		|| AnimSequenceFile::IsAnimSequencePath(Utf8Path)
		|| BlendSpaceFile::IsBlendSpacePath(Utf8Path)
		|| AnimBlueprintFile::IsAnimBlueprintPath(Utf8Path)
		|| MaterialFile::IsMaterialPath(Utf8Path)
		|| PhysicsAssetFile::IsPhysicsAssetPath(Utf8Path);
}

ImTextureID FMeshThumbnailRenderer::GetOrRequestThumbnail(
	const fs::path& FilePath,
	const FString& Utf8Path,
	fs::file_time_type LastWriteTime)
{
	const auto It = Cache.find(Utf8Path);
	const unsigned char CurrentTheme = static_cast<unsigned char>(EditorStyle::GetTheme());
	if (It != Cache.end() && It->second.LastWriteTime == LastWriteTime && It->second.Theme == CurrentTheme)
	{
		if (It->second.Texture && It->second.Texture->GetSRV())
		{
			return reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV());
		}
		if (It->second.bFailed)
		{
			return ImTextureID_Invalid;
		}
	}

	auto QueueIt = std::find_if(PendingQueue.begin(), PendingQueue.end(),
		[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; });
	if (QueueIt == PendingQueue.end())
	{
		PendingQueue.push_back({ FilePath, Utf8Path, LastWriteTime });
	}
	else if (QueueIt->LastWriteTime != LastWriteTime)
	{
		QueueIt->FilePath = FilePath;
		QueueIt->LastWriteTime = LastWriteTime;
	}

	// 파일이 막 갱신된 경우 새로 찍히기 전까지는 직전 텍스처라도 유지해 깜빡임을 줄인다.
	if (It != Cache.end() && It->second.Texture && It->second.Texture->GetSRV())
	{
		return reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV());
	}
	return ImTextureID_Invalid;
}

ImTextureID FMeshThumbnailRenderer::GetOrRequestAssetThumbnail(const FString& AssetPathOrKey)
{
	if (AssetPathOrKey.empty())
	{
		return ImTextureID_Invalid;
	}

	// 디스크 기반 에셋(.mat, .obj, .fbx.bin, .anim.bin 등) — ContentDrawerPanel과 동일한 정규 경로 캐시 키 사용
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(AssetPathOrKey);
		fs::path FilePath = FPaths::FromUtf8(Resolved.empty() ? AssetPathOrKey : Resolved);
		if (FilePath.is_relative())
		{
			FilePath = FPaths::ProjectContentDir() / FPaths::FromUtf8(FAssetUtils::GetAssetKey(AssetPathOrKey));
		}

		std::error_code Ec;
		if (fs::is_regular_file(FilePath, Ec))
		{
			const fs::path AbsPath = fs::absolute(FilePath, Ec).lexically_normal();
			const FString CacheKey = FPaths::ToUtf8(AbsPath);
			if (IsSupportedMeshFile(AbsPath, CacheKey))
			{
				const fs::file_time_type WriteTime = fs::last_write_time(AbsPath, Ec);
				return GetOrRequestThumbnail(AbsPath, CacheKey, WriteTime);
			}
		}
	}

	// 내장 에셋("DefaultMaterial", "Cube", "Sphere" 등)
	if (URenderAsset* Loaded = UAssetManager::FindLoadedAsset(AssetPathOrKey))
	{
		if (Loaded->IsA<UMaterial>())
		{
			return GetOrRequestThumbnail({}, "builtin_mat:" + AssetPathOrKey, fs::file_time_type{});
		}
		if (Loaded->IsA<UStaticMesh>())
		{
			return GetOrRequestThumbnail({}, "builtin_mesh:" + AssetPathOrKey, fs::file_time_type{});
		}
		if (Loaded->IsA<USkeletalMesh>())
		{
			return GetOrRequestThumbnail({}, "builtin_skel:" + AssetPathOrKey, fs::file_time_type{});
		}
	}

	return ImTextureID_Invalid;
}

void FMeshThumbnailRenderer::RenderPending(UEditorEngine& Engine, FRenderer* Renderer)
{
	if (PendingQueue.empty() || !Renderer || bPreviewInitFailed)
	{
		return;
	}

	const FPendingRequest Request = std::move(PendingQueue.front());
	PendingQueue.erase(PendingQueue.begin());

	const auto It = Cache.find(Request.Utf8Path);
	if (It != Cache.end() && It->second.LastWriteTime == Request.LastWriteTime &&
		((It->second.Texture && It->second.Texture->GetSRV()) || It->second.bFailed))
	{
		return;
	}

	RenderMeshToCache(Request, Engine, Renderer);
}

bool FMeshThumbnailRenderer::EnsurePreviewScene(UEditorEngine& Engine)
{
	if (PreviewScene && ViewportClient && StaticMeshComp && SkeletalMeshComp)
	{
		return true;
	}
	if (bPreviewInitFailed)
	{
		return false;
	}

	PreviewScene = MakeUnique<FPreviewScene>();
	if (!PreviewScene->Initialize(&Engine))
	{
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}
	PreviewScene->Setup();

	AActor* PreviewActor = PreviewScene->AddActor(AActor::StaticClass());
	if (!PreviewActor)
	{
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}
	PreviewActor->SetActorEnableCollision(false);

	StaticMeshComp = Cast<UStaticMeshComponent>(
		PreviewActor->AddComponent(UStaticMeshComponent::StaticClass(), "ThumbnailStaticMesh"));
	SkeletalMeshComp = Cast<USkeletalMeshComponent>(
		PreviewActor->AddComponent(USkeletalMeshComponent::StaticClass(), "ThumbnailSkeletalMesh"));
	if (!StaticMeshComp || !SkeletalMeshComp)
	{
		StaticMeshComp = nullptr;
		SkeletalMeshComp = nullptr;
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}

	ViewportClient = MakeUnique<FThumbnailViewportClient>(*PreviewScene);
	ViewportClient->SetViewportType(ELevelViewportType::Perspective);
	ViewportClient->Resize(ThumbnailResolution, ThumbnailResolution);

	return true;
}

void FMeshThumbnailRenderer::RenderMeshToCache(const FPendingRequest& Request, UEditorEngine& Engine, FRenderer* Renderer)
{
	FThumbnailEntry& Entry = Cache[Request.Utf8Path];
	Entry.LastWriteTime = Request.LastWriteTime;
	// 테마가 바뀌어 다시 찍는 경우, 실패하면 예전 테마 배경이 남지 않도록 이전 이미지를 먼저 버린다.
	const unsigned char CurrentTheme = static_cast<unsigned char>(EditorStyle::GetTheme());
	if (Entry.Theme != CurrentTheme)
	{
		Entry.Texture.reset();
	}
	Entry.Theme = CurrentTheme;
	Entry.bFailed = true;

	if (!EnsurePreviewScene(Engine))
	{
		return;
	}

	FBox BoundsBox{};
	bool bValidMesh = false;
	bool bIsTranslucentPreview = false;

	StaticMeshComp->ClearOverrideMaterials();
	FThumbnailViewportClient* ThumbnailClient = static_cast<FThumbnailViewportClient*>(ViewportClient.get());
	ThumbnailClient->SetPhysicsOverlay(nullptr, nullptr);

	if (Request.Utf8Path.starts_with("builtin_mesh:"))
	{
		const FString BuiltinName = Request.Utf8Path.substr(13);
		if (UStaticMesh* StaticMesh = UAssetManager::GetAssetByPath<UStaticMesh>(BuiltinName))
		{
			const auto& MeshData = StaticMesh->GetMeshData();
			if (!MeshData.Vertices.IsEmpty() && !MeshData.Indices.IsEmpty())
			{
				SkeletalMeshComp->SetSkeletalMesh(nullptr);
				StaticMeshComp->SetStaticMesh(StaticMesh);
				BoundsBox = StaticMesh->GetRenderBounds();
				bValidMesh = AreBoundsFinite(BoundsBox);
			}
		}
	}
	else if (Request.Utf8Path.starts_with("builtin_skel:"))
	{
		const FString BuiltinName = Request.Utf8Path.substr(13);
		if (USkeletalMesh* SkelMesh = UAssetManager::GetAssetByPath<USkeletalMesh>(BuiltinName))
		{
			bValidMesh = SetupSkeletalThumbnail(SkelMesh, nullptr, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else if (MaterialFile::IsMaterialPath(Request.Utf8Path) || Request.Utf8Path.starts_with("builtin_mat:"))
	{
		UMaterial* Material = nullptr;
		if (Request.Utf8Path.starts_with("builtin_mat:"))
		{
			const FString BuiltinName = Request.Utf8Path.substr(12);
			Material = UAssetManager::GetAssetByPath<UMaterial>(BuiltinName);
		}
		else
		{
			Material = UAssetManager::Get().LoadMaterial(Request.Utf8Path);
		}
		UStaticMesh* Sphere = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
		if (Material && Sphere)
		{
			SkeletalMeshComp->SetSkeletalMesh(nullptr);
			StaticMeshComp->SetStaticMesh(Sphere);
			StaticMeshComp->SetMaterial(0, Material);
			BoundsBox = Sphere->GetRenderBounds();
			bValidMesh = AreBoundsFinite(BoundsBox);
			bIsTranslucentPreview = Material->IsTranslucent();
		}
	}
	else if (PhysicsAssetFile::IsPhysicsAssetPath(Request.Utf8Path))
	{
		// X.fbx.phys의 메시는 옆의 X.fbx.bin
		fs::path MeshPath = Request.FilePath;
		MeshPath.replace_extension(".bin");
		USkeletalMesh* PreviewMesh = TryLoadSkeletalMeshCandidate(FPaths::ToUtf8(MeshPath));
		const UPhysicsAsset* PhysicsAsset = UAssetManager::LoadPhysicsAsset(Request.Utf8Path);
		bValidMesh = SetupSkeletalThumbnail(PreviewMesh, nullptr, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		if (bValidMesh && PhysicsAsset)
		{
			ThumbnailClient->SetPhysicsOverlay(PhysicsAsset, SkeletalMeshComp);
		}
	}
	else if (AnimSequenceFile::IsAnimSequencePath(Request.Utf8Path))
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Request.Utf8Path);
		if (Sequence && Sequence->GetDataModel())
		{
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(Sequence->GetPreviewMeshPath(), Request.FilePath, Sequence);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, Sequence, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else if (BlendSpaceFile::IsBlendSpacePath(Request.Utf8Path))
	{
		if (UBlendSpace1D* BlendSpace = UAssetManager::LoadBlendSpace(Request.Utf8Path))
		{
			const UAnimSequence* SampleSeq = nullptr;
			for (const FBlendSample& Sample : BlendSpace->GetSampleData())
			{
				if (Sample.Animation)
				{
					SampleSeq = Sample.Animation;
					break;
				}
			}
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(BlendSpace->GetPreviewMeshPath(), Request.FilePath, SampleSeq);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, SampleSeq, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else if (AnimBlueprintFile::IsAnimBlueprintPath(Request.Utf8Path))
	{
		FAnimBlueprintData Data;
		FString Error;
		if (AnimBlueprintFile::Read(Request.FilePath, Data, Error))
		{
			const UAnimSequence* SampleSeq = FindPreviewSequenceInBlueprint(Data);
			USkeletalMesh* PreviewMesh = LoadPreviewSkeletalMesh(Data.PreviewMeshPath, Request.FilePath, SampleSeq);
			bValidMesh = SetupSkeletalThumbnail(PreviewMesh, SampleSeq, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}
	else
	{
		URenderAsset* Asset = LoadMeshAssetForThumbnail(Request.FilePath, Request.Utf8Path);
		if (!Asset)
		{
			return;
		}

		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			const auto& MeshData = StaticMesh->GetMeshData();
			if (!MeshData.Vertices.IsEmpty() && !MeshData.Indices.IsEmpty())
			{
				StaticMeshComp->SetStaticMesh(StaticMesh);
				SkeletalMeshComp->SetSkeletalMesh(nullptr);
				BoundsBox = StaticMesh->GetRenderBounds();
				bValidMesh = AreBoundsFinite(BoundsBox);
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			bValidMesh = SetupSkeletalThumbnail(SkeletalMesh, nullptr, StaticMeshComp, SkeletalMeshComp, BoundsBox);
		}
	}

	if (!bValidMesh)
	{
		StaticMeshComp->ClearOverrideMaterials();
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	ThumbnailClient->ApplySettings(Engine.GetEditorSettings(), bIsTranslucentPreview);

	PreviewScene->Tick(0.0f);
	if (UWorld* PreviewWorld = PreviewScene->GetWorld())
	{
		PreviewWorld->GetScene().UpdateAllTransforms();
	}

	const FVector Extent = (BoundsBox.Max - BoundsBox.Min) * 0.5f;
	const FBoxSphereBounds Bounds{
		(BoundsBox.Min + BoundsBox.Max) * 0.5f,
		Extent,
		Extent.Size()
	};

	ViewportClient->Resize(ThumbnailResolution, ThumbnailResolution);
	if (!ViewportClient->GetViewTargets().IsValidFor(ThumbnailResolution, ThumbnailResolution))
	{
		StaticMeshComp->ClearOverrideMaterials();
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	ViewportClient->SetViewRotation(FRotator(22.0f, 200.0f, 0.0f));
	ViewportClient->FocusOnBounds(Bounds, false);
	ViewportClient->Draw(Renderer);

	FTexture2D* RenderedColor = ViewportClient->GetColorTarget();
	if (RenderedColor && RenderedColor->GetRawPtr())
	{
		FTextureDesc Desc{};
		Desc.Width = ThumbnailResolution;
		Desc.Height = ThumbnailResolution;
		Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
		Desc.BindFlags = ETextureBindFlags::ShaderResource | ETextureBindFlags::RenderTarget;

		TUniquePtr<FTexture2D> CachedTexture = RenderCommand::CreateTexture2D(Desc);
		if (CachedTexture && CachedTexture->GetRawPtr() && CachedTexture->GetSRV())
		{
			RenderCommand::CopyTexture(CachedTexture.get(), RenderedColor);
			Entry.Texture = std::move(CachedTexture);
			Entry.bFailed = false;
		}
	}

	ThumbnailClient->SetPhysicsOverlay(nullptr, nullptr);
	StaticMeshComp->ClearOverrideMaterials();
	StaticMeshComp->SetStaticMesh(nullptr);
	SkeletalMeshComp->SetSkeletalMesh(nullptr);
}

void FMeshThumbnailRenderer::ClearPendingQueue()
{
	PendingQueue.clear();
}

void FMeshThumbnailRenderer::InvalidatePath(const FString& Utf8Path)
{
	Cache.erase(Utf8Path);
	PendingQueue.erase(
		std::remove_if(PendingQueue.begin(), PendingQueue.end(),
			[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; }),
		PendingQueue.end());
}

void FMeshThumbnailRenderer::InvalidateDirectoryPrefix(const FString& Utf8DirPrefix)
{
	if (Utf8DirPrefix.empty())
	{
		return;
	}

	const auto MatchesPrefix = [&Utf8DirPrefix](const std::string& Path)
	{
		if (Path == Utf8DirPrefix)
		{
			return true;
		}
		if (Path.size() > Utf8DirPrefix.size() && Path.compare(0, Utf8DirPrefix.size(), Utf8DirPrefix) == 0)
		{
			const char Next = Path[Utf8DirPrefix.size()];
			return Next == '/' || Next == '\\';
		}
		return false;
	};

	for (auto It = Cache.begin(); It != Cache.end();)
	{
		if (MatchesPrefix(It->first))
		{
			It = Cache.erase(It);
		}
		else
		{
			++It;
		}
	}

	PendingQueue.erase(
		std::remove_if(PendingQueue.begin(), PendingQueue.end(),
			[&MatchesPrefix](const FPendingRequest& Req) { return MatchesPrefix(Req.Utf8Path); }),
		PendingQueue.end());
}

void FMeshThumbnailRenderer::Release()
{
	PendingQueue.clear();
	Cache.clear();
	StaticMeshComp = nullptr;
	SkeletalMeshComp = nullptr;
	ViewportClient.reset();
	if (PreviewScene)
	{
		PreviewScene->Release();
		PreviewScene.reset();
	}
	bPreviewInitFailed = false;
}
