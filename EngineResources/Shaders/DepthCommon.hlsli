#ifndef DEPTH_COMMON_HLSLI
#define DEPTH_COMMON_HLSLI

// 깊이 버퍼 값(0~1)을 뷰 공간 앞쪽 거리로 되돌린다.
// ZScale = ProjectionMatrix.M[0][2], ZOffset = ProjectionMatrix.M[3][2]
float ConvertFromDeviceZ(float DeviceZ, float ZScale, float ZOffset, bool bPerspective)
{
    float LinearDepth;
    if (bPerspective)
    {
        LinearDepth = ZOffset / (DeviceZ - ZScale);
    }
    else
    {
        LinearDepth = (DeviceZ - ZOffset) / ZScale;
    }
    return LinearDepth;
}

#endif
