#include "EnginePCH.h"
#include "Landscape/LandscapeCollision.h"

#include "Terrain/TerrainData.h"
#include "Components/PrimitiveComponent.h"

#include "Physics/BodyInstance.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysScene.h"
#include "Physics/PhysicsCore.h"
#include "Physics/PhysicsLock.h"
#include "Physics/PhysXConversions.h"
#include "Physics/PhysXFilterShader.h"
#include "Physics/PhysXIncludes.h"

bool FLandscapeCollision::Create(const FTerrainData& Data, FBodyInstance& Body, UPrimitiveComponent* Owner, FPhysScene* Scene)
{
	if (Data.SizeX < 2 || Data.SizeY < 2)
	{
		KRS_LOG(Warning, "[Terrain] HeightField skipped: size must be at least 2x2 ({}x{}).", Data.SizeX, Data.SizeY);
		return false;
	}
	if (Data.Heights.Num() != Data.SizeX * Data.SizeY)
	{
		KRS_LOG(Warning, "[Terrain] HeightField skipped: Heights count {} != SizeX * SizeY ({}).", Data.Heights.Num(), Data.SizeX * Data.SizeY);
		return false;
	}
	if (Data.CellSize <= 0.0f || Data.HeightScale <= 0.0f)
	{
		KRS_LOG(Warning, "[Terrain] HeightField skipped: CellSize ({}) and HeightScale ({}) must be > 0.", Data.CellSize, Data.HeightScale);
		return false;
	}
	if (!Scene || !Scene->IsValid() || !FPhysicsCore::Get().IsInitialized() || !FPhysicsCore::Get().GetCooking())
	{
		KRS_LOG(Warning, "[Terrain] HeightField skipped: physics scene or cooking is not ready.");
		return false;
	}

	Destroy(Body);	// 시작시 Body, HeightField 초기화

	TArray<physx::PxHeightFieldSample> Samples;		// Height Field Cooking에 사용할 배열 생성
	physx::PxPhysics* Physics = FPhysicsCore::Get().GetPhysics();
	Samples.SetNum(Data.SizeX * Data.SizeY);
	for (int i = 0;i < Data.SizeY;i++)
	{
		for (int j = 0;j < Data.SizeX;j++)
		{
			physx::PxHeightFieldSample HFSample;
			HFSample.height = static_cast<physx::PxI16>(int32(Data.Heights[Data.SizeX * i + j]) - 32768);	// uint16으로 Height 저장되어있으므로 int16으로 변환
			HFSample.materialIndex0 = 0;
			HFSample.materialIndex1 = 0;
			HFSample.setTessFlag();
			Samples[Data.SizeX * i + j] = HFSample;
		}
	}

	physx::PxHeightFieldDesc HFDesc;
	HFDesc.format = physx::PxHeightFieldFormat::eS16_TM;	// signed16 -> Triangle Materials로 저장했다는 표시, sample 각 바이트를 어떻게 읽을 것인지를 결정
	HFDesc.nbRows = Data.SizeY;
	HFDesc.nbColumns = Data.SizeX;
	HFDesc.samples.data = Samples.GetData();
	HFDesc.samples.stride = sizeof(physx::PxHeightFieldSample);

	HeightField = FPhysicsCore::Get().GetCooking()->createHeightField(HFDesc, Physics->getPhysicsInsertionCallback());

	if(!HeightField)
	{
		KRS_LOG(Warning, "[Terrain] HeightField cooking failed.");
		return false;
	}

	physx::PxHeightFieldGeometry HFGeometry;
	HFGeometry.heightField = HeightField;
	HFGeometry.heightScale = Data.HeightScale / 65535;	// HeightScale이 Height == 65535 일때를 기준으로 하므로 나눠줌
	HFGeometry.rowScale = Data.CellSize;
	HFGeometry.columnScale = Data.CellSize;

	if (!HFGeometry.isValid())
	{
		KRS_LOG(Warning, "[Terrain] HeightFieldGeometry create failed.");
		Destroy(Body);
		return false;
	}

	FTransform Pose(FQuat(0.5f, 0.5f, 0.5f, 0.5f),  Data.Origin + FVector(0, 0, 32768 * HFGeometry.heightScale)); // HeightField는 Y-Up으로 생성, Z-Up 변환을 위해 (1,1,1) 축 기준 120도로 회전 적용
	physx::PxRigidStatic* HFRigidStatic = Physics->createRigidStatic(K2PTransform(Pose));

	if (HFRigidStatic==nullptr)
	{
		KRS_LOG(Warning, "[Terrain] HeightFieldRigidActor create failed.");
		Destroy(Body);
		return false;
	}

	if (!Owner)
	{
		KRS_LOG(Warning, "[Terrain] HeightField owner component is null.");
		HFRigidStatic->release();	
		Destroy(Body);
		return false;
	}

	physx::PxMaterial* Material = Owner->BodyInstance.GetPhysicalMaterial()->GetPxMaterial();	// Primitive의 BodyInstance에서 PxMaterial 가져옴
	if (!Material)
	{
		KRS_LOG(Warning, "[Terrain] HeightField PxMaterial is null.");
		HFRigidStatic->release();
		Destroy(Body);
		return false;
	}

	physx::PxShape* HFShape = physx::PxRigidActorExt::createExclusiveShape(*HFRigidStatic, HFGeometry, *Material);	// 지금까지 생성한 RigidActor, Geometry 바탕으로 Shape 생성 + Attach
	if (!HFShape)
	{
		KRS_LOG(Warning, "[Terrain] HeightField shape create failed.");
		HFRigidStatic->release();	
		Destroy(Body);
		return false;
	}

	FMaskFilter HFFilter = Body.MakeMaskFilter();	// BodyInstance에 세팅되어 있는 충돌 필터 세팅 가지고 와서 BitMask 형태로 만들어줌
	physx::PxFilterData HFFilterData = K2PFilterData(HFFilter);	// 해당 BitMask 세팅을 PxFilterData 형태로 전환
	HFShape->setSimulationFilterData(HFFilterData);		// 충돌시에 사용할 FilterData 세팅
	HFShape->setQueryFilterData(HFFilterData);			// 탐색시에 사용할 FilterData 세팅

	ECollisionEnabled Collision = Body.CollisionEnabled;
	HFShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, Collision == ECollisionEnabled::PhysicsOnly || Collision == ECollisionEnabled::QueryAndPhysics); 
	HFShape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, Collision == ECollisionEnabled::QueryOnly || Collision == ECollisionEnabled::QueryAndPhysics);
	// CollisionEnable 설정에 따라 Shape의 Physics,Query 포함 여부 결정

	Body.Actor = HFRigidStatic;
	Body.Scene = Scene;
	Body.OwnerComponent = Owner;
	Body.Scale3D = FVector(1, 1, 1);	// 지형은 컴포넌트 스케일 적용 X
	HFRigidStatic->userData = &Body;		// Actor -> Body와의 연결
	
	{
		SCOPED_SCENE_WRITE_LOCK(Scene->GetPxScene());
		Scene->GetPxScene()->addActor(*HFRigidStatic);		// PxScene에 HeightFieldActor 추가, Lock을 걸고 해야함
	}

	return true;
}

void FLandscapeCollision::Destroy(FBodyInstance& Body)
{
	// 액터를 씬에서 빼고 해제한다 (exclusive shape도 함께 해제). 액터가 없으면 아무 일도 하지 않는다.
	Body.TermBody();
	if (HeightField)
	{
		HeightField->release();
		HeightField = nullptr;
	}
}
