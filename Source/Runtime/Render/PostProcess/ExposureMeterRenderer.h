#pragma once

#include "Core/Types.h"
#include "Render/PipelineState.h"

#include <d3d11.h>
#include <wrl/client.h>

class FRenderer;
class FTexture2D;

// 카메라 노출계. HDR 장면의 중앙 중점 평균 휘도를 GPU에서 구하고 몇 프레임 뒤에 CPU로 읽어 온다.
// 측정은 렌더러가, 읽은 값은 게임(뷰파인더)이 쓰므로 결과를 정적 값으로 둔다.
class FExposureMeterRenderer
{
public:
	void Init(FRenderer* InRenderer);

	// 측정 패스의 렌더 타깃. FSceneRenderer가 그래프에 import해서 바인딩한다
	FTexture2D* GetTarget() const { return Target.get(); }

	// 현재 바인딩된 Target에 측정값을 쓰고, 평균을 만들어 읽기 대기열에 넣는다
	bool Render(FTexture2D* SceneHDR);

	// 가장 최근에 읽어 온 평균 휘도(선형). 아직 읽은 적이 없으면 false
	static bool GetAverageLuminance(float& OutLuminance);

	// 눈 적응: 측정값이 가리키는 EV100 쪽으로 현재 EV100을 시간에 따라 조금씩 옮기고 돌려준다.
	// 한 프레임에 여러 뷰가 불러도 한 번만 움직인다
	static float UpdateAdaptedEV100(const struct FPostProcessSettings& Settings);
	// 톤맵이 거는 감마. 렌더러가 매 프레임 넣고, 노출계 목표와 뷰파인더 바늘이 같은 값을 쓴다
	static void SetDisplayGamma(float InGamma) { DisplayGamma = InGamma; }
	static float GetDisplayGamma() { return DisplayGamma; }
	// 가장 최근 적응 EV100. 게임 쪽(사진 모드 AE 다이얼)이 읽는다. 아직 적응한 적이 없으면 false
	static bool GetAdaptedEV100(float& OutEV100)
	{
		OutEV100 = AdaptedEV100;
		return bHasAdapted;
	}

private:
	void ReadBack();

	static constexpr uint32 TargetSize = 256;
	// GPU가 앞서 달리므로 3프레임 전에 복사한 것을 읽으면 기다리지 않는다
	static constexpr uint32 NumStaging = 3;

	FRenderer* Renderer = nullptr;
	FShaderProgram* Shader = nullptr;
	FPipelineState PipelineState;
	TUniquePtr<FTexture2D> Target;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> MipChain;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> MipChainSRV;
	Microsoft::WRL::ComPtr<ID3D11Texture2D> Staging[NumStaging];
	bool bStagingPending[NumStaging]{};
	uint32 WriteIndex = 0;
	uint32 LastMip = 0;

	static inline float AverageLuminance = 0.18f;
	static inline bool bHasReading = false;
	static inline float AdaptedEV100 = 0.0f;
	static inline bool bHasAdapted = false;
	static inline float LastAdaptTime = -1.0f;
	static inline float DisplayGamma = 1.0f;
};
