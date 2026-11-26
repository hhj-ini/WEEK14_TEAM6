#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Core/Delegate/Delegate.h"

class AActor;
class UActorComponent;

// 레벨 에디터에서 무엇이 선택됐는지의 원본.
// 아웃라이너, 디테일, 기즈모, 외곽선은 이 값을 읽고 OnChanged로 따라간다. 서로를 직접 부르지 않는다.
// 액터 목록의 마지막이 주 선택(Primary)이다. 기즈모가 붙고 디테일이 보여 주는 액터다.
class FEditorSelection
{
public:
	DECLARE_DELEGATE(FOnSelectionChanged);

	// ---------- 바꾸기 ----------
	// Actor 하나만 선택한다. nullptr이면 선택을 비운다.
	void SelectActor(AActor* Actor);
	// 선택돼 있으면 빼고 아니면 더한다. 기준(Anchor)은 Actor가 된다.
	void ToggleActor(AActor* Actor);
	// 목록 그대로 선택한다. 순서를 지키고 마지막이 주 선택이다. Anchor가 nullptr이면 첫 액터가 기준이다.
	void SelectActors(const TArray<AActor*>& Actors, AActor* Anchor = nullptr);
	// 주 선택 액터 안의 컴포넌트를 고른다. nullptr이면 액터 자체(루트)를 고른 것으로 본다.
	// 다른 액터의 컴포넌트면 그 액터 하나만 선택한다.
	void SelectComponent(UActorComponent* Component);
	void Clear();

	// 삭제된 액터를 선택에서 뺀다.
	void RemoveActor(AActor* Actor);
	// Valid에 없는 액터를 선택에서 뺀다.
	void RemoveActorsNotIn(const TArray<AActor*>& Valid);
	// 선택은 그대로인데 선택된 대상의 구성(컴포넌트 추가·삭제 등)이 바뀌었을 때 따라오는 쪽을 다시 갱신한다.
	void NotifyChanged() { MarkChanged(); }

	// ---------- 읽기 ----------
	const TArray<AActor*>& GetActors() const { return Actors; }
	AActor* GetPrimaryActor() const { return Actors.IsEmpty() ? nullptr : Actors.Last(); }
	AActor* GetAnchorActor() const { return Anchor; }
	// 고른 컴포넌트. 액터 자체를 고른 상태면 nullptr이다.
	UActorComponent* GetComponent() const { return Component; }
	bool IsSelected(const AActor* Actor) const;
	bool IsEmpty() const { return Actors.IsEmpty(); }

	// ---------- 알림 ----------
	// 여러 번 바꿔도 알림은 마지막에 한 번만 보낸다.
	void BeginBatch() { ++BatchDepth; }
	void EndBatch();

	// 선택이 실제로 바뀌었을 때 한 번 불린다. 알림 도중에 선택을 바꾸면 알림이 끝난 뒤 한 번 더 불린다.
	FOnSelectionChanged OnChanged;

private:
	void SetActorsInternal(TArray<AActor*>&& NewActors, AActor* NewAnchor);
	void MarkChanged();

	TArray<AActor*> Actors;
	AActor* Anchor = nullptr;
	UActorComponent* Component = nullptr;

	int32 BatchDepth = 0;
	bool bPendingNotify = false;
	bool bNotifying = false;
};

// 범위 안의 변경을 알림 한 번으로 묶는다.
struct FEditorSelectionBatch
{
	explicit FEditorSelectionBatch(FEditorSelection& InSelection) : Selection(InSelection) { Selection.BeginBatch(); }
	~FEditorSelectionBatch() { Selection.EndBatch(); }
	FEditorSelectionBatch(const FEditorSelectionBatch&) = delete;
	FEditorSelectionBatch& operator=(const FEditorSelectionBatch&) = delete;

private:
	FEditorSelection& Selection;
};
