// 반해상도 밝은 부분 → 반해상도. 광원 점 하나하나를 조리개 모양(BladeCount 각형)으로 번지게 한다.
// Ghost 패스가 이 결과를 배율대로 복사하므로 고스트도 같은 N각형이 된다.
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
    float BokehRadius; // 반해상도 px
    float Padding0;
    float Padding1;
};

Texture2D BrightTexture : register(t0);
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

float4 mainPS(VSOutput Input) : SV_Target
{
    static const float PI = 3.14159265f;
    static const int SampleCount = 128; // 반경이 커져도 샘플 간격이 벌어져 점박이가 되지 않게

    float2 Size;
    BrightTexture.GetDimensions(Size.x, Size.y);
    float2 UV = Input.Position.xy / Size;
    float2 TexelSize = 1.0f / Size;
    float Aspect = Size.x / Size.y;

    if (BokehRadius <= 0.25f)
    {
        return float4(BrightTexture.SampleLevel(LinearClamp, UV, 0).rgb, 1.0f);
    }

    float Blades = max(BladeCount, 3.0f);
    float Wedge = 2.0f * PI / Blades;
    // 조리개 다각형이 화면 축과 딱 맞지 않도록 자연스러운 회전각을 준다
    float ApertureRotation = 0.22f;
    // 광원이 렌즈 중심에서 벗어날수록 경통에 의해 조리개 한쪽이 잘리는 캣츠아이(광학 비네팅)
    float2 OffAxis = (UV - 0.5f) * float2(Aspect, 1.0f);

    float3 Result = 0.0f;
    float ReferenceWeightSum = 0.0f;

    [unroll]
    for (int i = 0; i < SampleCount; ++i)
    {
        float NormDist = sqrt((i + 0.5f) / SampleCount); // 0(중심) ~ 1(다각형 경계)
        float SampleAngle = i * 2.39996323f; // 황금각

        // BladeCount 각형 조리개 모양으로 반경 보정
        float Phi = fmod(SampleAngle, Wedge) - Wedge * 0.5f;
        float PolyScale = cos(PI / Blades) / max(cos(Phi), 0.1f);
        float RotatedAngle = SampleAngle + ApertureRotation;
        float2 Dir = float2(cos(RotatedAngle), sin(RotatedAngle));

        // 실제 렌즈 내부 반사 고스트는 구면수차로 인해 테두리(림)가 밝고 안쪽은 반투명하다
        float RimDelta = (NormDist - 0.87f) / 0.11f;
        float Rim = (abs(RimDelta) < 4.0f) ? exp(-RimDelta * RimDelta) : 0.0f;
        float InnerDelta = (NormDist - 0.52f) / 0.14f;
        float InnerRing = (abs(InnerDelta) < 4.0f) ? (0.28f * exp(-InnerDelta * InnerDelta)) : 0.0f;
        float EdgeAA = smoothstep(1.0f, 0.93f, NormDist);
        float RadialWeight = (0.36f + InnerRing + 1.55f * Rim) * EdgeAA;

        // 캣츠아이 비네팅: 축 외곽 광원일수록 경통 원에 의해 한쪽이 부드럽게 잘린다
        float2 UnitPos = Dir * NormDist;
        float CatEyeDist = length(UnitPos - OffAxis * 0.48f);
        float CatEyeMask = smoothstep(1.12f, 0.92f, CatEyeDist);

        float2 SampleOffsetHalfPx = Dir * (NormDist * PolyScale * BokehRadius);
        float3 Neighbor = BrightTexture.SampleLevel(LinearClamp, UV + SampleOffsetHalfPx * TexelSize, 0).rgb;

        Result += Neighbor * (RadialWeight * CatEyeMask);
        ReferenceWeightSum += RadialWeight;
    }

    return float4(Result / max(ReferenceWeightSum, 1e-4f), 1.0f);
}
