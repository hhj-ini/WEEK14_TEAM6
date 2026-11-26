// 반해상도 고스트 → 풀해상도 SceneColor에 가산 블렌드로 더한다.
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

Texture2D GhostTexture : register(t0);
Texture2D DirtTexture : register(t1); // 화면 UV에 고정된 렌즈 먼지 (흑백)
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
    float2 Size;
    GhostTexture.GetDimensions(Size.x, Size.y);
    // 타깃은 풀해상도라 고스트 텍스처 크기의 2배다
    float2 UV = Input.Position.xy / (Size * 2.0f);

    // 고스트를 읽어 Intensity를 곱해 돌려준다 (블렌드가 더하기를 해 준다)
    float4 Color = GhostTexture.Sample(LinearClamp, UV);
    Color.rgb *= Intensity;

    // Dirt — DirtTexture를 화면 UV로 읽어 DirtIntensity만큼 플레어(Color.rgb)에 곱한다
    float4 DirtColor = DirtTexture.Sample(LinearClamp, UV);
    Color.rgb *= (1 + DirtColor.r * DirtIntensity);

    return float4(Color.rgb, 0);
}
