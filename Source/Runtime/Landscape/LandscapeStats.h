#pragma once

#include "Core/Types.h"
#include "Core/Stats/LightweightStats.h"
#include "Landscape/LandscapeSharedBuffers.h"

// 지형 렌더 통계 ("stat landscape"). UE의 STAT_LandscapeDrawCalls 등에 대응한다.
// 컬링은 엔진(FScene BVH)이 컴포넌트 경계 상자로 이미 하므로,
// 여기서는 "컬링을 통과해 GetDynamicMeshElements까지 온 컴포넌트"를 센다.

namespace LandscapeCycleStats
{
	DECLARE_CYCLE_STAT("Landscape: Bake Heightmap", STAT_LandscapeBakeHeightmap);
}

struct FLandscapeFrameStats
{
	uint32 DrawnComponents = 0;                 // 메인 뷰에서 그린 컴포넌트 수 (= 메인 뷰 드로우 콜)
	uint32 ShadowDraws = 0;                     // 그림자 패스에서 그린 컴포넌트 수 (캐스케이드마다 따로 센다)
	uint64 Triangles = 0;                       // 메인 뷰에서 그린 삼각형 수
	uint32 LODCounts[LandscapeNumLODs] = {};    // LOD별 컴포넌트 수. 7단계 전까지는 모두 LOD 0
};

class FLandscapeStats
{
public:
	// 프록시 생성·소멸 때 부른다. 씬에 있는 지형 컴포넌트 전체 수
	static void AddProxy() { ++NumComponents; }
	static void RemoveProxy() { if (NumComponents > 0) --NumComponents; }

	static void AddMainDraw(int32 LOD, uint32 InTriangles)
	{
		++Current.DrawnComponents;
		Current.Triangles += InTriangles;
		if (LOD >= 0 && LOD < LandscapeNumLODs)
			++Current.LODCounts[LOD];
	}
	static void AddShadowDraw() { ++Current.ShadowDraws; }

	// 프레임이 끝날 때 한 번 (FStatRegistry::EndFrame 옆). 이번 프레임 값을 표시용으로 넘기고 비운다
	static void EndFrame()
	{
		Last = Current;
		Current = FLandscapeFrameStats{};
	}

	static const FLandscapeFrameStats& GetLastFrame() { return Last; }
	static uint32 GetNumComponents() { return NumComponents; }

private:
	inline static uint32 NumComponents = 0;
	inline static FLandscapeFrameStats Current;
	inline static FLandscapeFrameStats Last;
};
