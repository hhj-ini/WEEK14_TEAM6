#pragma once

// 레벨 리소스 수명주기 틀. A·B·C는 각자 핸들러를 만들어 등록하고, D가 레벨 로드/언로드 때 Broadcast를 부른다.

#include "Containers/Array.h"

class UWorld;

class ILevelResourceHandler
{
public:
	virtual ~ILevelResourceHandler() = default;
	virtual void OnLevelLoad(UWorld* World) = 0;
	virtual void OnLevelUnload(UWorld* World) = 0;
};

class FLevelLifecycle
{
public:
	static void RegisterHandler(ILevelResourceHandler* Handler);
	static void UnregisterHandler(ILevelResourceHandler* Handler);

	static void BroadcastLevelLoad(UWorld* World);
	static void BroadcastLevelUnload(UWorld* World);
};
