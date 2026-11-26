// Setup 패스가 만든 부호 있는 CoC(음수 = near, 양수 = far)를 색으로 칠한다.
Texture2D<float> CoCTexture : register(t0);
Texture2D SceneColorTexture : register(t1);

// CoC는 픽셀 반경이라, 블러가 허용할 최대 반경으로 나눠야 near/far 모두 0~1 색으로 구별된다
static const float MaxCoCRadiusPx = 32.f;

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
    int3 PixelPosition = int3(int2(Input.Position.xy), 0);
    float CoCPx = CoCTexture.Load(PixelPosition);
    float3 SceneColor = SceneColorTexture.Load(PixelPosition).rgb;

    // 장면을 어둡게 깔아야 CoC 색과 원래 물체 색이 헷갈리지 않는다
    float Luma = dot(SceneColor, float3(0.2126, 0.7152, 0.0722));
    float3 Gray = Luma.xxx * 0.3;

    // 작은 CoC도 눈에 띄게 제곱근으로 끌어올린다 (보기만 바뀌고 블러는 그대로)
    float Amount = sqrt(saturate(abs(CoCPx) / MaxCoCRadiusPx));
    float3 CoCColor = CoCPx < 0 ? float3(0, 1, 0) : float3(0, 0, 1);

    // 강도는 lerp에서 한 번만 써야 제곱으로 약해지지 않는다
    float3 Result = lerp(Gray, CoCColor, Amount);
    return float4(Result, 1);
}
