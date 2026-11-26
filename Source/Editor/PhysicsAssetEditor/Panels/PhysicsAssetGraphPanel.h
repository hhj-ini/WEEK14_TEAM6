#pragma once

#include "PhysicsAssetEditorPanel.h"
#include "Containers/Array.h"
#include "imgui_node_editor.h"

// 고른 바디마다 한 묶음: 왼쪽에 그 바디, 가운데에 붙은 조인트, 오른쪽에 조인트 건너편 바디. 여러 개를 고르면 묶음을 위아래로 쌓는다.
// 빈 곳을 드래그하면 사각형 안의 노드를 고르고, Ctrl+클릭으로 넣거나 뺀다.
// 핀끼리 잇기는 막고, 바디 출력 핀을 빈 곳에 놓으면 바디 목록에서 골라 새 조인트를 만든다.
class FPhysicsAssetGraphPanel : public FPhysicsAssetEditorPanel
{
public:
	explicit FPhysicsAssetGraphPanel(FPhysicsAssetEditor& InEditor);
	~FPhysicsAssetGraphPanel() override;

protected:
	void DrawContent() override;

private:
	struct FGraphLink
	{
		int32 ConstraintIndex = -1;
		int32 SourceBody = -1;       // 이 조인트를 모은 가운데 바디
		int32 OtherBodyIndex = -1;   // 조인트 건너편 바디. 없으면(본이 메시에 없음) -1
	};

	void RebuildGraph(const TArray<int32>& Centers);
	TArray<int32> ComputeCenterBodies() const;   // 지금 선택으로 정한 가운데 바디 (정렬)
	bool HasBodyNode(int32 BodyIndex) const;
	bool HasConstraintNode(int32 ConstraintIndex) const;
	void PlaceProvisional();   // 크기를 재기 전 임시 위치
	void PlaceMeasured();      // 지난 프레임에 잰 노드 크기로 겹치지 않게 배치
	void DrawBodyNode(int32 BodyIndex, bool bCenter);
	void DrawConstraintNode(int32 ConstraintIndex);
	void DrawLinks();
	void HandleCreation();
	void HandleDeletion();
	void DrawNewConstraintMenu();
	void SyncSelection();

	ax::NodeEditor::EditorContext* Context = nullptr;

	// 지금 그래프가 보여 주는 것
	TArray<int32> CenterBodies;   // 고른 바디들 (왼쪽 열)
	TArray<int32> GraphBodies;    // 그리는 바디 전부. 가운데 바디가 앞에 온다
	TArray<FGraphLink> Links;     // 조인트마다 하나
	int32 LayoutBodyCount = -1;         // 바디·조인트 수가 바뀌면 다시 배치한다
	int32 LayoutConstraintCount = -1;

	// 노드 크기는 한 번 그려야 알 수 있으므로 세 프레임에 걸쳐 배치한다
	enum class ELayoutStage : uint8
	{
		None,
		Measure,    // 임시 위치에 그려 크기를 잰다
		Place,      // 잰 크기로 자리를 정한다
		Navigate,   // 바뀐 자리 전체가 보이게 화면을 맞춘다
	};
	ELayoutStage LayoutStage = ELayoutStage::None;

	TArray<uint64> LastGraphSelection;  // 마지막으로 본 노드 에디터 선택 (정렬, 사용자 조작 판정)

	// 빈 곳 드롭으로 연 새 조인트 메뉴
	int32 PendingSourceBody = -1;
	bool bOpenNewConstraintMenu = false;
	char MenuSearch[64] = {};

	// 삭제 키는 그리는 중에 배열을 바꾸지 않도록 End 뒤에 처리한다
	bool bPendingDelete = false;
};
