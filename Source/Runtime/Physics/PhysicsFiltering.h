#pragma once

#include "Core/Types.h"
#include "PhysicsFiltering.generated.h"

// 바디의 종류. 한 비트씩 쓰므로 word0(내 종류)·word1(받아 주는 종류)에 OR로 담는다.
UENUM()
enum class ECollisionCategory : uint8
{
	WorldStatic,    // 바닥, 벽
	WorldDynamic,   // 상자 등 일반 물체
	Pawn,           // 캐릭터 캡슐
	PhysicsBody,    // 랙돌 본
	Vehicle,        // 차체
	Count,
};

constexpr uint32 CategoryBit(ECollisionCategory Category) { return 1u << static_cast<uint32>(Category); }
constexpr uint32 AllCategoriesMask = (1u << static_cast<uint32>(ECollisionCategory::Count)) - 1u;

// word3 플래그
namespace ECollisionFlags
{
	constexpr uint32 Drivable = 1u << 0;   // 차량 서스펜션 레이캐스트가 바퀴를 올릴 수 있는 표면
}

// 이 바디가 어떤 충돌에 참여하는지. UE의 ECollisionEnabled와 같다.
UENUM()
enum class ECollisionEnabled : uint8
{
	NoCollision,       // 바디를 만들지 않는다. 렌더링만 한다 (장식용 소품, 먼 배경)
	QueryOnly,         // 레이캐스트·스윕·오버랩에만 걸린다. 물리로 밀거나 밀리지 않는다 (트리거, 클릭 판정)
	PhysicsOnly,       // 물리로 부딪히지만 쿼리에는 걸리지 않는다 (파편)
	QueryAndPhysics,   // 둘 다 (기본)
};

// PxFilterData 대응. 팀 규칙: word0 카테고리, word1 마스크, word2 랙돌 소유자 ID, word3 플래그.
// PhysX 타입이 필요한 변환·Filter Shader 선언은 PhysXFilterShader.h(cpp 전용)에 있다.
struct FMaskFilter
{
	uint32 Category = 0;      // word0: 이 바디의 종류 (CategoryBit 하나)
	uint32 CollidesWith = 0;  // word1: 충돌을 받아 주는 종류들 (CategoryBit OR)
	uint32 OwnerId = 0;       // word2: 0이 아니고 서로 같으면 충돌하지 않는다 (같은 랙돌의 본끼리)
	uint32 Flags = 0;         // word3: ECollisionFlags
};
