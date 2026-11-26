#include "EnginePCH.h"
#include "Render/ShowFlags.h"

#include <array>
#include <cctype>

namespace
{
	constexpr FShowFlagInfo GShowFlagInfos[] =
	{
#define SHOWFLAG(Name, bGameDefault, bEditorDefault, Category, Icon, DisplayName) \
		{ #Name, DisplayName, Icon, EShowFlagCategory::Category, bGameDefault, bEditorDefault, &FEngineShowFlags::Name },
#include "Render/ShowFlagsValues.h"
#undef SHOWFLAG
	};

	bool EqualsIgnoreCase(std::string_view A, std::string_view B)
	{
		if (A.size() != B.size()) return false;
		for (size_t i = 0; i < A.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(A[i])) != std::tolower(static_cast<unsigned char>(B[i])))
				return false;
		}
		return true;
	}
}

void FEngineShowFlags::Init(EShowFlagInitMode Mode)
{
	for (const FShowFlagInfo& Info : GShowFlagInfos)
		Set(Info, Mode == EShowFlagInitMode::Editor ? Info.bEditorDefault : Info.bGameDefault);
}

std::span<const FShowFlagInfo> FEngineShowFlags::GetAll()
{
	return GShowFlagInfos;
}

const FShowFlagInfo* FEngineShowFlags::Find(std::string_view Name)
{
	for (const FShowFlagInfo& Info : GShowFlagInfos)
		if (EqualsIgnoreCase(Info.Name, Name)) return &Info;
	return nullptr;
}

bool FEngineShowFlags::SetByName(std::string_view Name, bool bValue)
{
	const FShowFlagInfo* Info = Find(Name);
	if (!Info) return false;
	Set(*Info, bValue);
	return true;
}
