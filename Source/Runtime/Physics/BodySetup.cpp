#include "EnginePCH.h"
#include "BodySetup.h"
#include "PhysicsCore.h"
#include "PhysXConversions.h"
#include "PhysXIncludes.h"
#include "PhysXFilterShader.h"
#include "BodyInstance.h"
#include "PhysicalMaterial.h"
#include "Serialization/MathSerialization.h"

namespace
{
	void SerializeQuat(FStructuredArchive::FSlot Slot, FQuat& Q)
	{
		FStructuredArchive::FRecord R = Slot.EnterRecord();
		R << SA_VALUE("X", Q.X) << SA_VALUE("Y", Q.Y) << SA_VALUE("Z", Q.Z) << SA_VALUE("W", Q.W);
	}

	void SerializeShapeBase(FStructuredArchive::FRecord R, FKShapeElem& E)
	{
		R << SA_VALUE("Center", E.Center);
		SerializeQuat(R.EnterField("Rotation"), E.Rotation);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKSphereElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Radius", E.Radius);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKBoxElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Extent", E.Extent);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKSphylElem& E)
	{
		SerializeShapeBase(R, E);
		R << SA_VALUE("Radius", E.Radius) << SA_VALUE("Length", E.Length);
	}

	void SerializeElem(FStructuredArchive::FRecord R, FKConvexElem& E)
	{
		SerializeShapeBase(R, E);
		int32 Num = E.VertexData.Num();
		FStructuredArchive::FArray Verts = R.EnterArray("Vertices", Num);
		if (R.GetUnderlyingArchive().IsLoading())
		{
			E.VertexData.SetNum(Num);
		}

		for (int32 i = 0; i < Num; ++i)
		{
			Verts.EnterElement() << E.VertexData[i];
		}
	}

	template<typename ElemT>
	void SerializeElemArray(FStructuredArchive::FRecord Parent, const char* Name, TArray<ElemT>& Elems)
	{
		const bool bLoading = Parent.GetUnderlyingArchive().IsLoading();
		int32 Num = bLoading ? 0 : Elems.Num();
		FStructuredArchive::FArray Arr = Parent.EnterArray(Name, Num);
		if (bLoading)
		{
			Elems.SetNum(Num);
		}

		for (int32 i = 0; i < Num; ++i)
		{
			SerializeElem(Arr.EnterElement().EnterRecord(), Elems[i]);
		}
	}

	// 스케일은 형상 크기에 반영한다. 캡슐·구는 균일해야 하므로 가장 큰 축을 쓴다.
	float MaxAbsXY(const FVector& S) { return std::max(std::abs(S.X), std::abs(S.Y)); }
	float MaxAbs3(const FVector& S) { return std::max({ std::abs(S.X), std::abs(S.Y), std::abs(S.Z) }); }

	physx::PxTransform MakeLocalPose(const FVector& Center, const FQuat& Rotation, const FVector& Scale)
	{
		const FVector ScaledCenter(Center.X * Scale.X, Center.Y * Scale.Y, Center.Z * Scale.Z);
		return physx::PxTransform(K2PVector(ScaledCenter), K2PQuat(Rotation));
	}

	void AttachShape(physx::PxRigidActor& Actor, const physx::PxGeometry& Geometry, const physx::PxTransform& LocalPose, FBodyInstance* Owner)
	{
		physx::PxMaterial* Material = Owner->GetPhysicalMaterial()->GetPxMaterial();
		physx::PxShape* Shape = Material ? physx::PxRigidActorExt::createExclusiveShape(Actor, Geometry, *Material) : nullptr;
		if (!Shape)
		{
			KRS_LOG(Error, "[Physics] Failed to create shape.");
			return;
		}
		Shape->setLocalPose(LocalPose);

		// 시뮬레이션(밀고 밀림)과 쿼리(레이캐스트·스윕) 참여를 따로 켠다.
		const ECollisionEnabled Collision = Owner->CollisionEnabled;
		Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE,
			Collision == ECollisionEnabled::PhysicsOnly || Collision == ECollisionEnabled::QueryAndPhysics);
		Shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE,
			Collision == ECollisionEnabled::QueryOnly || Collision == ECollisionEnabled::QueryAndPhysics);
		// 밀고 밀리지 않으면서 겹침 이벤트를 원하면 트리거로 만들어 onTrigger로 들어옴/나감을 받는다.
		// eSIMULATION_SHAPE와 함께 켤 수 없으므로 위에서 시뮬레이션 플래그를 내린 뒤에 켠다.
		if (Owner->IsTriggerBody())
			Shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
		Shape->userData = Owner;

		const physx::PxFilterData FilterData = K2PFilterData(Owner->MakeMaskFilter());
		Shape->setSimulationFilterData(FilterData);   // 바디끼리 충돌 판정 (Filter Shader)
		Shape->setQueryFilterData(FilterData);        // 레이캐스트·스윕 판정 (A7-2)
	}
}

UBodySetup::~UBodySetup()
{
	// UObject는 PhysX SDK 정리(FPhysicsCore::Shutdown)보다 먼저 지워지므로 여기서 release해도 된다.
	ClearPhysicsMeshes();
}

void UBodySetup::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FStructuredArchive::FRecord Geom = Record.EnterRecord("AggGeom");

	SerializeElemArray(Geom, "Sphere", AggGeom.SphereElems);
	SerializeElemArray(Geom, "Box", AggGeom.BoxElems);
	SerializeElemArray(Geom, "Spyls", AggGeom.SphylElems);
	SerializeElemArray(Geom, "Convexes", AggGeom.ConvexElems);

	if (Record.GetUnderlyingArchive().IsLoading())
	{
		ClearPhysicsMeshes();
	}
}

bool UBodySetup::CreatePhysicsMeshes()
{
	if (CookedConvexMeshes.Num() == AggGeom.ConvexElems.Num())
		return true;   // 이미 만들었다

	ClearPhysicsMeshes();

	physx::PxCooking* Cooking = FPhysicsCore::Get().GetCooking();
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Cooking || !Physics)
		return false;

	bool bAllSucceeded = true;
	for (const FKConvexElem& Elem : AggGeom.ConvexElems)
	{
		physx::PxConvexMeshDesc Desc;
		Desc.points.count = static_cast<physx::PxU32>(Elem.VertexData.Num());
		Desc.points.stride = sizeof(FVector);
		Desc.points.data = Elem.VertexData.GetData();
		// 점 목록에서 볼록 껍질을 직접 계산한다. 결과 정점은 vertexLimit(기본 255)개 이하로 줄인다.
		Desc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX | physx::PxConvexFlag::eSHIFT_VERTICES;

		// 스트림으로 저장하지 않고 바로 PhysX 객체로 만든다
		physx::PxConvexMesh* Mesh = Cooking->createConvexMesh(Desc, Physics->getPhysicsInsertionCallback());
		if (!Mesh)
		{
			// 점이 4개 미만이거나 한 평면 위에 있으면 부피가 없어 실패한다.
			KRS_LOG(Warning, "[Physics] Convex cooking failed ({} points).", Elem.VertexData.Num());
			bAllSucceeded = false;
		}
		CookedConvexMeshes.Add(Mesh);   // 실패해도 nullptr로 자리를 채워 인덱스를 맞춘다
	}
	return bAllSucceeded;
}

void UBodySetup::ClearPhysicsMeshes()
{
	for (physx::PxConvexMesh* Mesh : CookedConvexMeshes)
	{
		if (Mesh)
			Mesh->release();   // 이 메시를 쓰는 shape가 남아 있으면 PhysX가 참조 수로 살려 둔다
	}
	CookedConvexMeshes.Empty();

	if (CookedTriMesh)
	{
		CookedTriMesh->release();
		CookedTriMesh = nullptr;
	}
	bTriMeshCookAttempted = false;
}

physx::PxTriangleMesh* UBodySetup::GetOrCreateTriMesh()
{
	if (bTriMeshCookAttempted)
		return CookedTriMesh;
	bTriMeshCookAttempted = true;

	physx::PxCooking* Cooking = FPhysicsCore::Get().GetCooking();
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	if (!Cooking || !Physics || TriMeshVertices.IsEmpty() || TriMeshIndices.Num() < 3)
		return nullptr;

	physx::PxTriangleMeshDesc Desc;
	Desc.points.count = static_cast<physx::PxU32>(TriMeshVertices.Num());
	Desc.points.stride = sizeof(FVector);
	Desc.points.data = TriMeshVertices.GetData();
	Desc.triangles.count = static_cast<physx::PxU32>(TriMeshIndices.Num() / 3);
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.triangles.data = TriMeshIndices.GetData();
	// 감김 순서가 반대면 면 법선이 안쪽을 향해, 위에서 떨어진 물체를 아래로 밀어낸다.
	if (bFlipTriMeshNormals)
		Desc.flags |= physx::PxMeshFlag::eFLIPNORMALS;

	CookedTriMesh = Cooking->createTriangleMesh(Desc, Physics->getPhysicsInsertionCallback());
	if (!CookedTriMesh)
		KRS_LOG(Warning, "[Physics] TriangleMesh cooking failed ({} triangles).", Desc.triangles.count);
	return CookedTriMesh;
}

void UBodySetup::AddShapesToRigidActor(FBodyInstance* OwningInstance, physx::PxRigidActor& Actor, const FVector& Scale3D)
{
	// 음수(뒤집기) 스케일은 PhysX가 유효하지 않은 메시 스케일로 거부하므로 크기만 쓴다.
	const physx::PxMeshScale MeshScale(K2PVector(Scale3D.GetAbs()));

	// Complex as Simple이면 삼각형 메시 하나만 붙인다.
	// 계단·지형처럼 오목한 배경을 정확히 막고, 평면 바닥도 두께 없이 그대로 쓸 수 있다.
	// PhysX는 시뮬레이트하는 바디와 트리거에 삼각형 메시를 허용하지 않으므로 Static 바디에만 쓰고,
	// 나머지는 삼각형이 없을 때처럼 단순 형상으로 만든다.
	const bool bUseComplexAsSimple = CollisionTraceFlag == ECollisionTraceFlag::UseComplexAsSimple
		&& OwningInstance->Mobility == EBodyMobility::Static && !OwningInstance->IsTriggerBody();
	if (bUseComplexAsSimple)
	{
		if (physx::PxTriangleMesh* TriMesh = GetOrCreateTriMesh())
		{
			AttachShape(Actor, physx::PxTriangleMeshGeometry(TriMesh, MeshScale), physx::PxTransform(physx::PxIdentity), OwningInstance);
			return;
		}
	}

	for (const FKSphereElem& Elem : AggGeom.SphereElems)
	{
		const physx::PxSphereGeometry Sphere(Elem.Radius * MaxAbs3(Scale3D));
		AttachShape(Actor, Sphere, MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), OwningInstance);
	}
	for (const FKBoxElem& Elem : AggGeom.BoxElems)
	{
		const physx::PxBoxGeometry Box(
			std::abs(Elem.Extent.X * Scale3D.X), std::abs(Elem.Extent.Y * Scale3D.Y), std::abs(Elem.Extent.Z * Scale3D.Z));
		AttachShape(Actor, Box, MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), OwningInstance);
	}
	for (const FKSphylElem& Elem : AggGeom.SphylElems)
	{
		const physx::PxCapsuleGeometry Capsule(Elem.Radius * MaxAbsXY(Scale3D), Elem.Length * 0.5f * std::abs(Scale3D.Z));
		physx::PxTransform LocalPose = MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D);
		LocalPose.q = LocalPose.q * CapsuleAxisFix();
		AttachShape(Actor, Capsule, LocalPose, OwningInstance);
	}

	if (!AggGeom.ConvexElems.IsEmpty())
	{
		CreatePhysicsMeshes();
		for (int32 Index = 0; Index < AggGeom.ConvexElems.Num(); ++Index)
		{
			physx::PxConvexMesh* Mesh = GetConvexMesh(Index);
			if (!Mesh)
				continue;   // Cooking 실패한 요소는 건너뛴다 (경고는 CreatePhysicsMeshes가 남겼다)

			// 구·캡슐과 달리 볼록 메시는 축마다 다른 스케일을 그대로 받는다.
			const FKConvexElem& Elem = AggGeom.ConvexElems[Index];
			AttachShape(Actor, physx::PxConvexMeshGeometry(Mesh, MeshScale), MakeLocalPose(Elem.Center, Elem.Rotation, Scale3D), OwningInstance);
		}
	}
}

physx::PxConvexMesh* UBodySetup::GetConvexMesh(int32 Index) const
{
	return (Index >= 0 && Index < CookedConvexMeshes.Num()) ? CookedConvexMeshes[Index] : nullptr;
}