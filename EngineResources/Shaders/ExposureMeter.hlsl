// 노출계: 톤매핑 전 HDR 장면을 작은 타깃에 (가중 log2 휘도, 가중치)로 줄인다.
// 이후 mip을 끝까지 만들면 1x1 mip에 가중 평균이 남는다 (R/G 하면 중앙 중점 평균 log2 휘도)
Texture2D<float4> SceneHDR : register(t0);
SamplerState LinearClamp : register(s0);

struct FPSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

FPSInput mainVS(uint VertexID : SV_VertexID)
{
    FPSInput Output;
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    Output.UV = UV;
    return Output;
}

float Log2Luminance(float2 UV)
{
    float3 HDR = max(SceneHDR.SampleLevel(LinearClamp, UV, 0).rgb, 0.0f);
    float Luminance = dot(HDR, float3(0.2126f, 0.7152f, 0.0722f));
    // 완전히 검은 픽셀이 log2에서 -무한대가 되지 않게 바닥을 둔다
    return log2(max(Luminance, 1e-4f));
}

float2 mainPS(FPSInput Input) : SV_Target
{
    // 타깃 한 픽셀이 장면의 넓은 칸을 대표하므로, 한 점보다 네 점을 섞어 작은 하이라이트에 덜 흔들리게 한다
    float2 TexelSize;
    {
        uint W, H;
        SceneHDR.GetDimensions(W, H);
        TexelSize = 1.0f / float2(W, H);
    }
    float2 Offset = TexelSize * 2.0f;
    float LogL = 0.25f * (Log2Luminance(Input.UV + float2(-Offset.x, -Offset.y)) +
                          Log2Luminance(Input.UV + float2( Offset.x, -Offset.y)) +
                          Log2Luminance(Input.UV + float2(-Offset.x,  Offset.y)) +
                          Log2Luminance(Input.UV + float2( Offset.x,  Offset.y)));

    // 필름 카메라 노출계처럼 가운데를 더 보는 중앙 중점 측광
    float2 D = Input.UV - 0.5f;
    float Weight = 0.15f + exp(-dot(D, D) * 12.0f);
    return float2(LogL * Weight, Weight);
}
