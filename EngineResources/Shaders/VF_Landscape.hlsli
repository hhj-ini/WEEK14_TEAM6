// FLandscapeVertex. 공유 격자 좌표(0~64)를 받고 높이는 조각 하이트맵에서 읽는다.
// 로컬 공간: X·Y = 칸 번호, Z = 높이 0~1. World 행렬(스케일 CellSize, CellSize, HeightScale)이 cm로 늘린다.

#define LANDSCAPE_COMPONENT_QUADS 64.0f

// R16_UNORM, 67x67 (정점 65x65 + 테두리 1칸). Load 결과가 0~1 실수다
Texture2D<float> LandscapeHeightmap : register(t17);

struct FVertexFactoryInput
{
    float2 GridXY : POSITION;
};

float LandscapeHeight(int2 Texel)
{
    return LandscapeHeightmap.Load(int3(Texel, 0));
}

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    const int2 Texel = int2(Input.GridXY) + 1;   // 텍셀 (1,1)이 정점 (0,0)

    const float H  = LandscapeHeight(Texel);
    const float HL = LandscapeHeight(Texel + int2(-1, 0));
    const float HR = LandscapeHeight(Texel + int2( 1, 0));
    const float HD = LandscapeHeight(Texel + int2( 0,-1));
    const float HU = LandscapeHeight(Texel + int2( 0, 1));

    // 중앙 차분 기울기 (로컬 단위: 칸 1개당 높이 변화)
    const float DhDx = (HR - HL) * 0.5f;
    const float DhDy = (HU - HD) * 0.5f;

    // 로컬 법선·접선. NormalMatrix(World의 역전치)가 스케일 차이를 맞춰 월드 법선이 된다
    const float3 LocalNormal = float3(-DhDx, -DhDy, 1.0f);
    const float4 LocalTangent = float4(1.0f, 0.0f, DhDx, 1.0f);
    const float2 UV = Input.GridXY / LANDSCAPE_COMPONENT_QUADS;

    return VF_LocalToWorld(float3(Input.GridXY, H), LocalNormal, LocalTangent, UV,
        float4(1.0f, 1.0f, 1.0f, 1.0f), World, NormalMatrix);
}
