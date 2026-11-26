#pragma once

#include "Render/RenderEnums.h"
#include "Render/ShowFlags.h"

// 뷰 하나를 "어떻게 그릴지" 정하는 품질·경로 설정. 표시 여부는 FEngineShowFlags가 맡는다.
// 엔진 전체에 하나만 있는 값이라 나중에 CVar(r.*)로 옮기기 쉽도록 따로 둔다.
struct FSceneRenderSettings
{
	EPcfMode PcfMode = EPcfMode::Sample8;
	float ShadowDepthBias = 0.0001f;
	bool bCacheShadows = true; // r.Shadow.CacheWholeSceneShadows: 바뀌지 않은 그림자 타일은 다시 그리지 않는다
	ELightCullingMode LightCullingMode = ELightCullingMode::Tiled;
	ELightingModel LightingModel = ELightingModel::Gouraud;

	// 데칼 컬링 단계별 on/off (성능 비교용)
	bool bDecalCulling = true;
	bool bDecalNarrowCulling = true;
	bool bDecalBVHCulling = true;
	bool bDecalOBBTree = true;

	float SceneDepthRange = 1.0f;     // VisualizeSceneDepth의 최대 거리
	uint32 DirectionalShadowMapSize = 0;
	uint32 PointShadowMapSize = 0;
	uint32 SpotShadowMapSize = 0;

	bool bGammaCorrection = true;
	float DisplayGamma = 2.2f;
	float ExposureCompensation = 0.0f; // 스톱 단위. +1이면 2배 밝게 (UE PostProcess의 Exposure Compensation)
	bool bUsePreviewSkybox = false;
};

// 렌더러에 넘기는 뷰 하나의 표시 플래그와 렌더 설정 묶음.
struct FSceneRenderOptions
{
	FEngineShowFlags ShowFlags{ EShowFlagInitMode::Game };
	FSceneRenderSettings Settings;
};
