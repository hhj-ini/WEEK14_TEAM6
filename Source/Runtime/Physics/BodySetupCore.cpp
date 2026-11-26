#include "EnginePCH.h"
#include "BodySetupCore.h"

void UBodySetupCore::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	// UPROPERTY 가 아니라서 직접 씀
	Record << SA_VALUE("BoneName", BoneName);
}
