#include "EnginePCH.h"
#include "StaticMeshActor.h"

#include "Components/PrimitiveComponent.h"
#include "Asset/AssetManager.h"
#include "Serialization/StructuredArchiveSlots.h"

AStaticMeshActor::AStaticMeshActor()
{
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("UStaticMeshComponent");
	StaticMeshComponent->SetupAttachment(GetRootComponent());
	// 레벨에 놓는 메시는 대부분 배경이라 Static으로 시작한다 (삼각형 충돌, 낮은 비용).
	// Simulate Physics를 켜면 UPrimitiveComponent가 Movable로 바꾼다.
	StaticMeshComponent->BodyInstance.Mobility = EBodyMobility::Static;
}

void AStaticMeshActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	const FArchive& Archive = Record.GetUnderlyingArchive();
	if (!Archive.IsLoading() || Archive.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate)
		|| !StaticMeshComponent->IsRegistered())
		return;

	// Scene loading registers the default mesh before reading the saved mesh and body settings.
	StaticMeshComponent->DestroyPhysicsState();
	StaticMeshComponent->MarkTransformDirty();
	StaticMeshComponent->MarkRenderStateDirty();
	StaticMeshComponent->CreatePhysicsState();
}

void AStaticMeshActor::SetPrimitiveType(EPrimitiveType Type)
{

}


void AStaticMeshActor::BeginPlay()
{
	Super::BeginPlay();
}
