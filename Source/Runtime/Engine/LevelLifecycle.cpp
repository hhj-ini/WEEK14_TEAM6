#include "EnginePCH.h"
#include "Engine/LevelLifecycle.h"

namespace
{
	// 다른 파일의 static 객체 생성자에서 등록해도 안전하도록 함수 안 static으로 둔다.
	TArray<ILevelResourceHandler*>& GetHandlers()
	{
		static TArray<ILevelResourceHandler*> Handlers;
		return Handlers;
	}
}

void FLevelLifecycle::RegisterHandler(ILevelResourceHandler* Handler)
{
	if (Handler) GetHandlers().AddUnique(Handler);
}

void FLevelLifecycle::UnregisterHandler(ILevelResourceHandler* Handler)
{
	GetHandlers().Remove(Handler);
}

void FLevelLifecycle::BroadcastLevelLoad(UWorld* World)
{
	for (ILevelResourceHandler* Handler : GetHandlers())
		Handler->OnLevelLoad(World);
}

void FLevelLifecycle::BroadcastLevelUnload(UWorld* World)
{
	// 만든 순서의 반대로 해제한다.
	TArray<ILevelResourceHandler*>& Handlers = GetHandlers();
	for (int32 i = Handlers.Num() - 1; i >= 0; --i)
		Handlers[i]->OnLevelUnload(World);
}
