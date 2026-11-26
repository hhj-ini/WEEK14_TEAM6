#include "EnginePCH.h"
#include "Editor/KurisuEd/EditorSelection.h"

#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

void FEditorSelection::SelectActor(AActor* Actor)
{
	TArray<AActor*> NewActors;
	if (Actor) NewActors.Add(Actor);
	SetActorsInternal(std::move(NewActors), Actor);
}

void FEditorSelection::ToggleActor(AActor* Actor)
{
	if (!Actor) return;

	TArray<AActor*> NewActors = Actors;
	const int32 Index = NewActors.Find(Actor);
	if (Index != INDEX_NONE) NewActors.RemoveAt(Index);
	else NewActors.Add(Actor);
	SetActorsInternal(std::move(NewActors), Actor);
}

void FEditorSelection::SelectActors(const TArray<AActor*>& InActors, AActor* InAnchor)
{
	TArray<AActor*> NewActors;
	for (AActor* Actor : InActors)
	{
		if (Actor && NewActors.Find(Actor) == INDEX_NONE)
			NewActors.Add(Actor);
	}
	AActor* NewAnchor = InAnchor;
	if (!NewAnchor || NewActors.Find(NewAnchor) == INDEX_NONE)
		NewAnchor = NewActors.IsEmpty() ? nullptr : NewActors[0];
	SetActorsInternal(std::move(NewActors), NewAnchor);
}

void FEditorSelection::SelectComponent(UActorComponent* InComponent)
{
	if (InComponent)
	{
		AActor* Owner = InComponent->GetOwner();
		if (Owner && Owner != GetPrimaryActor())
		{
			FEditorSelectionBatch Batch(*this);
			SelectActor(Owner);
			Component = InComponent;
			MarkChanged();
			return;
		}
	}

	if (Component == InComponent) return;
	Component = InComponent;
	MarkChanged();
}

void FEditorSelection::Clear()
{
	SetActorsInternal({}, nullptr);
}

void FEditorSelection::RemoveActor(AActor* Actor)
{
	if (!Actor || !IsSelected(Actor))
	{
		if (Anchor == Actor) Anchor = nullptr;
		return;
	}

	TArray<AActor*> NewActors = Actors;
	NewActors.RemoveAt(NewActors.Find(Actor));
	AActor* NewAnchor = (Anchor == Actor) ? (NewActors.IsEmpty() ? nullptr : NewActors[0]) : Anchor;
	SetActorsInternal(std::move(NewActors), NewAnchor);
}

void FEditorSelection::RemoveActorsNotIn(const TArray<AActor*>& Valid)
{
	if (Actors.IsEmpty())
	{
		if (Anchor && Valid.Find(Anchor) == INDEX_NONE) Anchor = nullptr;
		return;
	}

	TArray<AActor*> NewActors;
	for (AActor* Actor : Actors)
	{
		if (Actor && Valid.Find(Actor) != INDEX_NONE)
			NewActors.Add(Actor);
	}
	if (NewActors.Num() == Actors.Num()) return;

	AActor* NewAnchor = (Anchor && NewActors.Find(Anchor) != INDEX_NONE)
		? Anchor : (NewActors.IsEmpty() ? nullptr : NewActors[0]);
	SetActorsInternal(std::move(NewActors), NewAnchor);
}

bool FEditorSelection::IsSelected(const AActor* Actor) const
{
	return Actor && Actors.Find(const_cast<AActor*>(Actor)) != INDEX_NONE;
}

void FEditorSelection::EndBatch()
{
	if (BatchDepth > 0 && --BatchDepth == 0 && bPendingNotify)
		MarkChanged();
}

void FEditorSelection::SetActorsInternal(TArray<AActor*>&& NewActors, AActor* NewAnchor)
{
	bool bSame = NewActors.Num() == Actors.Num();
	for (int32 i = 0; bSame && i < NewActors.Num(); ++i)
		bSame = NewActors[i] == Actors[i];

	Anchor = NewAnchor;
	if (bSame) return;

	Actors = std::move(NewActors);
	// 액터 선택이 바뀌면 고른 컴포넌트는 새 주 선택의 루트로 돌아간다.
	Component = nullptr;
	MarkChanged();
}

void FEditorSelection::MarkChanged()
{
	bPendingNotify = true;
	if (BatchDepth > 0 || bNotifying) return;

	bNotifying = true;
	// 알림을 받은 쪽이 다시 선택을 바꾸면 한 번 더 알린다. 서로 계속 바꾸는 경우를 막으려고 횟수를 제한한다.
	for (int32 Round = 0; bPendingNotify && Round < 4; ++Round)
	{
		bPendingNotify = false;
		OnChanged.Broadcast();
	}
	bPendingNotify = false;
	bNotifying = false;
}
