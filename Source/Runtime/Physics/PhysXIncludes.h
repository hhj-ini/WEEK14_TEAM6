#pragma once

// PhysX SDK 헤더를 모아 include하는 유일한 곳이다.
// - .cpp에서만 include한다. 헤더에는 physx 타입의 전방 선언만 둔다 (PxPhysicsAPI.h가 무거워 빌드가 느려진다).
// - using namespace physx는 여기서 하지 않는다. 필요한 .cpp에서만 쓴다.
// - PhysX 헤더 경고는 아래 push/pop 안에서만 끈다. 끌 경고가 생기면 disable에 번호를 추가한다.

#pragma warning(push)

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleSDK.h>

#pragma warning(pop)
