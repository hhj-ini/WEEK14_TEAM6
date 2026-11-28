// FLandscapeVertex. 공유 격자 좌표(0~63)를 받고 높이·법선은 조각 하이트맵에서 읽는다.
// 로컬 공간: X·Y = 칸 번호, Z = 높이 0~1. World 행렬(스케일 CellSize, CellSize, HeightScale)이 cm로 늘린다.

#define LANDSCAPE_COMPONENT_QUADS 63.0f

// B8G8R8A8_UNORM, 64x64 + 밉 5장 (32, 16, 8, 4, 2). 텍셀 (i, j) = 정점 (i, j). 테두리 없음. UE와 같은 배치:
//   R = 높이 상위 8bit, G = 높이 하위 8bit
//   B = 월드 법선 X * 0.5 + 0.5, A = 월드 법선 Y * 0.5 + 0.5
// 5단계는 밉 0만 읽는다. 7단계에서 LOD k 정점이 밉 k를 읽고 k+1과 섞는다
Texture2D<float4> LandscapeHeightmap : register(t17);

struct FVertexFactoryInput
{
    float2 GridXY : POSITION;
};

struct FLandscapeSample
{
    float Height;        // 0~1
    float3 WorldNormal;  // 액터 공간 (회전 0이면 월드), 정규화됨
};

FLandscapeSample LandscapeLoad(int2 Texel, int Mip)
{
    const float4 T = LandscapeHeightmap.Load(int3(Texel, Mip));
    FLandscapeSample S;
    // UNORM 0~1을 다시 0~255 바이트로 돌려 16bit로 합친다 (UE UnpackHeight와 같다)
    S.Height = (round(T.r * 255.0f) * 256.0f + round(T.g * 255.0f)) / 65535.0f;
    const float2 N = T.ba * 2.0f - 1.0f;
    S.WorldNormal = float3(N, sqrt(saturate(1.0f - dot(N, N))));
    return S;
}

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 World, float4x4 NormalMatrix)
{
    const FLandscapeSample S = LandscapeLoad(int2(Input.GridXY), 0);

    // 법선은 이미 월드 비율로 구워져 있다. VF_LocalToWorld는 로컬 법선에 NormalMatrix(스케일의 역수)를 곱하므로,
    // 미리 스케일을 곱해 넘겨 서로 지워지게 한다. 남는 건 액터 회전뿐이다
    const float3 Scale = float3(length(World[0].xyz), length(World[1].xyz), length(World[2].xyz));
    const float3 LocalNormal = S.WorldNormal * Scale;

    // 접선 = X 방향으로 표면을 따라가는 벡터 (1, 0, dZ/dX). World가 스케일을 곱하므로 미리 나눠 둔다
    const float DzDx = -S.WorldNormal.x / max(S.WorldNormal.z, 1e-4f);
    const float4 LocalTangent = float4(float3(1.0f, 0.0f, DzDx) / Scale, 1.0f);

    const float2 UV = Input.GridXY / LANDSCAPE_COMPONENT_QUADS;

    return VF_LocalToWorld(float3(Input.GridXY, S.Height), LocalNormal, LocalTangent, UV,
        float4(1.0f, 1.0f, 1.0f, 1.0f), World, NormalMatrix);
}
