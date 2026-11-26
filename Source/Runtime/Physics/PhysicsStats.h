#pragma once
#include "Core/Stats/LightweightStats.h"

namespace PhysicsStats
{
	DECLARE_CYCLE_STAT("Phys: Simulate", STAT_PhysSimulate);
	DECLARE_CYCLE_STAT("Phys: Sync Actors", STAT_PhysSyncActors);
	DECLARE_CYCLE_STAT("Phys: Dispatch Notifies", STAT_PhysNotifies);
	DECLARE_CYCLE_STAT("Ragdoll: Init Bodies", STAT_RagdollInit);
	DECLARE_CYCLE_STAT("Ragdoll: Kinematic Update", STAT_RagdollKinematic);
	DECLARE_CYCLE_STAT("Ragdoll: Physics To Bones", STAT_RagdollBonePose);
}