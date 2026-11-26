#include "EnginePCH.h"
#include "PhysicalMaterial.h"
#include "PhysicsCore.h"
#include "PhysXIncludes.h"

namespace
{
	physx::PxCombineMode::Enum ToPxCombineMode(EPhysicsCombineMode Mode)
	{
		switch (Mode)
		{
		case EPhysicsCombineMode::Min:      return physx::PxCombineMode::eMIN;
		case EPhysicsCombineMode::Multiply: return physx::PxCombineMode::eMULTIPLY;
		case EPhysicsCombineMode::Max:      return physx::PxCombineMode::eMAX;
		default:                            return physx::PxCombineMode::eAVERAGE;
		}
	}
}

UPhysicalMaterial::~UPhysicalMaterial()
{
	// 에셋은 UAssetManager::Shutdown에서, 그 밖의 객체는 DestroyAllObjectsExceptEngine에서 지워진다.
	// 둘 다 PhysX SDK 정리(FPhysicsCore::Shutdown)보다 먼저라 여기서 release해도 된다.
	// 이 머티리얼을 쓰는 shape가 남아 있어도 PhysX가 참조 수로 관리한다.
	if (PxMaterial)
	{
		PxMaterial->release();
		PxMaterial = nullptr;
	}
}

physx::PxMaterial* UPhysicalMaterial::GetPxMaterial()
{
	if (!PxMaterial && FPhysicsCore::Get().IsInitialized())
	{
		PxMaterial = FPhysicsCore::Get().GetPhysics()->createMaterial(StaticFriction, Friction, Restitution);
		if (PxMaterial)
		{
			PxMaterial->userData = this;
			UpdatePxMaterial();
		}
	}
	return PxMaterial;
}

void UPhysicalMaterial::UpdatePxMaterial()
{
	if (!PxMaterial)
		return;

	// 머티리얼은 씬에 속하지 않아 씬 락이 필요 없다. simulate 중에 바꾸면 다음 스텝부터 반영된다.
	PxMaterial->setStaticFriction(StaticFriction);
	PxMaterial->setDynamicFriction(Friction);
	PxMaterial->setRestitution(Restitution);
	PxMaterial->setFrictionCombineMode(ToPxCombineMode(FrictionCombineMode));
	PxMaterial->setRestitutionCombineMode(ToPxCombineMode(RestitutionCombineMode));
}

void UPhysicalMaterial::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	UpdatePxMaterial();
}
