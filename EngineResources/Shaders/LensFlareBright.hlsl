// 풀해상도 HDR 장면 → 반해상도. Threshold를 넘은 밝은 부분만 남긴다.
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

Texture2D SceneColorTexture : register(t0);
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
    // 타깃이 반해상도이므로 출력 픽셀 위치를 출력 크기로 나눠 UV를 얻는다
    float2 OutputSize;
    SceneColorTexture.GetDimensions(OutputSize.x, OutputSize.y);
    OutputSize *= 0.5f;
    float2 UV = Input.Position.xy / OutputSize;

    // 장면 색을 읽어 부드러운 소프트 니(soft knee)로 Threshold를 넘은 밝은 광원만 추출한다
    float3 Color = max(SceneColorTexture.Sample(LinearClamp, UV).rgb, 0.0f);
    float Luma = dot(Color, float3(0.2126f, 0.7152f, 0.0722f));
    float Knee = max(Threshold * 0.35f, 0.05f);
    float Soft = clamp(Luma - Threshold + Knee, 0.0f, 2.0f * Knee);
    Soft = (Soft * Soft) / (4.0f * Knee + 1e-4f);
    float Contribution = max(Soft, Luma - Threshold) / max(Luma, 1e-4f);
    // 극단적 파이어플라이 픽셀이 보케 커널 전체를 터뜨리지 않도록 상한을 둔다
    Color = min(Color * Contribution, 64.0f);
    return float4(Color, 1.0f);
}
