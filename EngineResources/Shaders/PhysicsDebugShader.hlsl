// Physics Asset 에디터의 바디·조인트 표시. 반투명, 라이트를 받지 않는다.
// Mode 0: 일반 메시. Lit이면 노멀과 위쪽(+Z)의 내적으로 위는 밝고 아래는 어둡게 칠한다.
// Mode 1: Swing 원뿔. 매개변수 메시(UV.x = 둘레 비율, UV.y = 0 꼭짓점 / 1 테두리)를 X축 원뿔로 펼친다.
// Mode 2: Twist 부채꼴. YZ 평면에서 Y축을 가운데로 ±Angle1만큼 펼친다.
#pragma pack_matrix(row_major)

cbuffer PhysicsDebugConstants : register(b0)
{
	matrix World;
	matrix ViewProjection;
	float4 Color;
	float4 Params;   // x = Mode, y = Angle1(rad), z = Angle2(rad), w = Lit
};

struct VS_INPUT
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 tangent : TANGENT;
	float2 uv : TEXCOORD;
	float4 color : COLOR;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float3 worldNormal : NORMAL;
};

static const float PI = 3.14159265f;

// 두 반각(Y쪽 Angle1, Z쪽 Angle2)으로 정한 타원 원뿔 위의 방향. 원뿔 축은 +X
float3 ConeDirection(float Angle1, float Angle2, float Azimuth)
{
	const float A1 = clamp(Angle1, 0.001f, PI - 0.001f);
	const float A2 = clamp(Angle2, 0.001f, PI - 0.001f);

	const float SinX2 = sin(0.5f * A1);
	const float SinY2 = sin(0.5f * A2);
	const float SinSqX2 = SinX2 * SinX2;
	const float SinSqY2 = SinY2 * SinY2;

	const float Phi = atan2(sin(Azimuth) * SinY2, cos(Azimuth) * SinX2);
	const float SinPhi = sin(Phi);
	const float CosPhi = cos(Phi);

	const float RSq = SinSqX2 * SinSqY2 / (SinSqX2 * SinPhi * SinPhi + SinSqY2 * CosPhi * CosPhi);
	const float R = sqrt(RSq);
	const float S = sqrt(max(1.0f - RSq, 0.0f));

	return float3(1.0f - 2.0f * RSq, 2.0f * S * R * CosPhi, 2.0f * S * R * SinPhi);
}

PS_INPUT mainVS(VS_INPUT input)
{
	float3 LocalPosition = input.position;
	const int Mode = (int)(Params.x + 0.5f);

	if (Mode == 1)
	{
		LocalPosition = input.uv.y * ConeDirection(Params.y, Params.z, input.uv.x * 2.0f * PI);
	}
	else if (Mode == 2)
	{
		const float Theta = lerp(-Params.y, Params.y, input.uv.x);
		LocalPosition = input.uv.y * float3(0.0f, cos(Theta), sin(Theta));
	}

	PS_INPUT output;
	output.position = mul(mul(float4(LocalPosition, 1.0f), World), ViewProjection);
	output.worldNormal = mul(input.normal, (float3x3)World);
	return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
	float3 Rgb = Color.rgb;
	if (Params.w > 0.5f)
	{
		const float3 N = normalize(input.worldNormal);
		const float Shade = dot(N, float3(0.0f, 0.0f, 1.0f)) * 0.5f + 0.5f;
		Rgb *= lerp(0.45f, 1.15f, Shade);
	}
	return float4(saturate(Rgb), Color.a);
}
