#pragma once

#include "Core/Types.h"
#include "Math/Transform.h"

class FLineBatcher;
struct FKSphylElem;
struct FKAggregateGeom;

// 본(또는 바디) 로컬 기준 형상을 BoneToWorld로 옮겨 와이어로 그린다. 행렬에 스케일이 있으면 반영된다.
void DrawSphylElemWire(FLineBatcher& LineBatcher, const FMatrix& BoneToWorld, const FKSphylElem& Elem, const FVector4& Color);
void DrawAggGeomWire(FLineBatcher& LineBatcher, const FMatrix& BoneToWorld, const FKAggregateGeom& Geom, const FVector4& Color);