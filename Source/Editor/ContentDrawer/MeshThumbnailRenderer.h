#pragma once

#include "Core/Types.h"
#include "Render/Texture2D.h"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

class FPreviewScene;
class FEditorViewportClient;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UEditorEngine;
class FRenderer;

// 콘텐츠 드로어의 .obj / .fbx.bin / .anim.bin / .blendspace / .animblueprint / .mat / .phys 에셋용 오프스크린 3D 썸네일 렌더러·캐시.
// 프레임 드랍을 막기 위해 미생성 썸네일은 큐에 넣고 프레임당 1개씩 렌더한다.
class FMeshThumbnailRenderer
{
public:
	FMeshThumbnailRenderer();
	~FMeshThumbnailRenderer();

	FMeshThumbnailRenderer(const FMeshThumbnailRenderer&) = delete;
	FMeshThumbnailRenderer& operator=(const FMeshThumbnailRenderer&) = delete;

	static bool IsSupportedMeshFile(const fs::path& FilePath, const FString& Utf8Path);

	// 썸네일 배경 체커의 가로 칸 수. 콘텐츠 브라우저의 아이콘 타일도 같은 칸 수로 그린다.
	static constexpr int CheckerCellsAcross = 8;

	// 캐시된 썸네일 SRV를 돌려준다. 아직 없거나 파일이 갱신됐으면 렌더 큐에 넣고 Invalid를 반환한다.
	ImTextureID GetOrRequestThumbnail(const fs::path& FilePath, const FString& Utf8Path, fs::file_time_type LastWriteTime);

	// .mat / .obj / .fbx.bin 에셋 경로 또는 내장 에셋 키("DefaultMaterial", "Cube" 등)의 3D 썸네일을 조회/요청한다.
	ImTextureID GetOrRequestAssetThumbnail(const FString& AssetPathOrKey);
	ImTextureID GetOrRequestMaterialThumbnail(const FString& MaterialPathOrKey) { return GetOrRequestAssetThumbnail(MaterialPathOrKey); }

	// 프레임당 1개씩 대기 중인 3D 썸네일을 오프스크린 타깃에 그려 캐시한다 (백버퍼 패스 전에 호출).
	void RenderPending(UEditorEngine& Engine, FRenderer* Renderer);

	void ClearPendingQueue();
	void InvalidatePath(const FString& Utf8Path);
	void InvalidateDirectoryPrefix(const FString& Utf8DirPrefix);
	void Release();

private:
	struct FThumbnailEntry
	{
		TUniquePtr<FTexture2D> Texture;
		fs::file_time_type LastWriteTime{};
		bool bFailed = false;
		// 찍을 때의 에디터 테마(EEditorTheme). 배경색이 테마를 따르므로 테마가 바뀌면 다시 찍는다.
		unsigned char Theme = 0;
	};

	struct FPendingRequest
	{
		fs::path FilePath;
		FString Utf8Path;
		fs::file_time_type LastWriteTime{};
	};

	bool EnsurePreviewScene(UEditorEngine& Engine);
	void RenderMeshToCache(const FPendingRequest& Request, UEditorEngine& Engine, FRenderer* Renderer);

	static constexpr uint32 ThumbnailResolution = 128;

	TUniquePtr<FPreviewScene> PreviewScene;
	TUniquePtr<FEditorViewportClient> ViewportClient;
	UStaticMeshComponent* StaticMeshComp = nullptr;
	USkeletalMeshComponent* SkeletalMeshComp = nullptr;
	bool bPreviewInitFailed = false;

	std::unordered_map<std::string, FThumbnailEntry> Cache;
	std::vector<FPendingRequest> PendingQueue;
};
