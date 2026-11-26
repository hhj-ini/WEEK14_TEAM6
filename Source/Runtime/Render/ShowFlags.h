#pragma once

#include "Core/Types.h"
#include <span>
#include <string_view>

// 뷰포트가 무엇을 보여 줄지(표시 여부)만 담는다.
// 그림자 해상도, 감마 같은 "어떻게 그릴지"는 FSceneRenderSettings(SceneRenderOptions.h)가 담는다.
// 플래그 목록은 ShowFlagsValues.h 한 곳에서 정의한다.

enum class EShowFlagInitMode : uint8
{
	Game,
	Editor,
};

enum class EShowFlagCategory : uint8
{
	Common,
	Physics,
	PostProcess,
	Developer,
	Render,     // Show 메뉴 대신 Render 메뉴에 나온다. 저장한다.
	Hidden,     // 뷰 모드·코드가 매 프레임 정한다. 메뉴에 나오지 않고 저장하지 않는다.
};

struct FEngineShowFlags;

// 플래그 하나의 메타데이터. 메뉴·ini처럼 플래그를 이름으로 다루는 코드가 쓴다.
struct FShowFlagInfo
{
	const char* Name;
	const char* DisplayName;
	const char* Icon;
	EShowFlagCategory Category;
	bool bGameDefault;
	bool bEditorDefault;
	bool FEngineShowFlags::* Member;
};

struct FEngineShowFlags
{
#define SHOWFLAG(Name, bGameDefault, bEditorDefault, Category, Icon, DisplayName) bool Name = bGameDefault;
#include "Render/ShowFlagsValues.h"
#undef SHOWFLAG

	FEngineShowFlags() = default;
	explicit FEngineShowFlags(EShowFlagInitMode Mode) { Init(Mode); }

	// 모든 플래그를 해당 모드의 기본값으로 되돌린다.
	void Init(EShowFlagInitMode Mode);

	// 전체 플래그 목록. 선언 순서와 같다.
	static std::span<const FShowFlagInfo> GetAll();
	// 이름으로 찾는다. 대소문자를 구분하지 않는다. 없으면 nullptr.
	static const FShowFlagInfo* Find(std::string_view Name);

	bool Get(const FShowFlagInfo& Info) const { return this->*Info.Member; }
	void Set(const FShowFlagInfo& Info, bool bValue) { this->*Info.Member = bValue; }
	// 이름으로 설정한다. 이름이 없으면 false.
	bool SetByName(std::string_view Name, bool bValue);

	bool operator==(const FEngineShowFlags&) const = default;
};
