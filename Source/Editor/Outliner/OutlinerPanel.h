#pragma once

#include "Editor/EditorUI/EditorPanel.h"

#include <functional>

class UWorld;
class ULevel;
class AActor;
class UActorComponent;
class UPrimitiveComponent;
class USceneComponent;
class FEditorUndoHistory;

class FEditorSelection;

using DeleteActorCallback = std::function<void(AActor*)>;
using DeleteActorsCallback = std::function<void(const TArray<AActor*>&)>;

class FOutlinerPanel : public IEditorPanel
{
public:
    FOutlinerPanel() = default;
    ~FOutlinerPanel() = default;

    // 선택 상태는 이 객체가 가진다. 패널은 읽고 클릭을 여기에 반영하기만 한다.
    void SetSelection(FEditorSelection* InSelection) { Selection = InSelection; }
    void SetDeleteActorCallback(DeleteActorCallback InCallback) { DeleteCallback = std::move(InCallback); }
    void SetDeleteActorsCallback(DeleteActorsCallback InCallback) { MultiDeleteCallback = std::move(InCallback); }
    void SetUndoHistory(FEditorUndoHistory* InHistory) { UndoHistory = InHistory; }

public:
    bool Init() override;
    void Tick(float DeltaTime) override;
    void OnRender() override;
    const char* GetPanelName() const override { return "Outliner"; }

    void SetWorld(UWorld* InWorld) { World = InWorld; };

    void DrawActors(ULevel* Level);
    void DrawActorNode(AActor* Actor);
    void OnActorDestroyed(AActor* Actor);
	bool IsFocused() const { return bFocused; }


private:
    // 아웃라이너 클릭을 선택 객체에 반영한다.
    void SelectActor(AActor* Actor, bool bAdditiveToggle = false, bool bRangeSelect = false);
    void PruneInvalidSelections(ULevel* Level);
    AActor* GetSelectedActor() const;
    int32 GetSelectedActorCount() const;
    bool IsActorSelected(const AActor* Actor) const;

    UWorld* World = nullptr;
    FEditorSelection* Selection = nullptr;
    // Shift 범위 선택에 쓰는, 이번 프레임에 그린 순서
    TArray<AActor*> VisibleActorOrder;

    DeleteActorCallback DeleteCallback;
    DeleteActorsCallback MultiDeleteCallback;
    FEditorUndoHistory* UndoHistory = nullptr;
    AActor* PendingDeleteActor = nullptr;
    TArray<AActor*> PendingDeleteActors;
	bool bFocused = false;
	AActor* RenamingActor = nullptr;
	char RenameBuffer[256] = {};
	bool bFocusRename = false;
	TMap<AActor*, TArray<AActor*>> ActorChildren;
	USceneComponent* PendingAttachChild = nullptr;
	USceneComponent* PendingAttachParent = nullptr;
	bool bPendingDetach = false;

	// 검색어. 이름에 포함되거나(대소문자 무시) 자손 중에 포함된 액터만 보인다.
	char SearchBuffer[128] = {};
	bool IsFiltering() const { return SearchBuffer[0] != '\0'; }
	bool MatchesSearch(AActor* Actor) const;
};
