#include "EnginePCH.h"
#include "ClothingSimulationNv.h"
#include "ClothCore.h"
#include "NvClothIncludes.h"
#include <algorithm>
#include <cmath>

bool FClothingSimulationNv::CreateCloth(const FClothPhysicalMeshData& MeshData)
{
	Shutdown();

	CachedMeshData = MeshData;

	// Factory는 FClothCore에서 관리한다.
	nv::cloth::Factory* Factory = FClothCore::Get().GetCpuFactory();
	if (!Factory)
		return false;

	// MeshData 검증
	const uint32 NumVertices = static_cast<uint32>(MeshData.Vertices.Num());
	const uint32 NumIndices = static_cast<uint32>(MeshData.Indices.Num());
	if (NumVertices == 0 || NumIndices < 3 || (NumIndices % 3 != 0) || MeshData.InverseMasses.Num() != MeshData.Vertices.Num())
		return false;

	// NvCloth ClothMeshDesc 설정
	nv::cloth::ClothMeshDesc Desc;
	Desc.setToDefault();
	Desc.points.data = MeshData.Vertices.GetData();
	Desc.points.stride = sizeof(FVector);
	Desc.points.count = NumVertices;
	Desc.invMasses.data = MeshData.InverseMasses.GetData();
	Desc.invMasses.stride = sizeof(float);
	Desc.invMasses.count = NumVertices;
	Desc.triangles.data = MeshData.Indices.GetData();
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.triangles.count = NumIndices / 3;

	// Fabric 쿠킹 (Z-up)
	nv::cloth::Vector<int32_t>::Type PhaseTypes;
	Fabric = NvClothCookFabricFromMesh(Factory, Desc, physx::PxVec3(0.0f, 0.0f, -1.0f), &PhaseTypes, true);
	if (!Fabric)
		return false;

	TArray<physx::PxVec4> Particles;
	Particles.SetNum(NumVertices);
	for (uint32 i = 0; i < NumVertices; ++i)
	{
		const FVector& Position = MeshData.Vertices[i];
		Particles[i] = physx::PxVec4(Position.X, Position.Y, Position.Z, MeshData.InverseMasses[i]);
	}

	// Cloth 생성
	Cloth = Factory->createCloth(
		nv::cloth::Range<physx::PxVec4>(Particles.GetData(), Particles.GetData() + NumVertices),
		*Fabric);
	if (!Cloth)
	{
		Shutdown();
		return false;
	}

	// PhaseConfig 설정: 먼저 초기화한 뒤 Phase 종류별로 Stiffness를 나눈다.
	const uint32 NumPhases = Fabric->getNumPhases();
	TArray<nv::cloth::PhaseConfig> Phases;
	Phases.SetNum(NumPhases);
	for (uint32 i = 0; i < NumPhases; ++i)
	{
		Phases[i] = nv::cloth::PhaseConfig(static_cast<uint16_t>(i));
		const int32_t Type = (i < PhaseTypes.size()) ? PhaseTypes[i] : nv::cloth::ClothFabricPhaseType::eINVALID;
		if (Type == nv::cloth::ClothFabricPhaseType::eBENDING)
			Phases[i].mStiffness = CachedConfig.BendStiffness;
		else if (Type == nv::cloth::ClothFabricPhaseType::eSHEARING)
			Phases[i].mStiffness = CachedConfig.ShearStiffness;
		else
			Phases[i].mStiffness = CachedConfig.StretchStiffness;
		Phases[i].mStiffnessMultiplier = 1.0f;
		Phases[i].mCompressionLimit = 1.0f;
		Phases[i].mStretchLimit = 1.0f;
	}
	Cloth->setPhaseConfig(nv::cloth::Range<nv::cloth::PhaseConfig>(Phases.GetData(), Phases.GetData() + NumPhases));

	// 기본 물리 파라미터 설정
	Cloth->setGravity(physx::PxVec3(CachedConfig.Gravity.X, CachedConfig.Gravity.Y, CachedConfig.Gravity.Z));
	Cloth->setDamping(physx::PxVec3(CachedConfig.Damping));
	Cloth->setSolverFrequency(FMath::Clamp(CachedConfig.SolverFrequency, 30.0f, 240.0f));
	Cloth->setTetherConstraintScale(CachedConfig.TetherScale);
	Cloth->setTetherConstraintStiffness(CachedConfig.TetherStiffness);

	// 로컬 메시 크기를 1회만 계산해 cm 단위 여부를 캐싱 (매 프레임 AABB 순회 제거)
	bLocalMeshIsCm = false;
	if (NumVertices > 1)
	{
		FVector MinP = MeshData.Vertices[0];
		FVector MaxP = MeshData.Vertices[0];
		for (uint32 i = 1; i < NumVertices; ++i)
		{
			MinP.X = FMath::Min(MinP.X, MeshData.Vertices[i].X);
			MinP.Y = FMath::Min(MinP.Y, MeshData.Vertices[i].Y);
			MinP.Z = FMath::Min(MinP.Z, MeshData.Vertices[i].Z);
			MaxP.X = FMath::Max(MaxP.X, MeshData.Vertices[i].X);
			MaxP.Y = FMath::Max(MaxP.Y, MeshData.Vertices[i].Y);
			MaxP.Z = FMath::Max(MaxP.Z, MeshData.Vertices[i].Z);
		}
		bLocalMeshIsCm = ((MaxP - MinP).Size() > 15.0f);
	}

	// 솔버 생성 및 등록
	Solver = Factory->createSolver();
	if (!Solver)
	{
		Shutdown();
		return false;
	}
	Solver->addCloth(Cloth);
	return true;
}

bool FClothingSimulationNv::CreateClothFromAsset(const UClothingAssetCommon& Asset)
{
	if (!Asset.IsValid())
		return false;

	CachedConfig = Asset.GetConfig();
	if (!CreateCloth(Asset.GetPhysicalMeshData()))
		return false;

	SetGravity(CachedConfig.Gravity);
	SetWindVelocity(CachedConfig.WindVelocity, CachedConfig.DragCoefficient, CachedConfig.LiftCoefficient);
	Cloth->setDamping(physx::PxVec3(CachedConfig.Damping));
	Cloth->setSolverFrequency(FMath::Clamp(CachedConfig.SolverFrequency, 30.0f, 240.0f));
	Cloth->setTetherConstraintScale(CachedConfig.TetherScale);
	Cloth->setTetherConstraintStiffness(CachedConfig.TetherStiffness);
	Cloth->setLinearInertia(physx::PxVec3(CachedConfig.LinearInertiaScale));
	Cloth->setAngularInertia(physx::PxVec3(CachedConfig.AngularInertiaScale));
	Cloth->setMotionConstraintScaleBias(CachedConfig.MaxDistanceScale, 0.0f);
	Cloth->setMotionConstraintStiffness(1.0f);
	Cloth->setSelfCollisionDistance(FMath::Max(0.0f, CachedConfig.SelfCollisionDistance));
	Cloth->setSelfCollisionStiffness(FMath::Clamp(CachedConfig.SelfCollisionStiffness, 0.0f, 1.0f));

	SkinnedPositions.SetNum(CachedMeshData.Vertices.Num());
	return true;
}

void FClothingSimulationNv::Shutdown()
{
	if (Solver)
	{
		if (Cloth)
			Solver->removeCloth(Cloth);
		delete Solver;
		Solver = nullptr;
	}
	if (Cloth)
	{
		delete Cloth;
		Cloth = nullptr;
	}
	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}
}

void FClothingSimulationNv::UpdateSkeletalPose(
	const TArray<FMatrix>& RefToLocal,
	const FTransform& ComponentToWorld,
	bool bTeleport)
{
	if (!Cloth || CachedMeshData.SkinWeights.Num() != CachedMeshData.Vertices.Num())
		return;

	const uint32 NumVerts = static_cast<uint32>(CachedMeshData.Vertices.Num());
	const int32 NumBones = RefToLocal.Num();
	if (NumVerts == 0 || NumBones == 0)
		return;

	// 1. 시뮬 메시를 현재 애니메이션 본 행렬로 스키닝
	SkinnedPositions.SetNum(NumVerts);
	for (uint32 i = 0; i < NumVerts; ++i)
	{
		const FSkinWeight& W = CachedMeshData.SkinWeights[i];
		FMatrix M = RefToLocal[W.BoneIndex[0]] * W.Weight[0];
		for (int32 k = 1; k < MaxBoneInfluences; ++k)
		{
			if (W.Weight[k] > 0.0f && W.BoneIndex[k] < NumBones)
				M += RefToLocal[W.BoneIndex[k]] * W.Weight[k];
		}
		SkinnedPositions[i] = M.TransformPosition(CachedMeshData.Vertices[i]);
	}

	// 2. 고정점 위치 갱신 + 동적점 MotionConstraints 전달
	nv::cloth::MappedRange<physx::PxVec4> Particles = Cloth->getCurrentParticles();
	nv::cloth::MappedRange<physx::PxVec4> PrevParticles = Cloth->getPreviousParticles();
	nv::cloth::Range<physx::PxVec4> Constraints = Cloth->getMotionConstraints();

	const bool bHasMaxDist = (CachedMeshData.MaxDistances.Num() == CachedMeshData.Vertices.Num());
	for (uint32 i = 0; i < NumVerts; ++i)
	{
		const FVector& P = SkinnedPositions[i];
		const float InvMass = CachedMeshData.InverseMasses[i];
		if (InvMass == 0.0f || bTeleport)
		{
			const physx::PxVec4 V(P.X, P.Y, P.Z, InvMass);
			Particles[i] = V;
			if (bTeleport)
			{
				PrevParticles[i] = V;
			}
		}
		const float MaxDist = bHasMaxDist ? CachedMeshData.MaxDistances[i] : 0.0f;
		Constraints[i] = physx::PxVec4(P.X, P.Y, P.Z, MaxDist);
	}

	// 3. 엔진 월드(m) -> 컴포넌트 로컬 공간 스케일 자동 보정
	//    - 레벨에서 Scale=0.01 배치된 cm 메시: InvScale = 100 -> -980 cm/s^2 (1G)
	//    - 프리뷰 에디터에서 Scale=1.0으로 띄운 cm 메시: CreateCloth에서 캐싱한 bLocalMeshIsCm으로 100x 보정
	const float UniformScale = FMath::Max(std::fabs(ComponentToWorld.Scale.Z), 0.001f);
	const float InvScale = 1.0f / UniformScale;
	const float PhysicsUnitScale = (UniformScale > 0.5f && bLocalMeshIsCm) ? 100.0f : InvScale;

	const FVector LocalGravity = CachedConfig.Gravity * PhysicsUnitScale;
	const FVector LocalWind = CachedConfig.WindVelocity * PhysicsUnitScale;
	Cloth->setGravity(physx::PxVec3(LocalGravity.X, LocalGravity.Y, LocalGravity.Z));
	Cloth->setWindVelocity(physx::PxVec3(LocalWind.X, LocalWind.Y, LocalWind.Z));

	// NvCloth applyWind의 impulse = (drag + lift) * fluidDensity * doubleArea는 L^4에 비례하므로
	// cm 단위(100x)에서 fluidDensity=1.0을 그대로 두면 항력이 1,000,000배 폭주해 NaN이 발생한다.
	// 길이 단위의 세제곱 역수(1 / L^3)로 유체 밀도를 스케일링해 m/cm 모든 메시에서 물리적으로 일관되게 유지한다.
	const float UnitScale3 = PhysicsUnitScale * PhysicsUnitScale * PhysicsUnitScale;
	const float FluidDensity = (PhysicsUnitScale > 1.0f) ? (5.0f / UnitScale3) : 1.0f;
	Cloth->setFluidDensity(FluidDensity);

	// 로컬 공간 이동/회전 전달
	const FVector ScaledLoc = ComponentToWorld.Location * InvScale;
	const FQuat Rot = ComponentToWorld.Rotation.Normalized();
	Cloth->setTranslation(physx::PxVec3(ScaledLoc.X, ScaledLoc.Y, ScaledLoc.Z));
	Cloth->setRotation(physx::PxQuat(Rot.X, Rot.Y, Rot.Z, Rot.W));
	if (bTeleport)
	{
		Cloth->clearInertia();
		Cloth->clearInterpolation();
	}
}

void FClothingSimulationNv::UpdateCollisions(const TArray<FVector4>& Spheres, const TArray<uint32>& CapsuleIndices)
{
	if (!Cloth)
		return;

	const uint32 NumSpheres = static_cast<uint32>(std::min<int32>(Spheres.Num(), 32));
	TArray<physx::PxVec4> PxSpheres;
	PxSpheres.SetNum(NumSpheres);
	for (uint32 i = 0; i < NumSpheres; ++i)
	{
		PxSpheres[i] = physx::PxVec4(Spheres[i].X, Spheres[i].Y, Spheres[i].Z, Spheres[i].W);
	}

	Cloth->setSpheres(
		nv::cloth::Range<const physx::PxVec4>(PxSpheres.GetData(), PxSpheres.GetData() + NumSpheres),
		0, Cloth->getNumSpheres());

	const uint32 NumCapIdx = static_cast<uint32>(std::min<int32>(CapsuleIndices.Num(), 64));
	Cloth->setCapsules(
		nv::cloth::Range<const uint32_t>(CapsuleIndices.GetData(), CapsuleIndices.GetData() + NumCapIdx),
		0, Cloth->getNumCapsules());
}

void FClothingSimulationNv::Simulate(float DeltaTime)
{
	if (!Solver || !Cloth || DeltaTime <= 0.0f)
		return;

	const float ClampedDt = std::min(DeltaTime, 1.0f / 30.0f); // 최대 30fps로 제한
	if (Solver->beginSimulation(ClampedDt))
	{
		const int ChunkCount = Solver->getSimulationChunkCount();
		for (int i = 0; i < ChunkCount; ++i)
		{
			Solver->simulateChunk(i);
		}
		Solver->endSimulation();
	}
}

void FClothingSimulationNv::GetSimulationData(FClothingSimulationData& OutData) const
{
	if (!Cloth)
		return;

	const nv::cloth::Cloth* ConstCloth = Cloth;
	nv::cloth::MappedRange<const physx::PxVec4> Particles = ConstCloth->getCurrentParticles();
	const uint32 Count = Particles.size();

	OutData.Positions.SetNum(Count);
	OutData.Normals.SetNum(Count);
	for (uint32 i = 0; i < Count; ++i)
	{
		const physx::PxVec4& P = Particles[i];
		if (std::isfinite(P.x) && std::isfinite(P.y) && std::isfinite(P.z))
		{
			OutData.Positions[i] = FVector(P.x, P.y, P.z);
		}
		else if (static_cast<int32>(i) < SkinnedPositions.Num())
		{
			OutData.Positions[i] = SkinnedPositions[i];
		}
		else if (static_cast<int32>(i) < CachedMeshData.Vertices.Num())
		{
			OutData.Positions[i] = CachedMeshData.Vertices[i];
		}
		else
		{
			OutData.Positions[i] = FVector::ZeroVector;
		}
		OutData.Normals[i] = FVector::ZeroVector;
	}

	// 시뮬 메시 삼각형 면 법선을 정점에 누적해 부드러운 노멀 재계산
	const TArray<uint32>& Idx = CachedMeshData.Indices;
	for (int32 i = 0; i + 2 < Idx.Num(); i += 3)
	{
		const uint32 I0 = Idx[i];
		const uint32 I1 = Idx[i + 1];
		const uint32 I2 = Idx[i + 2];
		if (I0 >= Count || I1 >= Count || I2 >= Count)
			continue;

		const FVector FaceN = FVector::Cross(
			OutData.Positions[I1] - OutData.Positions[I0],
			OutData.Positions[I2] - OutData.Positions[I0]);
		OutData.Normals[I0] += FaceN;
		OutData.Normals[I1] += FaceN;
		OutData.Normals[I2] += FaceN;
	}

	for (uint32 i = 0; i < Count; ++i)
	{
		OutData.Normals[i] = OutData.Normals[i].GetSafeNormal(SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
	}
}

void FClothingSimulationNv::SetGravity(const FVector& InGravity)
{
	CachedConfig.Gravity = InGravity;
	if (Cloth)
	{
		Cloth->setGravity(physx::PxVec3(InGravity.X, InGravity.Y, InGravity.Z));
	}
}

void FClothingSimulationNv::SetWindVelocity(const FVector& InWind, float InDrag, float InLift)
{
	CachedConfig.WindVelocity = InWind;
	CachedConfig.DragCoefficient = InDrag;
	CachedConfig.LiftCoefficient = InLift;
	if (Cloth)
	{
		Cloth->setWindVelocity(physx::PxVec3(InWind.X, InWind.Y, InWind.Z));
		Cloth->setDragCoefficient(InDrag);
		Cloth->setLiftCoefficient(InLift);
	}
}

void FClothingSimulationNv::ApplyConfig(const FClothConfigNv& InConfig)
{
	CachedConfig = InConfig;
	if (!Cloth)
		return;

	Cloth->setDragCoefficient(CachedConfig.DragCoefficient);
	Cloth->setLiftCoefficient(CachedConfig.LiftCoefficient);
	Cloth->setDamping(physx::PxVec3(CachedConfig.Damping));
	Cloth->setSolverFrequency(FMath::Clamp(CachedConfig.SolverFrequency, 30.0f, 240.0f));
	Cloth->setTetherConstraintScale(CachedConfig.TetherScale);
	Cloth->setTetherConstraintStiffness(CachedConfig.TetherStiffness);
	Cloth->setLinearInertia(physx::PxVec3(CachedConfig.LinearInertiaScale));
	Cloth->setAngularInertia(physx::PxVec3(CachedConfig.AngularInertiaScale));
	Cloth->setMotionConstraintScaleBias(CachedConfig.MaxDistanceScale, 0.0f);
	Cloth->setSelfCollisionDistance(FMath::Max(0.0f, CachedConfig.SelfCollisionDistance));
	Cloth->setSelfCollisionStiffness(FMath::Clamp(CachedConfig.SelfCollisionStiffness, 0.0f, 1.0f));
}
