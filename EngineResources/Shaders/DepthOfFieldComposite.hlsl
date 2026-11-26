// 선명한 원본과 반해상도 블러를 CoC 크기에 따라 섞어 SceneColor에 쓴다.
Texture2D SceneColorTexture : register(t0);
Texture2D<float> CoCTexture : register(t1);
Texture2D FarTexture : register(t2);
Texture2D NearTexture : register(t3);
SamplerState LinearClampSampler : register(s0);

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
    static const float BlendRadiusPx = 8.f;
    
    int3 PixelPosition = int3(int2(Input.Position.xy), 0);
    float3 SharpColor = SceneColorTexture.Load(PixelPosition).rgb;
    
    float W, H;
    SceneColorTexture.GetDimensions(W, H);
    float2 UV = Input.Position.xy / float2(W, H);
    float4 Far = FarTexture.SampleLevel(LinearClampSampler, UV, 0);
    float4 Near = NearTexture.SampleLevel(LinearClampSampler, UV, 0);
    float CoC = CoCTexture.Load(PixelPosition);

    // Sharp↔Far를 far CoC로 섞고, 그 위에 Near를 Near.a로 덮는다
    // near 픽셀도 Far(배경 또는 자기 색)로 미리 흐려 둬야, 전경 테두리의 반투명 부분에 선명한 원본이 비치지 않는다
    float FarAmount = saturate(abs(CoC) / BlendRadiusPx);
    float3 Mid = lerp(SharpColor, Far.rgb, FarAmount);
    float3 Color = lerp(Mid, Near.rgb, Near.a);

    return float4(Color, 1);
}
