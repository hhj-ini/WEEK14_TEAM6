#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"

#include "Engine/World.h"

struct FTransform;
class FImGuiRenderer;
class FEditorUndoHistory;
class FEditorSelection;
using FShadowPreviewQuery = std::function<bool(const ULightComponent*, FTexture2DArray*&, TArray<FLightShadowRenderData>&)>;

using DetailsDeleteComponentCallback = std::function<void(UActorComponent*)>;

class FDetailsPanel : public IEditorPanel
{
public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;

	void SelectComponent(UActorComponent* Component);
	void OnActorDestroyed(AActor* Actor);
	void DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent);

	const char* GetPanelName() const override { return "Details"; }
	bool IsFocused() const override { return bFocused; }

	void SetTarget(AActor* InTargetActor) 
	{ 
		TArray<AActor*> Single;
		if (InTargetActor)
		{
			Single.Add(InTargetActor);
		}
		SetTargets(Single);
	}
	void SetTargets(const TArray<AActor*>& InTargetActors)
	{
		TArray<AActor*> Filtered;
		for (AActor* Actor : InTargetActors)
		{
			if (Actor && Filtered.Find(Actor) == INDEX_NONE)
			{
				Filtered.Add(Actor);
			}
		}

		bool bSame = (Filtered.Num() == TargetActors.Num());
		if (bSame)
		{
			for (int32 i = 0; i < Filtered.Num(); ++i)
			{
				if (Filtered[i] != TargetActors[i])
				{
					bSame = false;
					break;
				}
			}
		}
		if (!bSame)
		{
			CommitPendingObjectEdit();
			EditingNameComponent = nullptr;
			PendingRootComponent = nullptr;
			PendingDeleteComponent = nullptr;
			PendingDragComponent = PendingDroppedComponent = nullptr;
		}

		TargetActors = std::move(Filtered);
		TargetActor = TargetActors.IsEmpty() ? nullptr : TargetActors.Last();
		TargetComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	}
	void SetTargetComponent(UActorComponent* InTargetComponent)
	{
		if (TargetComponent != InTargetComponent)
		{
			CommitPendingObjectEdit();
		}
		TargetComponent = InTargetComponent;
	}

	// 보여 줄 대상은 이 선택 객체를 따라간다. 컴포넌트를 고르면 선택 객체에 반영한다.
	void SetSelection(FEditorSelection* InSelection);
	void SetDeleteComponentCallback(DetailsDeleteComponentCallback InCallback) { DeleteCallback = InCallback; }
	void SetUndoHistory(FEditorUndoHistory* InHistory) { UndoHistory = InHistory; }

	void SetWorld(UWorld* InWorld)
	{
		if (World != InWorld)
		{
			bPendingEditCommit = false;
			PendingEditSnapshots.Empty();
		}
		World = InWorld;
	}
	const ULightComponent* GetSelectedLight() const;
	void SetShadowPreviewQuery(FShadowPreviewQuery InQuery) { QueryShadowPreview = std::move(InQuery); }
	void SetImGuiRenderer(FImGuiRenderer* InRenderer) { ImGuiRenderer = InRenderer; }

	ImFont* GetCustomFont() { return CustomFont; }

private:
	void DrawShadowPreview();
	void OnSelectionChanged();
	void DrawComponentNameEditor(UActorComponent* Component, UActorComponent*& Selected, bool bRequested);
	USceneComponent* PendingRootComponent = nullptr;
	FShadowPreviewQuery QueryShadowPreview;
	FImGuiRenderer* ImGuiRenderer = nullptr;
	float ShadowPreviewContrast = 32.0f;
	void DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent);
	void TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent);
	void CommitPendingObjectEdit();

	UWorld* World = nullptr;
	FEditorUndoHistory* UndoHistory = nullptr;
	bool bFocused = false;
	ImFont* CustomFont = nullptr;

	struct FPendingObjectEditSnapshot
	{
		FName ActorName;
		FName ComponentName;
		TArray<uint8> BeforeBytes;
	};

	bool bPendingEditCommit = false;
	TArray<FPendingObjectEditSnapshot> PendingEditSnapshots;

	TArray<AActor*> TargetActors;
	AActor* TargetActor = nullptr;
	UActorComponent* TargetComponent = nullptr;
	FEditorSelection* Selection = nullptr;
	DetailsDeleteComponentCallback DeleteCallback;

	UActorComponent* PendingDeleteComponent = nullptr;
	UActorComponent* EditingNameComponent = nullptr;
	char ComponentNameEditBuffer[256] = {};
	bool bFocusNameEdit = false;
	bool bIsEditingName = false;

	USceneComponent* PendingDroppedComponent = nullptr;
	USceneComponent* PendingDragComponent = nullptr;
};

