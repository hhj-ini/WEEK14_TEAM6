#include "EnginePCH.h"
#include "PhysicsConstraintTemplate.h"

void UPhysicsConstraintTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	Record << SA_VALUE("Bone1", DefaultInstance.ConstraintBone1);
	Record << SA_VALUE("Bone2", DefaultInstance.ConstraintBone2);
}
