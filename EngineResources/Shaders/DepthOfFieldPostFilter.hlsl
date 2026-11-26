// Gather 결과(반해상도 Far/Near)에서 튀는 픽셀을 3x3 중앙값으로 걸러 낸다.
Texture2D FarTexture : register(t0);
Texture2D NearTexture : register(t1);

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

struct PSOutput
{
    float4 Far : SV_Target0;
    float4 Near : SV_Target1;
};

float4 Median3x3(Texture2D Source, int2 Pixel)
{
    uint W, H;
    Source.GetDimensions(W, H);
    int2 Size = int2(W, H);

    float4 Neighbors[9];
    for (int i = -1; i < 2; ++i)
    {
        for (int j = -1; j < 2; ++j)
        {
            // 화면 밖 Load는 0을 돌려줘 가장자리가 검게 끌려가므로 안쪽으로 묶는다
            int2 Coordinate = clamp(Pixel + int2(i, j), 0, Size - 1);
            Neighbors[(i + 1) * 3 + (j + 1)] = Source.Load(int3(Coordinate, 0));
        }
    }

    // 고정 순서 비교 19번으로 Neighbors[4]에 중앙값이 오게 한다 (분기 없음, float4라 채널별로 동시에)
#define S(a, b) { float4 t = min(Neighbors[a], Neighbors[b]); Neighbors[b] = max(Neighbors[a], Neighbors[b]); Neighbors[a] = t; }
    S(1, 2) S(4, 5) S(7, 8) S(0, 1) S(3, 4) S(6, 7) S(1, 2) S(4, 5) S(7, 8)
    S(0, 3) S(5, 8) S(4, 7) S(3, 6) S(1, 4) S(2, 5) S(4, 7) S(4, 2) S(6, 4) S(4, 2)
#undef S
    return Neighbors[4];
}

PSOutput mainPS(VSOutput Input)
{
    int2 Pixel = int2(Input.Position.xy);

    PSOutput Output;
    Output.Far = Median3x3(FarTexture, Pixel);
    Output.Near = Median3x3(NearTexture, Pixel);
    return Output;
}
