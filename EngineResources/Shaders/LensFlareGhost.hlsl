// 반해상도 조리개 보케(t0) + 원본 밝은 부분(t1) → 반해상도 고스트 + 광학 할로.
// 화면 중앙 기준으로 렌즈 군별 배율·AR 코팅 색·스펙트럼 분산·선명도를 조합해 사실적인 다중 반사 고스트를 만든다.
cbuffer LensFlareConstants : register(b0)
{
    float Intensity;
    float Threshold;
    float GhostCount;
    float GhostDispersal;
    float HaloWidth;
    float ChromaticDistortion;
    float DirtIntensity;
    float HaloIntensity;
    float BladeCount;
    float BokehRadius;
    float Padding0;
    float Padding1;
};

Texture2D BokehTexture : register(t0);
Texture2D BrightTexture : register(t1);
SamplerState LinearClamp : register(s0);

struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
    static const float2 Positions[6] =
    {
        float2(-1, 1),
        float2(1, 1),
        float2(-1, -1),

        float2(-1, -1),
        float2(1, 1),
        float2(1, -1)
    };

    VSOutput Output;
    Output.Position = float4(Positions[VertexID], 0, 1);
    return Output;
}

// 고스트 i는 광원을 화면 중앙 기준으로 1/Scale배 옮기고 1/|Scale|배 키운 상이다.
// 음수면 광원 반대편, 양수면 광원 쪽, |Scale| < 1이면 바깥쪽에 크게, > 1이면 중앙 가까이 작고 또렷하게 생긴다.
static const float GhostScales[16] =
{
    -0.92f, -0.48f, -2.15f,  1.75f,
    -0.68f, -3.40f,  2.60f, -0.34f,
    -1.38f,  1.28f, -0.56f, -4.60f,
     2.10f, -0.82f, -1.80f,  3.70f
};

// 실제 멀티코팅(AR 코팅) 렌즈의 물리 반사 색조: 에메랄드/틸 그린, 시안/블루, 마젠타/바이올렛, 웜 앰버 위주
static const float3 GhostTints[16] =
{
    float3(0.48f, 0.96f, 0.72f), float3(0.78f, 0.52f, 0.96f), float3(1.00f, 0.76f, 0.42f), float3(0.46f, 0.78f, 1.00f),
    float3(0.72f, 0.95f, 0.60f), float3(0.70f, 0.92f, 1.00f), float3(0.95f, 0.68f, 0.85f), float3(0.42f, 0.72f, 0.98f),
    float3(0.55f, 0.92f, 0.84f), float3(1.00f, 0.84f, 0.55f), float3(0.62f, 0.58f, 1.00f), float3(1.00f, 0.88f, 0.74f),
    float3(0.45f, 0.94f, 0.78f), float3(0.96f, 0.74f, 0.52f), float3(0.74f, 0.62f, 1.00f), float3(0.84f, 0.96f, 0.78f)
};

// 고스트별 상대 반사율 (일부는 또렷한 주역 고스트, 일부는 은은한 2차 반사)
static const float GhostWeights[16] =
{
    0.95f, 0.65f, 1.15f, 0.55f,
    0.80f, 1.00f, 0.48f, 0.42f,
    0.70f, 0.52f, 0.58f, 0.85f,
    0.45f, 0.62f, 0.68f, 0.50f
};

// 0 = 림이 살아있는 다각형 보케 고스트(BokehTexture), 1 = 초점에 가까운 또렷한 점 반사(BrightTexture)
static const float GhostSharpness[16] =
{
    0.00f, 0.00f, 0.72f, 0.35f,
    0.10f, 0.88f, 0.55f, 0.00f,
    0.20f, 0.40f, 0.00f, 0.92f,
    0.60f, 0.05f, 0.30f, 0.78f
};

// 고스트별 색분산 배율 (바깥쪽 큰 고스트일수록 유리 통과가 길어 스펙트럼 번짐이 넓다)
static const float GhostDispersions[16] =
{
    1.15f, 1.90f, 0.65f, 0.80f,
    1.45f, 0.50f, 0.75f, 2.30f,
    1.05f, 0.95f, 1.65f, 0.45f,
    0.85f, 1.30f, 1.00f, 0.60f
};

// 5-탭 연속 스펙트럼 색수차 샘플링 (각 채널 가중치 합 = 1.0으로 백색 보존)
float3 SampleSpectral(Texture2D Tex, float2 UV, float2 Offset)
{
    static const float Offsets[5] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
    static const float3 Weights[5] =
    {
        float3(0.55f, 0.00f, 0.00f), // Red
        float3(0.45f, 0.35f, 0.00f), // Amber
        float3(0.00f, 0.50f, 0.10f), // Green
        float3(0.00f, 0.15f, 0.45f), // Cyan
        float3(0.00f, 0.00f, 0.45f)  // Blue-Violet
    };

    float3 Accum = 0.0f;
    [unroll]
    for (int s = 0; s < 5; ++s)
    {
        Accum += Tex.SampleLevel(LinearClamp, UV + Offset * Offsets[s], 0).rgb * Weights[s];
    }
    return Accum;
}

float ComputeGhostMask(float2 SampleUV, float AbsScale, float Aspect)
{
    // 부드러운 프레임 테두리 페이드 (Clamp 꼬리 제거)
    float2 FrameFade = smoothstep(0.0f, 0.10f, min(SampleUV, 1.0f - SampleUV));
    float Mask = FrameFade.x * FrameFade.y;

    // 원형 렌즈 경통 비네팅
    float2 Centered = (SampleUV - 0.5f) * float2(Aspect, 1.0f);
    float RadialDist = length(Centered);
    float RadialFalloff = smoothstep(0.95f, 0.10f, RadialDist);
    Mask *= RadialFalloff * RadialFalloff;

    // |Scale| > 1.2일 때 화면 전체가 중앙의 작은 사각형으로 축소되어 사각 경계가 보이는 현상을 원형 마스킹으로 완전히 제거한다
    float SmallScaleFactor = saturate((AbsScale - 1.15f) * 1.5f);
    float CircularBarrel = smoothstep(0.49f, 0.30f, length(SampleUV - 0.5f));
    Mask *= lerp(1.0f, CircularBarrel, SmallScaleFactor);

    return Mask;
}

float4 mainPS(VSOutput Input) : SV_Target
{
    float2 Size;
    BokehTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;

    // Dispersal 기본값 0.25에서 표의 배율이 그대로 쓰이도록 맞춘다. 클수록 고스트가 바깥으로 퍼지고 커진다
    const float Spread = max(GhostDispersal * 4.0f, 0.05f);

    const float Aspect = Size.x / Size.y;
    const float2 AspectVec = float2(Aspect, 1.0f);
    float2 CenterToUV = (UV - 0.5f) * AspectVec;
    float PixelDist = length(CenterToUV);
    float2 RadialDir = CenterToUV / max(PixelDist, 1e-4f);
    // 광축에서 멀수록 색분산이 강해지는 실제 횡색수차 특성 반영
    float2 ChromaBase = (RadialDir / AspectVec) * ChromaticDistortion * (0.45f + 0.95f * saturate(PixelDist));

    float3 Result = 0.0f;
    const int Count = min((int)GhostCount, 16);
    for (int i = 0; i < Count; ++i)
    {
        // P에 찍힐 고스트의 광원은 중앙에서 Scale배 떨어진 곳에 있다 (gather)
        const float Scale = GhostScales[i] / Spread;
        const float AbsScale = abs(Scale);
        float2 SampleUV = 0.5f + (UV - 0.5f) * Scale;

        float EdgeWeight = ComputeGhostMask(SampleUV, AbsScale, Aspect);
        if (EdgeWeight <= 1e-4f)
            continue;

        // 화면상 색분산 폭을 고스트 배율과 분산 계수에 맞춘다
        float2 DispersionOffset = ChromaBase * (AbsScale * GhostDispersions[i]);

        float Sharpness = GhostSharpness[i];
        float3 SampleColor = 0.0f;
        if (Sharpness < 0.95f)
            SampleColor += SampleSpectral(BokehTexture, SampleUV, DispersionOffset) * (1.0f - Sharpness);
        if (Sharpness > 0.05f)
            SampleColor += SampleSpectral(BrightTexture, SampleUV, DispersionOffset * 0.65f) * Sharpness;

        // 커진 고스트(|Scale|<1)는 빛이 넓은 면적에 퍼져 자연스럽게 어두워지고, 작은 고스트는 또렷함을 유지한다
        float Energy = pow(saturate(AbsScale), 1.35f);
        Result += SampleColor * GhostTints[i] * GhostWeights[i] * EdgeWeight * Energy;
    }

    // Optical Halo Arc — 광원의 외곽 거리(rL)에 비례해 반지름(rH = R0 + k * rL)이 함께 움직이는 얇은 무지개 회절 호
    if (HaloIntensity > 1e-4f && HaloWidth > 1e-4f)
    {
        const float BaseRadius = clamp(HaloWidth * 2.4f, 0.02f, 0.55f);
        const float MoveFactor = 0.35f; // 광원이 외곽으로 갈수록 할로도 반대편 바깥으로 이동 (k < 1로 얇은 두께 유지)
        const float ChromaSpread = max(ChromaticDistortion, 0.002f) * 2.4f;

        if (PixelDist > BaseRadius - ChromaSpread)
        {
            static const float SpecOffsets[5] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
            static const float3 SpecWeights[5] =
            {
                float3(0.55f, 0.00f, 0.00f),
                float3(0.45f, 0.35f, 0.00f),
                float3(0.00f, 0.50f, 0.10f),
                float3(0.00f, 0.15f, 0.45f),
                float3(0.00f, 0.00f, 0.45f)
            };

            float3 HaloColor = 0.0f;
            [unroll]
            for (int s = 0; s < 5; ++s)
            {
                // rH = BaseRadius + SpecOffset * ChromaSpread + MoveFactor * rL 을 rL에 대해 역산
                float SampleR = (PixelDist - (BaseRadius + SpecOffsets[s] * ChromaSpread)) / MoveFactor;
                if (SampleR <= 0.04f || SampleR >= 0.78f)
                    continue;

                float2 HaloUV = 0.5f - (RadialDir * SampleR) / AspectVec;
                float2 HaloInside = smoothstep(0.0f, 0.06f, min(HaloUV, 1.0f - HaloUV));
                // 정면(rL≈0)에서는 경통 스침 반사가 없으므로 사라지고, 외곽 광원일수록 선명한 호로 나타난다
                float OffAxisWeight = smoothstep(0.06f, 0.24f, SampleR)
                                    * smoothstep(0.76f, 0.48f, SampleR);
                float W = OffAxisWeight * HaloInside.x * HaloInside.y;

                if (W > 1e-4f)
                {
                    float3 BrightVal = BrightTexture.SampleLevel(LinearClamp, HaloUV, 0).rgb;
                    float3 BokehVal = BokehTexture.SampleLevel(LinearClamp, HaloUV, 0).rgb;
                    float3 SampleVal = BrightVal * 0.75f + BokehVal * 0.45f;
                    HaloColor += SampleVal * SpecWeights[s] * W;
                }
            }

            // 고스트와 같은 노출 전 HDR 스케일을 유지하되 극단적 과포화만 부드럽게 막는다
            float HaloLuma = dot(HaloColor, float3(0.2126f, 0.7152f, 0.0722f));
            HaloColor *= 40.0f / (40.0f + HaloLuma);

            Result += HaloColor * (HaloIntensity * 1.45f);
        }
    }

    return float4(Result, 1.0f);
}
