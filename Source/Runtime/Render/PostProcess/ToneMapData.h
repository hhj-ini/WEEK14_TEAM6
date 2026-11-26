#pragma once

struct alignas(16) FToneMapData
{
	float InvGamma = 1.0f / 2.2f;
	float bGammaCorretion = 1.0f;
	float Exposure = 1.0f; // 톤매핑 전에 곱하는 노출 배율. 1보다 작으면 어두워져 하이라이트가 더 살아난다
	float Padding = 0.0f;
};

static_assert(sizeof(FToneMapData) == 16);

