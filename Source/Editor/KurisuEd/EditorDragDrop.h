#pragma once

// 에디터 패널 간 드래그앤드롭 페이로드 식별자.
// ImGui는 이 문자열로 소스와 타깃을 짝지으므로 양쪽이 같은 값을 써야 한다.
// (ImGui 제한: 널 포함 32자 이내)
namespace EditorDragDrop
{
	// 페이로드 내용: UTexture2D* 한 개
	inline constexpr const char* Texture = "ASSET_TEXTURE";

	// 페이로드 내용: UFont* 한 개
	inline constexpr const char* Font = "ASSET_FONT";

	// 페이로드 내용: 에셋 경로 문자열
	inline constexpr const char* ParticleSystem = "ASSET_PARTICLE_SYSTEM";

	// 페이로드 내용: .mat 에셋 경로 문자열
	inline constexpr const char* Material = "ASSET_MATERIAL";

	// 페이로드 내용: .pmat 에셋 경로 문자열
	inline constexpr const char* PhysicalMaterial = "ASSET_PHYSICAL_MATERIAL";

	// 페이로드 내용: 스태틱/스켈레탈 메시 에셋 경로 문자열
	inline constexpr const char* StaticMesh = "ASSET_STATIC_MESH";
	inline constexpr const char* SkeletalMesh = "ASSET_SKELETAL_MESH";

	inline constexpr const char* Component = "COMPONENT";
	inline constexpr const char* Actor = "SCENE_ACTOR";
}
