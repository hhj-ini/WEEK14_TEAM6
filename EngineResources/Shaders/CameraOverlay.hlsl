cbuffer CameraOverlayConstants : register(b0)
{
    float4 FadeColor; // rgb = 색, a = FadeAmount
    float2 ViewSize;
    float VignetteIntensity; // 0 = 없음. 0.5~1.5가 보기 좋음
    float VignettePower; // 가장자리 경도
    float LetterboxAmount; // 0~1
    float LetterboxAspect; // 2.39 = 시네마스코프
    float2 TanHalfFov; // 화면 가장자리까지의 tan(반화각). 0이면 렌즈 비네팅 끔
    float PupilRadius; // 경통 반지름 1 기준 동공 반지름 = 최대 개방 N / 현재 N
    float LensVignetteAmount; // 경통 깊이 k. 어긋남 d = k * tanθ
    float BarrelRadius; // 최대 개방 동공 반지름 1 기준 경통 반지름 (>= 1)
    float Padding;
};

// 조리개와 상관없이 화각만으로 정해지는 감쇠
float NaturalVignette(float TanTheta)
{
    const float InvCosSquared = 1.0f + TanTheta * TanTheta; // = 1 / cos²θ
    return 1.0f / (InvCosSquared * InvCosSquared);
}

// 경통 원(반지름 BarrelRadius)과 동공 원(반지름 PupilRadius)이 PupilOffset만큼 어긋났을 때 동공이 살아남는 비율
float OpticalVignette(float TanTheta)
{
    const float PupilOffset = LensVignetteAmount * TanTheta;
    if (PupilRadius + PupilOffset <= BarrelRadius)
    {
        return 1.f;
    }
    else if (PupilOffset >= BarrelRadius + PupilRadius)
    {
        return 0.f;
    }
    else
    {
        const float OffsetSq = PupilOffset * PupilOffset;
        const float BarrelSq = BarrelRadius * BarrelRadius;
        const float PupilSq = PupilRadius * PupilRadius;
        // 경통 중심-동공 중심-교점 삼각형에 코사인 법칙. 오차로 ±1을 넘으면 acos가 NaN이라 막는다
        const float BarrelHalfAngle = acos(clamp((OffsetSq + BarrelSq - PupilSq) / (2.0f * PupilOffset * BarrelRadius), -1.0f, 1.0f));
        const float PupilHalfAngle = acos(clamp((OffsetSq + PupilSq - BarrelSq) / (2.0f * PupilOffset * PupilRadius), -1.0f, 1.0f));
        // 활꼴 = 부채꼴 - 삼각형. 반각이 90도를 넘으면 cos가 음수라 반원보다 큰 활꼴도 같은 식으로 나온다
        const float BarrelSegment = BarrelSq * (BarrelHalfAngle - sin(BarrelHalfAngle) * cos(BarrelHalfAngle));
        const float PupilSegment = PupilSq * (PupilHalfAngle - sin(PupilHalfAngle) * cos(PupilHalfAngle));
        const float OverlapArea = BarrelSegment + PupilSegment;
        return saturate(OverlapArea / (3.14159265f * PupilSq));
    }
}

struct FPSInput
{
    float4 Position : SV_POSITION;
};

// ToneMapShader.hlsl과 같은 풀스크린 삼각형 (정점 버퍼 없이 Draw(3))
FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return Output;
}

float4 mainPS(FPSInput Input) : SV_Target
{
    const float2 UV = Input.Position.xy / ViewSize;
    const float ViewAspect = ViewSize.x / ViewSize.y;

    // 1) Letterbox: 보이는 높이 비율 = ViewAspect / TargetAspect, 나머지를 위아래 반씩
    const float BarHeight = saturate(0.5f * (1.0f - ViewAspect / LetterboxAspect)) * LetterboxAmount;
    if (UV.y < BarHeight || UV.y > 1.0f - BarHeight)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    // 2) Vignette: 화면비 보정한 중심 거리 → 원형 유지 (보정 안 하면 가로로 긴 타원)
    const float2 CenterOffset = (UV - 0.5f) * float2(ViewAspect, 1.0f);
    float Vignette = pow(saturate(1.0f - dot(CenterOffset, CenterOffset) * VignetteIntensity), VignettePower);

    // NDC(-1~1)에 반화각 tan을 곱하면 픽셀 시선의 tanθ가 바로 나온다
    if (LensVignetteAmount > 0.0f)
    {
        const float2 NDC = UV * 2.0f - 1.0f;
        const float TanTheta = length(NDC * TanHalfFov);
        const float LensVignette = NaturalVignette(TanTheta) * OpticalVignette(TanTheta);
        // 톤맵 뒤 감마 값에 덮으므로, 선형 감쇠를 감마로 바꿔야 실제보다 어두워지지 않는다
        Vignette *= pow(saturate(LensVignette), 1.0f / 2.2f);
    }

    // 3) Fade + Vignette를 블렌드 식 하나로 (위 유도)
    const float FadeAmount = FadeColor.a;
    const float OutAlpha = 1.0f - Vignette * (1.0f - FadeAmount);
    const float3 OutColor = (OutAlpha > 1e-4f) ? FadeColor.rgb * (FadeAmount / OutAlpha) : float3(0.0f, 0.0f, 0.0f);
    return float4(OutColor, OutAlpha);
}