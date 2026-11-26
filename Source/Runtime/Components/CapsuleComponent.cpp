#include "EnginePCH.h"
#include "CapsuleComponent.h"
#include "Render/LineBatcher.h"
#include "Physics/BodySetup.h"

float UCapsuleComponent::GetScaledCapsuleHalfHeight() const
{
	const float Half = CapsuleHalfHeight * std::abs(GetWorldScale3D().Z); 
	return std::max(Half, GetScaledCapsuleRadius()); // 반높이가 반지름보다 작으면 구가 된다
}

float UCapsuleComponent::GetScaledCapsuleRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs(); 
	return CapsuleRadius * std::max(S.X, S.Y); // 반지름은 축에 수직인 X·Y 중 큰 쪽
}

FBox UCapsuleComponent::CalcLocalBounds() const
{
	const FVector E(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	return FBox{ E * -1.0f, E };
}

void UCapsuleComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const FMatrix M = GetWorldMatrix();
	const FVector Center(M.M[3][0], M.M[3][1], M.M[3][2]);
	const FVector Up = FVector(M.M[2][0], M.M[2][1], M.M[2][2]).GetSafeNormal();
	const FVector X = FVector(M.M[0][0], M.M[0][1], M.M[0][2]).GetSafeNormal();
	const FVector Y = Up.Cross(X).GetSafeNormal();
	const float R = GetScaledCapsuleRadius();
	// 반구 중심까지의 거리. 반높이는 반지름 이상으로 보정되므로 음수가 되지 않는다 (0이면 구).
	const float Segment = GetScaledCapsuleHalfHeight() - R;
	const FVector Top = Center + Up * Segment;      // 위쪽 반구 중심
	const FVector Bottom = Center - Up * Segment;   // 아래쪽 반구 중심
	constexpr float Pi = PI;

	// 위·아래 고리
	AddWireArc(LineBatcher, Top, X, Y, R, 0, 2 * Pi, Color);
	AddWireArc(LineBatcher, Bottom, X, Y, R, 0, 2 * Pi, Color);
	// 위 반구 (0 → π: X → Up → -X), 아래 반구 (0 → -π: X → -Up → -X)
	AddWireArc(LineBatcher, Top, X, Up, R, 0, Pi, Color);
	AddWireArc(LineBatcher, Top, Y, Up, R, 0, Pi, Color);
	AddWireArc(LineBatcher, Bottom, X, Up, R, 0, -Pi, Color);
	AddWireArc(LineBatcher, Bottom, Y, Up, R, 0, -Pi, Color);
	// 옆선 4개
	for (const FVector& Side : { X, X * -1.0f, Y, Y * -1.0f })
		LineBatcher->AddLine(Top + Side * R, Bottom + Side * R, Color);
}

void UCapsuleComponent::UpdateBodySetup()
{
	FKAggregateGeom& Geom = ShapeBodySetup->AggGeom;
	Geom = FKAggregateGeom();
	FKSphylElem& Sphyl = Geom.SphylElems.Emplace_GetRef();
	Sphyl.Radius = CapsuleRadius;
	// HalfHeight는 반구 끝까지, SphylElem의 Length는 두 반구 중심 사이 원통 길이다.
	Sphyl.Length = 2.0f * std::max(CapsuleHalfHeight - CapsuleRadius, 0.0f);
}
