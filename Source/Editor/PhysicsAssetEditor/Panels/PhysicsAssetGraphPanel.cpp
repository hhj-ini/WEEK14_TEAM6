#include "EnginePCH.h"
#include "PhysicsAssetGraphPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Physics/PhysicsAsset.h"

#include <algorithm>

namespace NodeEditor = ax::NodeEditor;

namespace
{
	// 노드·핀·링크 ID. 종류마다 구간을 나눠 ID만 보고 무엇인지 알 수 있게 한다
	constexpr uint64 BodyBase = 1000;
	constexpr uint64 ConstraintBase = 1000000;
	constexpr uint64 LinkBase = 3000000;
	constexpr uint64 Stride = 4;   // 노드 +0, 입력 핀 +1, 출력 핀 +2

	uint64 BodyNodeId(int32 Body) { return BodyBase + static_cast<uint64>(Body) * Stride; }
	uint64 ConstraintNodeId(int32 Constraint) { return ConstraintBase + static_cast<uint64>(Constraint) * Stride; }

	int32 DecodeBodyNode(uint64 Id)
	{
		if (Id < BodyBase || Id >= ConstraintBase || (Id - BodyBase) % Stride != 0)
			return -1;
		return static_cast<int32>((Id - BodyBase) / Stride);
	}

	int32 DecodeConstraintNode(uint64 Id)
	{
		if (Id < ConstraintBase || Id >= LinkBase || (Id - ConstraintBase) % Stride != 0)
			return -1;
		return static_cast<int32>((Id - ConstraintBase) / Stride);
	}

	int32 DecodeBodyOutputPin(uint64 Id)
	{
		if (Id < BodyBase || Id >= ConstraintBase || (Id - BodyBase) % Stride != 2)
			return -1;
		return static_cast<int32>((Id - BodyBase) / Stride);
	}

	const ImVec4 BodyColor(0.55f, 0.74f, 0.49f, 1.0f);
	const ImVec4 ConstraintColor(0.74f, 0.71f, 0.52f, 1.0f);
	const ImVec4 NodeTextColor(0.06f, 0.06f, 0.06f, 1.0f);
	const ImVec4 SelectedBorderColor(1.0f, 0.62f, 0.10f, 1.0f);
	const ImVec4 RejectColor(1.0f, 0.3f, 0.3f, 1.0f);

	const char* RejectMessage = "Drag from the output pin of a body and drop on\nempty space to create a new constraint";

	template <typename T>
	bool SameArray(const TArray<T>& A, const TArray<T>& B)
	{
		return A.Num() == B.Num() && std::equal(A.begin(), A.end(), B.begin());
	}

	constexpr float PinRadius = 5.0f;
	constexpr float NodeTextWidth = 170.0f;

	// 핀 자리에 흰 원을 그린다
	void DrawPinCircle()
	{
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(PinRadius * 2.0f, PinRadius * 2.0f));
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(Pos.x + PinRadius, Pos.y + PinRadius), PinRadius, IM_COL32(235, 235, 235, 255), 12);
	}

	// [입력 핀] 글자 줄들 [출력 핀]. 핀은 글자 묶음 가운데 높이에 둔다
	void DrawNodeContent(uint64 NodeId, const char* const* Lines, int32 LineCount)
	{
		const float LineHeight = ImGui::GetTextLineHeightWithSpacing();
		const float BlockHeight = LineHeight * LineCount;
		const float PinOffset = std::max((BlockHeight - PinRadius * 2.0f) * 0.5f, 0.0f);
		const ImVec2 Start = ImGui::GetCursorPos();

		ImGui::SetCursorPos(ImVec2(Start.x, Start.y + PinOffset));
		NodeEditor::BeginPin(NodeEditor::PinId(NodeId + 1), NodeEditor::PinKind::Input);
		DrawPinCircle();
		NodeEditor::EndPin();

		ImGui::SameLine();
		ImGui::SetCursorPosY(Start.y);
		ImGui::BeginGroup();
		ImGui::PushStyleColor(ImGuiCol_Text, NodeTextColor);
		for (int32 i = 0; i < LineCount; ++i)
			ImGui::TextUnformatted(Lines[i]);
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(NodeTextWidth, 0.0f));
		ImGui::EndGroup();

		ImGui::SameLine();
		ImGui::SetCursorPosY(Start.y + PinOffset);
		NodeEditor::BeginPin(NodeEditor::PinId(NodeId + 2), NodeEditor::PinKind::Output);
		DrawPinCircle();
		NodeEditor::EndPin();
	}

	// 금지 표시(빨간 원 + 사선)와 안내 문구
	void ShowRejectTooltip()
	{
		NodeEditor::Suspend();
		ImGui::BeginTooltip();
		const float Size = ImGui::GetTextLineHeight();
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const ImVec2 Center(Pos.x + Size * 0.5f, Pos.y + Size * 0.5f);
		ImDrawList* DL = ImGui::GetWindowDrawList();
		const ImU32 Red = ImGui::ColorConvertFloat4ToU32(RejectColor);
		DL->AddCircle(Center, Size * 0.45f, Red, 16, 2.0f);
		DL->AddLine(ImVec2(Center.x - Size * 0.3f, Center.y - Size * 0.3f), ImVec2(Center.x + Size * 0.3f, Center.y + Size * 0.3f), Red, 2.0f);
		ImGui::Dummy(ImVec2(Size, Size));
		ImGui::SameLine();
		ImGui::TextUnformatted(RejectMessage);
		ImGui::EndTooltip();
		NodeEditor::Resume();
	}

	void ShowAcceptTooltip(const char* Text)
	{
		NodeEditor::Suspend();
		ImGui::SetTooltip("%s", Text);
		NodeEditor::Resume();
	}
}

FPhysicsAssetGraphPanel::FPhysicsAssetGraphPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Graph")
{
}

FPhysicsAssetGraphPanel::~FPhysicsAssetGraphPanel()
{
	if (Context)
		NodeEditor::DestroyEditor(Context);
}

void FPhysicsAssetGraphPanel::RebuildGraph(const TArray<int32>& Centers)
{
	CenterBodies.Empty();
	GraphBodies.Empty();
	Links.Empty();

	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	LayoutBodyCount = Asset ? Asset->BodySetup.Num() : -1;
	LayoutConstraintCount = Asset ? Asset->ConstraintSetup.Num() : -1;
	LayoutStage = ELayoutStage::Measure;
	if (!Asset)
		return;

	for (int32 Center : Centers)
	{
		if (Center >= 0 && Center < Asset->BodySetup.Num() && !CenterBodies.Contains(Center))
			CenterBodies.Add(Center);
	}
	GraphBodies = CenterBodies;

	// 가운데 바디마다 자식이든 부모든 붙어 있는 조인트를 모은다. 두 가운데 바디를 잇는 조인트는 한 번만 둔다
	for (int32 Center : CenterBodies)
	{
		const FName CenterName = Asset->BodySetup[Center]->BoneName;
		for (int32 i = 0; i < Asset->ConstraintSetup.Num(); ++i)
		{
			const FConstraintInstance& CI = Asset->ConstraintSetup[i]->DefaultInstance;
			if (CI.ConstraintBone1 != CenterName && CI.ConstraintBone2 != CenterName)
				continue;
			if (std::any_of(Links.begin(), Links.end(), [i](const FGraphLink& L) { return L.ConstraintIndex == i; }))
				continue;

			const FName Other = CI.ConstraintBone1 == CenterName ? CI.ConstraintBone2 : CI.ConstraintBone1;
			const int32 OtherBody = Asset->FindBodyIndex(Other);
			Links.Add({ i, Center, OtherBody });
			if (OtherBody >= 0 && !GraphBodies.Contains(OtherBody))
				GraphBodies.Add(OtherBody);
		}
	}
}

bool FPhysicsAssetGraphPanel::HasBodyNode(int32 BodyIndex) const
{
	return GraphBodies.Contains(BodyIndex);
}

bool FPhysicsAssetGraphPanel::HasConstraintNode(int32 ConstraintIndex) const
{
	return std::any_of(Links.begin(), Links.end(), [ConstraintIndex](const FGraphLink& L) { return L.ConstraintIndex == ConstraintIndex; });
}

void FPhysicsAssetGraphPanel::DrawBodyNode(int32 BodyIndex, bool bCenter)
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const UBodySetup* Setup = Asset->BodySetup[BodyIndex];
	const uint64 Id = BodyNodeId(BodyIndex);

	const FString Name = Setup->BoneName.ToString();
	const int32 Shapes = Setup->AggGeom.GetElementCount();
	char ShapeText[32];
	snprintf(ShapeText, sizeof(ShapeText), Shapes == 1 ? "%d shape" : "%d shapes", Shapes);
	const char* Lines[] = { "Body", Name.c_str(), ShapeText };

	NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, BodyColor);
	NodeEditor::BeginNode(NodeEditor::NodeId(Id));
	DrawNodeContent(Id, Lines, 3);
	NodeEditor::EndNode();
	NodeEditor::PopStyleColor();
}

void FPhysicsAssetGraphPanel::DrawConstraintNode(int32 ConstraintIndex)
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const FConstraintInstance& CI = Asset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
	const uint64 Id = ConstraintNodeId(ConstraintIndex);

	const FString Label = CI.ConstraintBone1.ToString() + " : " + CI.ConstraintBone2.ToString();
	const char* Lines[] = { "Constraint", Label.c_str() };

	NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, ConstraintColor);
	NodeEditor::BeginNode(NodeEditor::NodeId(Id));
	DrawNodeContent(Id, Lines, 2);
	NodeEditor::EndNode();
	NodeEditor::PopStyleColor();
}

void FPhysicsAssetGraphPanel::PlaceProvisional()
{
	// 크기를 모르는 첫 프레임. 넉넉히 떨어뜨려 두어 잰 크기가 서로 영향을 받지 않게 한다
	for (int32 i = 0; i < GraphBodies.Num(); ++i)
		NodeEditor::SetNodePosition(NodeEditor::NodeId(BodyNodeId(GraphBodies[i])), ImVec2(0.0f, i * 200.0f));
	for (int32 i = 0; i < Links.Num(); ++i)
		NodeEditor::SetNodePosition(NodeEditor::NodeId(ConstraintNodeId(Links[i].ConstraintIndex)), ImVec2(800.0f, i * 200.0f));
}

void FPhysicsAssetGraphPanel::PlaceMeasured()
{
	const float GapX = 60.0f;       // 열 사이
	const float GapY = 16.0f;       // 줄 사이
	const float BlockGapY = 48.0f;  // 가운데 바디 묶음 사이

	// 열 폭은 그 열에서 가장 넓은 노드에 맞춘다
	float CenterColumnWidth = 0.0f;
	for (int32 Center : CenterBodies)
		CenterColumnWidth = std::max(CenterColumnWidth, NodeEditor::GetNodeSize(NodeEditor::NodeId(BodyNodeId(Center))).x);
	float ConstraintColumnWidth = 0.0f;
	for (const FGraphLink& Link : Links)
		ConstraintColumnWidth = std::max(ConstraintColumnWidth, NodeEditor::GetNodeSize(NodeEditor::NodeId(ConstraintNodeId(Link.ConstraintIndex))).x);

	const float ConstraintX = CenterColumnWidth + GapX;
	const float OtherX = ConstraintX + ConstraintColumnWidth + GapX;

	// 가운데 바디 하나가 한 묶음: 왼쪽에 가운데 바디, 가운데 열에 조인트, 오른쪽 열에 건너편 바디.
	// 건너편 바디가 다른 묶음의 가운데 바디이거나 이미 놓였으면 다시 놓지 않는다 (링크만 이어진다)
	TArray<int32> Placed = CenterBodies;
	float BlockTop = 0.0f;
	for (int32 Center : CenterBodies)
	{
		TArray<int32> Rows;
		for (int32 i = 0; i < Links.Num(); ++i)
		{
			if (Links[i].SourceBody == Center)
				Rows.Add(i);
		}

		TArray<float> RowHeights;
		float RowsHeight = 0.0f;
		for (int32 Row : Rows)
		{
			const ImVec2 ConstraintSize = NodeEditor::GetNodeSize(NodeEditor::NodeId(ConstraintNodeId(Links[Row].ConstraintIndex)));
			const int32 Other = Links[Row].OtherBodyIndex;
			const ImVec2 OtherSize = Other >= 0 && !Placed.Contains(Other)
				? NodeEditor::GetNodeSize(NodeEditor::NodeId(BodyNodeId(Other))) : ImVec2(0.0f, 0.0f);
			RowHeights.Add(std::max(ConstraintSize.y, OtherSize.y));
			RowsHeight += RowHeights.Last();
		}
		RowsHeight += GapY * std::max(Rows.Num() - 1, 0);

		const ImVec2 CenterSize = NodeEditor::GetNodeSize(NodeEditor::NodeId(BodyNodeId(Center)));
		const float BlockHeight = std::max(CenterSize.y, RowsHeight);

		// 가운데 바디는 묶음의 세로 가운데에 맞춘다
		NodeEditor::SetNodePosition(NodeEditor::NodeId(BodyNodeId(Center)), ImVec2(0.0f, BlockTop + (BlockHeight - CenterSize.y) * 0.5f));

		float Y = BlockTop + (BlockHeight - RowsHeight) * 0.5f;
		for (int32 r = 0; r < Rows.Num(); ++r)
		{
			const FGraphLink& Link = Links[Rows[r]];
			const NodeEditor::NodeId ConstraintId(ConstraintNodeId(Link.ConstraintIndex));
			const ImVec2 ConstraintSize = NodeEditor::GetNodeSize(ConstraintId);
			NodeEditor::SetNodePosition(ConstraintId, ImVec2(ConstraintX, Y + (RowHeights[r] - ConstraintSize.y) * 0.5f));

			if (Link.OtherBodyIndex >= 0 && !Placed.Contains(Link.OtherBodyIndex))
			{
				const NodeEditor::NodeId OtherId(BodyNodeId(Link.OtherBodyIndex));
				const ImVec2 OtherSize = NodeEditor::GetNodeSize(OtherId);
				NodeEditor::SetNodePosition(OtherId, ImVec2(OtherX, Y + (RowHeights[r] - OtherSize.y) * 0.5f));
				Placed.Add(Link.OtherBodyIndex);
			}
			Y += RowHeights[r] + GapY;
		}
		BlockTop += BlockHeight + BlockGapY;
	}
}

void FPhysicsAssetGraphPanel::DrawLinks()
{
	const ImVec4 LinkColor(0.92f, 0.92f, 0.92f, 1.0f);
	for (const FGraphLink& Link : Links)
	{
		const uint64 ConstraintId = ConstraintNodeId(Link.ConstraintIndex);
		const uint64 LinkId = LinkBase + static_cast<uint64>(Link.ConstraintIndex) * 2;

		// 가운데 바디 출력 → 조인트 입력, 조인트 출력 → 건너편 바디 입력
		NodeEditor::Link(NodeEditor::LinkId(LinkId), NodeEditor::PinId(BodyNodeId(Link.SourceBody) + 2),
			NodeEditor::PinId(ConstraintId + 1), LinkColor, 1.5f);
		if (Link.OtherBodyIndex >= 0)
		{
			NodeEditor::Link(NodeEditor::LinkId(LinkId + 1), NodeEditor::PinId(ConstraintId + 2),
				NodeEditor::PinId(BodyNodeId(Link.OtherBodyIndex) + 1), LinkColor, 1.5f);
		}
	}
}

void FPhysicsAssetGraphPanel::HandleCreation()
{
	if (NodeEditor::BeginCreate(ImVec4(1, 1, 1, 1), 1.5f))
	{
		// 핀끼리는 잇지 않는다. 조인트는 노드이므로 핀 두 개만으로는 어느 쪽이 부모인지 정할 수 없다
		NodeEditor::PinId StartPin;
		NodeEditor::PinId EndPin;
		if (NodeEditor::QueryNewLink(&StartPin, &EndPin) && StartPin && EndPin)
		{
			ShowRejectTooltip();
			NodeEditor::RejectNewItem(RejectColor, 2.0f);
		}

		// 빈 곳에 놓기: 바디 출력 핀에서 시작했을 때만 새 조인트 메뉴를 연다
		NodeEditor::PinId SourcePin;
		if (NodeEditor::QueryNewNode(&SourcePin) && SourcePin)
		{
			const int32 SourceBody = DecodeBodyOutputPin(SourcePin.Get());
			if (SourceBody >= 0)
			{
				ShowAcceptTooltip("+ Create a new constraint");
				if (NodeEditor::AcceptNewItem())
				{
					PendingSourceBody = SourceBody;
					bOpenNewConstraintMenu = true;
				}
			}
			else
			{
				ShowRejectTooltip();
				NodeEditor::RejectNewItem(RejectColor, 2.0f);
			}
		}
	}
	NodeEditor::EndCreate();
}

void FPhysicsAssetGraphPanel::HandleDeletion()
{
	if (NodeEditor::BeginDelete())
	{
		// 노드 에디터가 직접 지우지 않게 모두 거절하고, 선택된 대상을 End 뒤에 에디터 API로 지운다
		NodeEditor::LinkId LinkId;
		while (NodeEditor::QueryDeletedLink(&LinkId))
			NodeEditor::RejectDeletedItem();

		NodeEditor::NodeId NodeId;
		while (NodeEditor::QueryDeletedNode(&NodeId))
		{
			NodeEditor::RejectDeletedItem();
			bPendingDelete = true;
		}
	}
	NodeEditor::EndDelete();
}

void FPhysicsAssetGraphPanel::DrawNewConstraintMenu()
{
	NodeEditor::Suspend();

	if (bOpenNewConstraintMenu)
	{
		ImGui::OpenPopup("##NewConstraintMenu");
		MenuSearch[0] = '\0';
		bOpenNewConstraintMenu = false;
	}

	if (ImGui::BeginPopup("##NewConstraintMenu"))
	{
		UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
		if (!Asset || PendingSourceBody < 0 || PendingSourceBody >= Asset->BodySetup.Num())
		{
			ImGui::CloseCurrentPopup();
		}
		else
		{
			ImGui::TextDisabled("New constraint from %s to:", Asset->BodySetup[PendingSourceBody]->BoneName.ToString().c_str());
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			ImGui::SetNextItemWidth(EditorStyle::Scale(220.0f));
			ImGui::InputTextWithHint("##Search", "Search...", MenuSearch, sizeof(MenuSearch));

			ImGui::BeginChild("##Bodies", ImVec2(EditorStyle::Scale(220.0f), EditorStyle::Scale(260.0f)));
			for (int32 i = 0; i < Asset->BodySetup.Num(); ++i)
			{
				// 자기 자신과 이미 이어진 바디는 고를 수 없다
				if (i == PendingSourceBody || Editor.FindConstraintBetween(PendingSourceBody, i) >= 0)
					continue;

				const FString Name = Asset->BodySetup[i]->BoneName.ToString();
				if (!EditorStyle::ContainsIgnoreCase(Name.c_str(), MenuSearch))
					continue;

				ImGui::PushID(i);
				if (ImGui::Selectable(Name.c_str()))
				{
					// 드래그를 시작한 바디가 부모, 고른 바디가 자식이 된다
					Editor.MakeNewConstraint(PendingSourceBody, i);
					PendingSourceBody = -1;
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
		ImGui::EndPopup();
	}

	NodeEditor::Resume();
}

void FPhysicsAssetGraphPanel::SyncSelection()
{
	// 1. 사용자가 그래프에서 노드를 고르면(클릭, Ctrl+클릭, 빈 곳 드래그 사각형) 에디터 선택으로 보낸다.
	//    빈 곳 클릭으로 모두 비운 경우는 무시한다
	const int32 Count = NodeEditor::GetSelectedObjectCount();
	TArray<NodeEditor::NodeId> SelectedNodes;
	SelectedNodes.SetNum(std::max(Count, 1));
	const int32 NodeCount = Count > 0 ? NodeEditor::GetSelectedNodes(SelectedNodes.GetData(), Count) : 0;

	TArray<uint64> Current;
	for (int32 i = 0; i < NodeCount; ++i)
		Current.Add(SelectedNodes[i].Get());
	std::sort(Current.begin(), Current.end());

	if (!SameArray(Current, LastGraphSelection) && !Current.IsEmpty())
	{
		LastGraphSelection = Current;
		TArray<FPhysicsAssetSelection> Items;
		for (uint64 Id : Current)
		{
			FPhysicsAssetSelection Item;
			if (const int32 Body = DecodeBodyNode(Id); Body >= 0)
			{
				Item.Type = EPhysicsAssetSelectionType::Body;
				Item.Index = Body;
			}
			else if (const int32 Constraint = DecodeConstraintNode(Id); Constraint >= 0)
			{
				Item.Type = EPhysicsAssetSelectionType::Constraint;
				Item.Index = Constraint;
			}
			else
			{
				continue;
			}
			Items.Add(Item);
		}
		Editor.SetSelections(Items);
	}

	// 2. 에디터 선택을 그래프 선택에 맞춘다 (트리·뷰포트에서 고른 경우). 그래프에 없는 항목은 건너뛴다
	TArray<uint64> Desired;
	for (const FPhysicsAssetSelection& Item : Editor.GetSelections())
	{
		if (Item.IsBody() && HasBodyNode(Item.Index))
			Desired.Add(BodyNodeId(Item.Index));
		else if (Item.IsConstraint() && HasConstraintNode(Item.Index))
			Desired.Add(ConstraintNodeId(Item.Index));
	}
	std::sort(Desired.begin(), Desired.end());

	if (!SameArray(Desired, Current))
	{
		NodeEditor::ClearSelection();
		for (uint64 Id : Desired)
			NodeEditor::SelectNode(NodeEditor::NodeId(Id), true);
		LastGraphSelection = Desired;
	}
}

TArray<int32> FPhysicsAssetGraphPanel::ComputeCenterBodies() const
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	TArray<int32> Centers = Editor.GetSelectedBodyIndices();
	const TArray<int32> Constraints = Editor.GetSelectedConstraintIndices();
	if (!Asset || (Centers.IsEmpty() && Constraints.IsEmpty()))
		return {};

	// 조인트만 골랐으면 지금 보이는 가운데 바디를 유지한다
	if (Centers.IsEmpty())
	{
		for (int32 Center : CenterBodies)
		{
			if (Center < Asset->BodySetup.Num())
				Centers.Add(Center);
		}
	}

	// 고른 조인트가 어느 가운데 바디에도 붙어 있지 않으면 그 조인트의 자식 바디를 가운데에 더한다
	for (int32 ConstraintIndex : Constraints)
	{
		const FConstraintInstance& CI = Asset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
		const bool bAttached = std::any_of(Centers.begin(), Centers.end(), [&](int32 Center)
		{
			const FName Name = Asset->BodySetup[Center]->BoneName;
			return CI.ConstraintBone1 == Name || CI.ConstraintBone2 == Name;
		});
		const int32 Child = Asset->FindBodyIndex(CI.ConstraintBone1);
		if (!bAttached && Child >= 0 && !Centers.Contains(Child))
			Centers.Add(Child);
	}

	// 고른 순서와 상관없이 같은 배치가 나오게 정렬한다
	std::sort(Centers.begin(), Centers.end());
	return Centers;
}

void FPhysicsAssetGraphPanel::DrawContent()
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Asset || Size.x < 1.0f || Size.y < 1.0f)
		return;

	if (!Context)
	{
		NodeEditor::Config Config;
		Config.SettingsFile = nullptr;   // 배치는 선택마다 다시 정하므로 파일로 남기지 않는다
		Context = NodeEditor::CreateEditor(&Config);
		if (!Context)
		{
			ImGui::TextUnformatted("Failed to create the graph view.");
			return;
		}
	}

	// 가운데 바디: 고른 바디 전부. 조인트는 지금 그래프에 붙어 있으면 그대로 두고, 아니면 자식 바디를 더한다
	const TArray<int32> DesiredCenters = ComputeCenterBodies();
	if (!SameArray(DesiredCenters, CenterBodies) || LayoutBodyCount != Asset->BodySetup.Num() || LayoutConstraintCount != Asset->ConstraintSetup.Num())
		RebuildGraph(DesiredCenters);

	NodeEditor::EditorContext* PreviousContext = NodeEditor::GetCurrentEditor();
	NodeEditor::SetCurrentEditor(Context);
	NodeEditor::Begin("PhysicsAssetGraph", Size);
	NodeEditor::PushStyleColor(NodeEditor::StyleColor_SelNodeBorder, SelectedBorderColor);
	NodeEditor::PushStyleVar(NodeEditor::StyleVar_SelectedNodeBorderWidth, 3.0f);
	NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodeRounding, 3.0f);

	const bool bHasGraph = !CenterBodies.IsEmpty();
	if (bHasGraph)
	{
		if (LayoutStage == ELayoutStage::Measure)
			PlaceProvisional();
		else if (LayoutStage == ELayoutStage::Place)
			PlaceMeasured();

		for (int32 Body : GraphBodies)
			DrawBodyNode(Body, CenterBodies.Contains(Body));
		for (const FGraphLink& Link : Links)
			DrawConstraintNode(Link.ConstraintIndex);
		DrawLinks();
	}

	HandleCreation();
	HandleDeletion();
	SyncSelection();
	DrawNewConstraintMenu();

	NodeEditor::PopStyleVar(2);
	NodeEditor::PopStyleColor();
	NodeEditor::End();

	// 측정 → 배치 → 화면 맞춤을 한 프레임씩 진행한다
	if (!bHasGraph)
	{
		LayoutStage = ELayoutStage::None;
	}
	else if (LayoutStage == ELayoutStage::Measure)
	{
		LayoutStage = ELayoutStage::Place;
	}
	else if (LayoutStage == ELayoutStage::Place)
	{
		LayoutStage = ELayoutStage::Navigate;
	}
	else if (LayoutStage == ELayoutStage::Navigate)
	{
		// 노드 전체가 여백을 두고 들어오게 확대·축소한다. 노드가 하나뿐이면 크게 확대되지 않게 가운데만 맞춘다
		if (GraphBodies.Num() == 1 && Links.IsEmpty())
			NodeEditor::CenterNodeOnScreen(NodeEditor::NodeId(BodyNodeId(GraphBodies[0])));
		else
			NodeEditor::NavigateToContent(0.0f);
		LayoutStage = ELayoutStage::None;
	}

	// F: 그래프 전체가 보이게 다시 맞춘다
	if (bHasGraph && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
		!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		NodeEditor::NavigateToContent(0.2f);
	}

	NodeEditor::SetCurrentEditor(PreviousContext);

	if (!bHasGraph)
	{
		const ImVec2 Min = ImGui::GetItemRectMin();
		ImGui::GetWindowDrawList()->AddText(ImVec2(Min.x + 10.0f, Min.y + 10.0f),
			ImGui::GetColorU32(ImGuiCol_TextDisabled), "Select a body or constraint.");
	}

	// 그리는 도중에 배열을 바꾸지 않도록 삭제는 여기서 한다
	if (bPendingDelete)
	{
		bPendingDelete = false;
		Editor.DeleteSelection();
	}
}
