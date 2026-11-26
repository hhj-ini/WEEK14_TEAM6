#include "EnginePCH.h"
#include "PhysicsDebugDraw.h"
#include "BodySetup.h"
#include "Render/LineBatcher.h"

namespace
{
	// 중심, 원이 놓일 평면의 두 축, 반지름, 시작~끝 각도(라디안)
	void AddArc(FLineBatcher& LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments = 16)
	{
		auto PointAt = [&](float T) { return Center + AxisA * (std::cos(T) * Radius) + AxisB * (std::sin(T) * Radius); };
		FVector Prev = PointAt(StartRad);
		for (int32 i = 1; i <= Segments; ++i)
		{
			const FVector P = PointAt(StartRad + (EndRad - StartRad) * i / Segments);
			LineBatcher.AddLine(Prev, P, Color);
			Prev = P;
		}
	}
}


void DrawSphylElemWire(FLineBatcher& LineBatcher, const FMatrix& BoneToWorld, const FKSphylElem& Elem, const FVector4& Color)
{
	const FVector LocalUp = Elem.Rotation.RotateVector(FVector(0, 0, 1));
	const FVector LocalX = Elem.Rotation.RotateVector(FVector(1, 0, 0));

	const FVector Center = BoneToWorld.TransformPosition(Elem.Center);
	const FVector HalfAxis = BoneToWorld.TransformVector(LocalUp * (Elem.Length * 0.5f));

	const float Radius = BoneToWorld.TransformVector(LocalX * Elem.Radius).Size();

	const FVector Up = HalfAxis.GetSafeNormal();
	const FVector X = BoneToWorld.TransformVector(LocalX).GetSafeNormal();
	const FVector Y = Up.Cross(X).GetSafeNormal();
	const FVector Top = Center + HalfAxis;      // 위쪽 반구 중심
	const FVector Bottom = Center - HalfAxis;   // 아래쪽 반구 중심
	constexpr float Pi = PI;

	// 위, 아래 고리
	AddArc(LineBatcher, Top, X, Y, Radius, 0, 2 * Pi, Color);
	AddArc(LineBatcher, Bottom, X, Y, Radius, 0, 2 * Pi, Color);

	// 위 반구, 아래반구
	AddArc(LineBatcher, Top, X, Up, Radius, 0, Pi, Color);
	AddArc(LineBatcher, Top, Y, Up, Radius, 0, Pi, Color);
	AddArc(LineBatcher, Bottom, X, Up, Radius, 0, -Pi, Color);
	AddArc(LineBatcher, Bottom, Y, Up, Radius, 0, -Pi, Color);
	
	// 옆선 4개
	for (const FVector& Side : { X, X * -1.0f, Y, Y * -1.0f })
		LineBatcher.AddLine(Top + Side * Radius, Bottom + Side * Radius, Color);
}

void DrawAggGeomWire(FLineBatcher& LineBatcher, const FMatrix& BoneToWorld, const FKAggregateGeom& Geom, const FVector4& Color)
{
	for (const FKSphylElem& Sphyl : Geom.SphylElems)
		DrawSphylElemWire(LineBatcher, BoneToWorld, Sphyl, Color);

	for (const FKSphereElem& Sphere : Geom.SphereElems)
	{
		const float Radius = BoneToWorld.TransformVector(FVector(Sphere.Radius, 0, 0)).Size();
		LineBatcher.AddSphere(BoneToWorld.TransformPosition(Sphere.Center), Radius, Color, 12);
	}
}
