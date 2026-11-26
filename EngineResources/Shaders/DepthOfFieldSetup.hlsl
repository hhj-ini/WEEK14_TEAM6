#include "DepthCommon.hlsli"

cbuffer DepthOfFieldConstants : register(b0)
{
    float ZScale;
    float ZOffset;
    float FocalDistance;
    float FocusRange;

    // 렌즈 값은 모두 월드 단위
    float FocalLength;
    float Fstop;
    float SensorWidth;
    float ViewWidthPx;
};

Texture2D<float> SceneDepthTexture : register(t0);

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

// 부호 약속: 음수 = near(초점 앞), 양수 = far(초점 뒤). CoC를 읽는 모든 패스가 이 약속을 따른다.
float ComputeSignedCoC(float LinearDepth, float InFocalDistance, float InFocusRange)
{
    InFocusRange = max(InFocusRange, 0.01f);
    return clamp((LinearDepth - InFocalDistance) / InFocusRange, -1.f, 1.f);
}

// 얇은 렌즈 CoC를 픽셀 반경으로 돌려준다. 부호 약속은 ComputeSignedCoC와 같다.
float ComputeThinLensCoC(float LinearDepth)
{
    // 조리개 지름 → 센서 위 CoC → 픽셀 반경
    float Aperture = FocalLength / Fstop;
    float S = max(FocalDistance, FocalLength + 0.01f);
    float CoC = Aperture * FocalLength * (LinearDepth - S) / (LinearDepth * (S - FocalLength));
    float CoCPixel = CoC / SensorWidth * ViewWidthPx * 0.5f;
    return CoCPixel;
}

// R16_Float CoC 텍스처에 부호 있는 CoC 하나만 쓴다. 색으로 바꾸는 건 Visualize 패스의 일이다.
float mainPS(VSOutput Input) : SV_Target
{
    int2 PixelPosition = int2(Input.Position.xy);
    float DeviceZ = SceneDepthTexture.Load(int3(PixelPosition, 0));

    // DOF 패스는 원근 뷰에서만 등록하므로 항상 원근 변환을 쓴다
    float LinearDepth = ConvertFromDeviceZ(DeviceZ, ZScale, ZOffset, true);
    return ComputeThinLensCoC(LinearDepth);
}
