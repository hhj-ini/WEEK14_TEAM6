#pragma once

// NvCloth 1.1.6 SDK 헤더를 모아 include하는 유일한 곳이다.
// - .cpp에서만 include한다. 헤더에는 nv::cloth 타입의 전방 선언만 둔다.
// - PhysX 4.1의 foundation/* 헤더를 공유한다.

#pragma warning(push)
#pragma warning(disable : 4100 4127 4244 4267 4324 4458)

#include <foundation/PxErrorCallback.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>
#include <foundation/PxQuat.h>
#include <foundation/PxMat44.h>

#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Cloth.h>
#include <NvCloth/Solver.h>
#include <NvCloth/PhaseConfig.h>
#include <NvCloth/Range.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <NvClothExt/ClothMeshDesc.h>

#pragma warning(pop)
