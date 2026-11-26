#pragma once

#include <format>
#include "Core/Types.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Render/RenderEnums.h"
#include "Render/ShowFlags.h"

class UWorld;

struct FEditorSettings
{
	const uint32 DirectionalShadowMapSizePerLevels[5] = { 256, 512, 1024, 2048, 4096 };
	const uint32 PointShadowMapSizePerLevels[5] = { 128, 256, 512, 1024, 2048 };
	const uint32 SpotShadowMapSizePerLevels[5] = { 128, 256, 512, 1024, 2048 };

	// 에디터 렌더 옵션과 Multiple Viewports 레이아웃의 영구 설정을 한곳에 담는다.
	// 표시 플래그(Show 메뉴). 목록은 Render/ShowFlagsValues.h에 있다.
	FEngineShowFlags ShowFlags{ EShowFlagInitMode::Editor };

	// Toggles
	bool bCacheShadows = true; // r.Shadow.CacheWholeSceneShadows
	EPcfMode PcfMode = EPcfMode::PCSS;
	bool bDecalCulling = true;
	bool bDecalNarrowCulling = true;
	bool bDecalBVHCulling = true;
	bool bDecalOBBTree = true;
	ELightCullingMode LightCullingMode = ELightCullingMode::Tiled;
	ELightingModel LightingModel = ELightingModel::Gouraud;

	// Values
	float SceneDepthMaxRange = 50.0f;
	float CameraSpeed = 1.0f;
	float MouseSensitivity = 1.0f;
	int32 GridSpacing = 1;
	bool bLocationSnap = true;
	float LocationSnapSize = 1.0f;
	// 회전 스냅(도)과 크기 스냅(배율 단위)
	bool bRotationSnap = true;
	float RotationSnapDegrees = 5.0f;
	bool bScaleSnap = true;
	float ScaleSnapSize = 0.25f;
	float MultipleViewportsHorizontal = 0.5f;
	float MultipleViewportsVertical = 0.5f;
	int32 ViewportLayout = 1; // ELayoutMode. 기본은 2x2(QuadSplit)
	int32 MultipleViewportsSingleViewIndex = 0;

    // 미저장 표식은 초기 카메라 값을 유지한다. 위치·회전은 저장하지 않는다.
    float ViewFov[4]{};
    float ViewOrthoWidth[4]{};
    int32 ViewPreset[4]{-1,-1,-1,-1};
    int32 ViewMode[4]{-1,-1,-1,-1};

    // View별 카메라 Transform은 Quaternion 그대로 저장해 재실행 후 같은 시점을 복원한다.
    // 구형 editor.ini에 키가 없으면 Saved가 false로 남아 Adapter의 초기 Transform을 유지한다.
    FVector ViewLocation[4]{};
    FQuat ViewRotation[4]{};
    bool bViewLocationSaved[4]{};
    bool bViewRotationSaved[4]{};

	int32 DirectionalShadowMapSizeLevel = 4;
	int32 PointShadowMapSizeLevel = 2;
	int32 SpotShadowMapSizeLevel = 2;

	bool bGammaCorrection = true;
	float DisplayGamma = 2.2f;
	float ExposureCompensation = 0.0f;

	inline uint32 GetDirectionalShadowMapSize() const { return DirectionalShadowMapSizePerLevels[DirectionalShadowMapSizeLevel]; }
	inline uint32 GetPointShadowMapSize() const { return PointShadowMapSizePerLevels[PointShadowMapSizeLevel]; }
	inline uint32 GetSpotShadowMapSize() const { return SpotShadowMapSizePerLevels[SpotShadowMapSizeLevel]; }
};

class FViewportsPanel;

class FEditorSettingsManager
{
public:
	FEditorSettingsManager() = default;
	~FEditorSettingsManager();

	bool Init();

	void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }

	const FEditorSettings& GetSettings() const { return Settings; }
	// 실행 중 레이아웃 변경을 editor.ini 저장 대상과 동일한 설정 객체에 반영한다.
	FEditorSettings& GetMutableSettings() { return Settings; }
	// 알려진 모든 설정 섹션을 한 번에 다시 써서 editor.ini 값이 서로 덮어쓰이지 않게 저장한다.
	bool SaveSettings() const;
	// editor.ini의 기존 렌더·에디터·Multiple Viewports 값을 같은 설정 객체로 복원한다.
	bool LoadSettings();
    // 종료 저장은 마지막 프레임의 복사본을 사용해 저장 설정을 유지한다.
    void CaptureViewportSettings();
    void ApplyViewportSettings();
	void ApplySceneDepthRange();

private:

    // View별 Transform·투영·표시 설정을 읽어 저장용 스냅샷에 반영한다.
    void ReadViewportSettings(FEditorSettings& Out) const;

	FViewportsPanel* ViewportsPanel = nullptr;
	FEditorSettings Settings;
};
