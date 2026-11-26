// 풀해상도 장면 색 + CoC를 반해상도 한 장(RGB=색, A=CoC 픽셀 반경)으로 줄인다.
Texture2D SceneColorTexture : register(t0);
Texture2D<float> CoCTexture : register(t1);

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
    // 반해상도 픽셀 하나 = 풀해상도 2x2 픽셀 묶음
    int2 HalfPixel = int2(Input.Position.xy);

    // 2x2 묶음에서 CoC 값을 구한다
    int2 Base = HalfPixel * 2;
    float c00 = CoCTexture.Load(int3(Base + int2(0, 0), 0));
    float c10 = CoCTexture.Load(int3(Base + int2(1, 0), 0));
    float c01 = CoCTexture.Load(int3(Base + int2(0, 1), 0));
    float c11 = CoCTexture.Load(int3(Base + int2(1, 1), 0));
    
    // 그 중 크기가 가장 큰 CoC를 구한다
    float CoC = c00;
    if (abs(c10) > abs(CoC))
    {
        CoC = c10;
    }
    if (abs(c01) > abs(CoC))
    {
        CoC = c01;
    }
    if (abs(c11) > abs(CoC))
    {
        CoC = c11;
    }
    
    // 네 픽셀의 색을 평균낸다
    float3 Color = (SceneColorTexture.Load(int3(Base + int2(0, 0), 0)).rgb +
                SceneColorTexture.Load(int3(Base + int2(1, 0), 0)).rgb +
                SceneColorTexture.Load(int3(Base + int2(0, 1), 0)).rgb +
                SceneColorTexture.Load(int3(Base + int2(1, 1), 0)).rgb) * 0.25f;
    
    return float4(Color, CoC);
}
