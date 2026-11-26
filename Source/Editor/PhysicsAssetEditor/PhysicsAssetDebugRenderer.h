#pragma once

#include "Render/PipelineState.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

class UStaticMesh;
class FConstantBuffer;

// PhysicsDebugShader.hlsl의 상수 버퍼. 행렬 2개(128B) + float4 2개(32B) = 160B, 16바이트 경계에 맞는다.
struct FPhysicsDebugConstants
{
	FMatrix World;
	FMatrix ViewProjection;
	FVector4 Color;
	FVector4 Params;   // x = Mode(0 메시, 1 원뿔, 2 부채꼴), y = Angle1(rad), z = Angle2(rad), w = Lit
};
static_assert(sizeof(FPhysicsDebugConstants) == 160, "PhysicsDebugConstants layout must match the shader");

// 바디 색. 에디터 뷰포트와 콘텐츠 브라우저 썸네일이 함께 쓴다. 셰이더가 위는 밝게 아래는 어둡게 칠한다
namespace PhysicsDebugColors
{
	inline const FVector4 Body(0.7f, 0.71f, 0.72f, 0.55f);
	inline const FVector4 SelectedBody(0.2f, 0.55f, 1.0f, 0.65f);
}

// Physics Asset 에디터 뷰포트에 바디(반투명 회색 캡슐 등)와 조인트(Swing 원뿔, Twist 부채꼴, 화살표, 축)를 그린다.
// 오버레이 패스 안에서 Begin 뒤에 Draw를 부른다. 행렬은 row-vector(로컬 × 월드) 순서다.
class FPhysicsAssetDebugRenderer
{
public:
	FPhysicsAssetDebugRenderer();
	~FPhysicsAssetDebugRenderer();

	// bSolidShapes: 앞면만 그린다 (바디). false면 양면 (원뿔·부채꼴처럼 얇은 면)
	void Begin(const FMatrix& ViewProjection, bool bSolidShapes);

	// ElemToWorld: 형상 로컬(중심·회전) × 본 월드. 형상 로컬 Z가 캡슐의 긴 축이다
	void DrawCapsule(const FMatrix& ElemToWorld, float Radius, float Length, const FVector4& Color);
	void DrawSphere(const FMatrix& ElemToWorld, float Radius, const FVector4& Color);
	void DrawBox(const FMatrix& ElemToWorld, const FVector& Extent, const FVector4& Color);

	// FrameToWorld: 조인트 프레임(X = Twist 축) × 크기. 원뿔 반각은 Y쪽 Angle1, Z쪽 Angle2
	void DrawCone(const FMatrix& FrameToWorld, float Angle1, float Angle2, const FVector4& Color);
	// YZ 평면에서 Y축을 가운데로 ±Angle
	void DrawFan(const FMatrix& FrameToWorld, float Angle, const FVector4& Color);

	void DrawArrow(const FVector& Origin, const FVector& Direction, float Length, const FVector4& Color);
	void DrawStick(const FVector& Start, const FVector& End, float Radius, const FVector4& Color);

	// 세 축과 원점으로 행렬을 만든다 (각 축 길이가 그 방향 스케일)
	static FMatrix MakeBasis(const FVector& X, const FVector& Y, const FVector& Z, const FVector& Origin);

private:
	bool EnsureResources();
	void DrawMesh(UStaticMesh* Mesh, const FMatrix& World, const FVector4& Color, const FVector4& Params);

	FShaderProgram* Shader = nullptr;
	FPipelineState SolidState;
	FPipelineState TwoSidedState;
	TUniquePtr<FConstantBuffer> ConstantBuffer;

	UStaticMesh* HemisphereMesh = nullptr;
	UStaticMesh* CylinderMesh = nullptr;
	UStaticMesh* SphereMesh = nullptr;
	UStaticMesh* CubeMesh = nullptr;
	UStaticMesh* ArrowMesh = nullptr;
	UStaticMesh* FanMesh = nullptr;

	FMatrix ViewProjection = FMatrix::Identity;
	bool bResourcesReady = false;
};
