#include "EnginePCH.h"
#include "PhysicsAssetSkeletonTreePanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>

namespace
{
	void DrawBoneIcon(ImDrawList* DL, ImVec2 Center, float Size, ImU32 Color)
	{
		const float Half = Size * 0.5f;
		const ImVec2 P1(Center.x - Half * 0.7f, Center.y + Half * 0.7f);
		const ImVec2 P2(Center.x + Half * 0.7f, Center.y - Half * 0.7f);
		const float Thickness = EditorStyle::Scale(1.6f);
		const float R = EditorStyle::Scale(1.4f);
		const float Offset = EditorStyle::Scale(1.2f);
		DL->AddLine(P1, P2, Color, Thickness);
		DL->AddCircleFilled(ImVec2(P1.x - Offset, P1.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P1.x + Offset, P1.y + Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x - Offset, P2.y - Offset), R, Color);
		DL->AddCircleFilled(ImVec2(P2.x + Offset, P2.y + Offset), R, Color);
	}

	// 비스듬히 누운 캡슐. 속은 어둡게 채우고 테두리만 아이콘 색으로 그린다
	void DrawBodyIcon(ImDrawList* DL, ImVec2 Center, float Size, ImU32 Color)
	{
		const float Half = Size * 0.5f;
		const float Radius = Size * 0.27f;
		const float Offset = (Half - Radius) * 0.7071f;   // 축은 왼쪽 아래 → 오른쪽 위 (45도)
		const ImVec2 P1(Center.x - Offset, Center.y + Offset);
		const ImVec2 P2(Center.x + Offset, Center.y - Offset);
		const float Axis = -IM_PI * 0.25f;                 // P1 → P2 방향 (화면 Y는 아래로)

		// 위쪽 반원(P2 둘레) → 아래쪽 반원(P1 둘레)으로 이어 닫힌 외곽선을 만든다
		const auto BuildOutline = [&]()
		{
			DL->PathArcTo(P2, Radius, Axis - IM_PI * 0.5f, Axis + IM_PI * 0.5f, 10);
			DL->PathArcTo(P1, Radius, Axis + IM_PI * 0.5f, Axis + IM_PI * 1.5f, 10);
		};
		BuildOutline();
		DL->PathFillConvex(EditorColor::WithAlpha(EditorColor::Ink, 200));
		BuildOutline();
		DL->PathStroke(Color, ImDrawFlags_Closed, EditorStyle::Scale(1.5f));
	}

	// 고리 두 개가 이어진 모양
	void DrawConstraintIcon(ImDrawList* DL, ImVec2 Center, float Size, ImU32 Color)
	{
		const float R = Size * 0.22f;
		const ImVec2 P1(Center.x - Size * 0.22f, Center.y + Size * 0.22f);
		const ImVec2 P2(Center.x + Size * 0.22f, Center.y - Size * 0.22f);
		const float Thickness = EditorStyle::Scale(1.4f);
		DL->AddCircle(P1, R, Color, 12, Thickness);
		DL->AddCircle(P2, R, Color, 12, Thickness);
		DL->AddLine(P1, P2, Color, Thickness);
	}
}

FPhysicsAssetSkeletonTreePanel::FPhysicsAssetSkeletonTreePanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Skeleton Tree")
{
}

int32 FPhysicsAssetSkeletonTreePanel::AddNode(ENodeKind Kind, int32 Index, int32 BoneIndex, const FString& Label)
{
	FTreeNode Node;
	Node.Kind = Kind;
	Node.Index = Index;
	Node.BoneIndex = BoneIndex;
	Node.Label = Label;
	Nodes.Add(std::move(Node));
	return Nodes.Num() - 1;
}

TArray<int32> FPhysicsAssetSkeletonTreePanel::BuildBoneSubtree(int32 BoneIndex, const TArray<TArray<int32>>& ChildBones,
	const TArray<TArray<int32>>& ConstraintsByChildBone)
{
	const TArray<FEditorBoneItem>& BoneItems = Editor.GetBoneItems();
	const FString& BoneName = BoneIndex < BoneItems.Num() ? BoneItems[BoneIndex].Name : Editor.GetMesh()->GetRefSkeleton()[BoneIndex].Name;

	// 자식 본 쪽을 먼저 만든다. 노드 배열이 자라므로 인덱스로만 들고 있는다
	TArray<int32> ChildResults;
	for (int32 Child : ChildBones[BoneIndex])
		ChildResults.Append(BuildBoneSubtree(Child, ChildBones, ConstraintsByChildBone));

	// 조인트는 자식 바디 아래에 둔다
	TArray<int32> ConstraintNodes;
	if (bShowConstraints)
	{
		UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
		for (int32 ConstraintIndex : ConstraintsByChildBone[BoneIndex])
		{
			const FConstraintInstance& CI = Asset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
			ConstraintNodes.Add(AddNode(ENodeKind::Constraint, ConstraintIndex, BoneIndex,
				CI.ConstraintBone1.ToString() + " : " + CI.ConstraintBone2.ToString()));
		}
	}

	const int32 BodyIndex = Editor.FindBodyIndexForBone(BoneIndex);
	int32 BodyNode = -1;
	if (bShowBodies && BodyIndex >= 0)
	{
		BodyNode = AddNode(ENodeKind::Body, BodyIndex, BoneIndex, BoneName);
		Nodes[BodyNode].Children = ConstraintNodes;
	}

	TArray<int32> Result;
	if (bShowBones)
	{
		// 본 행 아래에 [바디(그 아래 조인트) 또는 조인트], 그다음 자식 본
		const int32 BoneNode = AddNode(ENodeKind::Bone, BoneIndex, BoneIndex, BoneName);
		TArray<int32> Kids;
		if (BodyNode >= 0)
			Kids.Add(BodyNode);
		else
			Kids.Append(ConstraintNodes);
		Kids.Append(ChildResults);
		Nodes[BoneNode].Children = std::move(Kids);
		Result.Add(BoneNode);
	}
	else if (BodyNode >= 0)
	{
		// 본을 숨기면 바디가 바디 있는 가장 가까운 조상 아래로 올라간다
		Nodes[BodyNode].Children.Append(ChildResults);
		Result.Add(BodyNode);
	}
	else
	{
		// 이 본은 보이지 않으므로 자식들을 한 단계 위로 올린다
		Result = std::move(ConstraintNodes);
		Result.Append(ChildResults);
	}
	return Result;
}

bool FPhysicsAssetSkeletonTreePanel::UpdateVisibility(int32 NodeIndex)
{
	bool bVisible = EditorStyle::ContainsIgnoreCase(Nodes[NodeIndex].Label.c_str(), SearchBuffer);
	const TArray<int32> Children = Nodes[NodeIndex].Children;
	for (int32 Child : Children)
		bVisible |= UpdateVisibility(Child);   // 자손이 일치하면 경로를 보이게 남긴다
	Nodes[NodeIndex].bVisible = bVisible;
	return bVisible;
}

void FPhysicsAssetSkeletonTreePanel::BuildTree()
{
	Nodes.Empty();
	RootNodes.Empty();

	USkeletalMesh* Mesh = Editor.GetMesh();
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	if (!Mesh || !Asset)
		return;

	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();

	TArray<TArray<int32>> ChildBones;
	ChildBones.SetNum(Bones.Num());
	for (int32 B = 0; B < Bones.Num(); ++B)
	{
		if (Bones[B].ParentIndex >= 0)
			ChildBones[Bones[B].ParentIndex].Add(B);
	}

	TArray<TArray<int32>> ConstraintsByChildBone;
	ConstraintsByChildBone.SetNum(Bones.Num());
	for (int32 i = 0; i < Asset->ConstraintSetup.Num(); ++i)
	{
		if (!Asset->ConstraintSetup[i])
			continue;
		const int32 ChildBone = Mesh->FindBoneIndex(Asset->ConstraintSetup[i]->DefaultInstance.ConstraintBone1.ToString());
		if (ChildBone >= 0)
			ConstraintsByChildBone[ChildBone].Add(i);
	}

	for (int32 B = 0; B < Bones.Num(); ++B)
	{
		if (Bones[B].ParentIndex < 0)
			RootNodes.Append(BuildBoneSubtree(B, ChildBones, ConstraintsByChildBone));
	}

	for (int32 Root : RootNodes)
		UpdateVisibility(Root);
}

bool FPhysicsAssetSkeletonTreePanel::IsNodeSelected(const FTreeNode& Node) const
{
	switch (Node.Kind)
	{
	case ENodeKind::Body:       return Editor.IsBodySelected(Node.Index);
	case ENodeKind::Constraint: return Editor.IsConstraintSelected(Node.Index);
	case ENodeKind::Bone:
		// 바디가 보이는 중이면 바디 행이 선택을 표시한다
		if (bShowBodies && Editor.FindBodyIndexForBone(Node.Index) >= 0)
			return false;
		return Editor.GetSelection().Type == EPhysicsAssetSelectionType::None && Editor.GetSelectedBoneIndex() == Node.Index;
	}
	return false;
}

bool FPhysicsAssetSkeletonTreePanel::ToSelectionItem(ENodeKind Kind, int32 NodeIndex, FPhysicsAssetSelection& OutItem) const
{
	OutItem = FPhysicsAssetSelection();
	int32 Index = NodeIndex;
	EPhysicsAssetSelectionType Type = EPhysicsAssetSelectionType::None;
	if (Kind == ENodeKind::Body)
		Type = EPhysicsAssetSelectionType::Body;
	else if (Kind == ENodeKind::Constraint)
		Type = EPhysicsAssetSelectionType::Constraint;
	else if ((Index = Editor.FindBodyIndexForBone(NodeIndex)) >= 0)
		Type = EPhysicsAssetSelectionType::Body;   // 바디가 있는 본 행은 그 바디로 고른다
	if (Type == EPhysicsAssetSelectionType::None)
		return false;
	OutItem.Type = Type;
	OutItem.Index = Index;
	return true;
}

void FPhysicsAssetSkeletonTreePanel::SelectNode(const FTreeNode& Node)
{
	const ImGuiIO& IO = ImGui::GetIO();
	FPhysicsAssetSelection Item;
	const bool bSelectable = ToSelectionItem(Node.Kind, Node.Index, Item);

	// Shift: 마지막으로 누른 행부터 이 행까지 보이는 순서대로 모두 고른다
	if (IO.KeyShift && bSelectable && AnchorKind >= 0)
	{
		int32 AnchorRow = -1;
		int32 ClickedRow = -1;
		for (int32 Row = 0; Row < LastDrawOrder.Num(); ++Row)
		{
			const FRowKey& Drawn = LastDrawOrder[Row];
			if (static_cast<int32>(Drawn.Kind) == AnchorKind && Drawn.Index == AnchorIndex)
				AnchorRow = Row;
			if (Drawn.Kind == Node.Kind && Drawn.Index == Node.Index)
				ClickedRow = Row;
		}
		if (AnchorRow >= 0 && ClickedRow >= 0)
		{
			TArray<FPhysicsAssetSelection> Items;
			const int32 Step = ClickedRow >= AnchorRow ? 1 : -1;
			for (int32 Row = AnchorRow; ; Row += Step)
			{
				FPhysicsAssetSelection RowItem;
				if (ToSelectionItem(LastDrawOrder[Row].Kind, LastDrawOrder[Row].Index, RowItem))
					Items.Add(RowItem);   // 누른 행이 마지막에 들어가 주 선택이 된다
				if (Row == ClickedRow)
					break;
			}
			Editor.SetSelections(Items);
			return;
		}
	}

	AnchorKind = static_cast<int32>(Node.Kind);
	AnchorIndex = Node.Index;

	// Ctrl: 이 행만 넣거나 뺀다
	if (IO.KeyCtrl && bSelectable)
	{
		if (Item.IsBody())
			Editor.SelectBody(Item.Index, -1, EPhysicsAssetSelectMode::Toggle);
		else
			Editor.SelectConstraint(Item.Index, EPhysicsAssetSelectMode::Toggle);
		return;
	}

	switch (Node.Kind)
	{
	case ENodeKind::Body:
		Editor.SelectBody(Node.Index);
		break;
	case ENodeKind::Constraint:
		Editor.SelectConstraint(Node.Index);
		break;
	case ENodeKind::Bone:
		Editor.SelectBone(Node.Index);   // 바디가 있는 본이면 OnBoneSelected가 그 바디를 고른다
		break;
	}
}

void FPhysicsAssetSkeletonTreePanel::DrawNodeContextMenu(const FTreeNode& Node)
{
	if (!ImGui::BeginPopupContextItem("##NodeContext"))
		return;

	// 여러 개를 고른 채 그중 하나를 우클릭하면 모두 지우는 항목을 먼저 둔다
	const int32 SelectedCount = Editor.GetSelections().Num();
	if (SelectedCount > 1 && IsNodeSelected(Node))
	{
		char Label[64];
		snprintf(Label, sizeof(Label), "Delete Selected (%d)", SelectedCount);
		if (ImGui::MenuItem(Label))
		{
			Editor.DeleteSelection();
			ImGui::EndPopup();
			return;
		}
		ImGui::Separator();
	}

	if (Node.Kind == ENodeKind::Constraint)
	{
		// 프리셋은 고른 조인트 모두에 들어간다 (우클릭한 행은 이미 선택에 들어 있다)
		if (ImGui::BeginMenu("Constraint Type"))
		{
			if (ImGui::MenuItem("Ball and Socket"))
				Editor.ApplyConstraintPreset(FPhysicsAssetEditor::EConstraintPreset::BallAndSocket);
			if (ImGui::MenuItem("Hinge"))
				Editor.ApplyConstraintPreset(FPhysicsAssetEditor::EConstraintPreset::Hinge);
			if (ImGui::MenuItem("Skeletal"))
				Editor.ApplyConstraintPreset(FPhysicsAssetEditor::EConstraintPreset::Skeletal);
			ImGui::EndMenu();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Constraint"))
			Editor.DeleteConstraint(Node.Index);
		ImGui::EndPopup();
		return;
	}

	// 본·바디 행: 그 본에 형상을 더한다. 바디가 없으면 새 바디와 부모 바디와의 조인트가 생긴다
	if (ImGui::MenuItem("Add Capsule"))
		Editor.AddPrimitive(Node.BoneIndex, EPhysicsAssetPrimitive::Capsule);
	if (ImGui::MenuItem("Add Box"))
		Editor.AddPrimitive(Node.BoneIndex, EPhysicsAssetPrimitive::Box);
	if (ImGui::MenuItem("Add Sphere"))
		Editor.AddPrimitive(Node.BoneIndex, EPhysicsAssetPrimitive::Sphere);

	const int32 BodyIndex = Editor.FindBodyIndexForBone(Node.BoneIndex);
	if (BodyIndex >= 0)
	{
		ImGui::Separator();
		if (ImGui::MenuItem("Delete Body"))
			Editor.DeleteBody(BodyIndex);
		if (ImGui::MenuItem("Delete All Bodies Below"))
			Editor.DeleteBodiesBelow(BodyIndex);

		// 충돌·재생성은 고른 바디 모두에 들어간다
		ImGui::Separator();
		if (ImGui::BeginMenu("Collision"))
		{
			const bool bMultiple = Editor.GetSelectedBodyCount() >= 2;
			if (ImGui::MenuItem("Enable Collision", nullptr, false, bMultiple))
				Editor.SetCollisionBetweenSelected(true);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Enable collision between the selected bodies (select two or more)");
			if (ImGui::MenuItem("Enable Collision All"))
				Editor.SetCollisionBetweenSelectedAndAll(true);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Enable collision between the selected bodies and every other body");
			if (ImGui::MenuItem("Disable Collision", nullptr, false, bMultiple))
				Editor.SetCollisionBetweenSelected(false);
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Disable collision between the selected bodies (select two or more)");
			if (ImGui::MenuItem("Disable Collision All"))
				Editor.SetCollisionBetweenSelectedAndAll(false);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Disable collision between the selected bodies and every other body");
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Regenerate Bodies"))
			Editor.RegenerateSelectedBodies();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Re-fit the shapes of the selected bodies from the mesh.\nMass, constraints and collision settings are kept.");
	}
	ImGui::EndPopup();
}

void FPhysicsAssetSkeletonTreePanel::DrawNode(int32 NodeIndex, bool& bScrollToSelection)
{
	// 메뉴로 바디를 지우면 Nodes가 다음 프레임에 다시 만들어지므로 복사본으로 그린다
	const FTreeNode Node = Nodes[NodeIndex];
	if (!Node.bVisible)
		return;
	DrawOrder.Add({ Node.Kind, Node.Index });

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::PushID(static_cast<int32>(Node.Kind) * 100000 + Node.Index);

	const bool bSelected = IsNodeSelected(Node);
	const bool bRowHovered = (ImGui::GetHoveredID() == ImGui::GetID("Node"));
	if (bSelected)
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, EditorColor::WithAlpha(EditorColor::Accent, 210));
	else if (bRowHovered)
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, EditorColor::WithAlpha(EditorColor::BgAccentHover, 210));

	bool bHasVisibleChild = false;
	for (int32 Child : Node.Children)
		bHasVisibleChild |= Nodes[Child].bVisible;

	ImGuiTreeNodeFlags Flags =
		ImGuiTreeNodeFlags_DefaultOpen |
		ImGuiTreeNodeFlags_OpenOnArrow |
		ImGuiTreeNodeFlags_OpenOnDoubleClick |
		ImGuiTreeNodeFlags_SpanAvailWidth |
		ImGuiTreeNodeFlags_DrawLinesToNodes;
	if (!bHasVisibleChild)
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (bSelected)
		Flags |= ImGuiTreeNodeFlags_Selected;
	if (SearchBuffer[0] && bHasVisibleChild)
		ImGui::SetNextItemOpen(true);   // 검색 중에는 찾은 자손이 보이게 펼친다

	// 아이콘과 이름은 직접 그린다 (폰트에 따라 겹치지 않게)
	const bool bOpen = ImGui::TreeNodeEx("Node", Flags, "");

	ImDrawList* DL = ImGui::GetWindowDrawList();
	const ImVec2 ItemMin = ImGui::GetItemRectMin();
	const ImVec2 ItemMax = ImGui::GetItemRectMax();
	const float LabelAreaStartX = ItemMin.x + ImGui::GetTreeNodeToLabelSpacing();
	const float IconCenterX = bHasVisibleChild
		? (LabelAreaStartX + EditorStyle::Scale(6.0f))
		: (ItemMin.x + ImGui::GetStyle().FramePadding.x + EditorStyle::Scale(6.0f));
	const ImVec2 IconCenter(IconCenterX, (ItemMin.y + ItemMax.y) * 0.5f);
	const ImU32 IconColor = bSelected ? EditorColor::Ink : EditorColor::WithAlpha(EditorColor::TextMuted, 230);
	const float IconSize = EditorStyle::Scale(11.0f);

	switch (Node.Kind)
	{
	case ENodeKind::Bone:       DrawBoneIcon(DL, IconCenter, EditorStyle::Scale(8.0f), IconColor); break;
	case ENodeKind::Body:       DrawBodyIcon(DL, IconCenter, IconSize, IconColor); break;
	case ENodeKind::Constraint: DrawConstraintIcon(DL, IconCenter, IconSize, IconColor); break;
	}

	const float TextX = IconCenterX + EditorStyle::Scale(11.0f);
	const float TextY = (ItemMin.y + ItemMax.y - ImGui::GetTextLineHeight()) * 0.5f;
	const ImVec2 ClipMax(std::max(TextX + 1.0f, ItemMax.x - EditorStyle::Scale(4.0f)), ItemMax.y);
	DL->PushClipRect(ImVec2(TextX, ItemMin.y), ClipMax, true);
	DL->AddText(ImVec2(TextX, TextY), ImGui::GetColorU32(ImGuiCol_Text), Node.Label.c_str());
	DL->PopClipRect();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		SelectNode(Node);
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !bSelected)
		SelectNode(Node);   // 메뉴가 가리키는 대상을 선택으로도 보여 준다. 이미 고른 행이면 다중 선택을 그대로 둔다

	DrawNodeContextMenu(Node);

	if (bScrollToSelection && bSelected)
	{
		ImGui::SetScrollHereY(0.5f);
		bScrollToSelection = false;
	}

	if (bOpen && bHasVisibleChild)
	{
		for (int32 Child : Node.Children)
			DrawNode(Child, bScrollToSelection);
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FPhysicsAssetSkeletonTreePanel::DrawOptionsMenu()
{
	if (!ImGui::BeginPopup("##TreeOptions"))
		return;

	ImGui::TextDisabled("Show");
	ImGui::MenuItem("Bones", nullptr, &bShowBones);
	ImGui::MenuItem("Bodies", nullptr, &bShowBodies);
	ImGui::MenuItem("Constraints", nullptr, &bShowConstraints);
	ImGui::EndPopup();
}

void FPhysicsAssetSkeletonTreePanel::DrawContent()
{
	if (!Editor.GetMesh() || !Editor.GetPhysicsAsset())
		return;

	// 검색창 + 오른쪽 표시 옵션 버튼
	const char* OptionsLabel = "Options";
	const float ButtonWidth = ImGui::CalcTextSize(OptionsLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - ButtonWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f));
	ImGui::InputTextWithHint("##PhysicsTreeSearch", "Search...", SearchBuffer, sizeof(SearchBuffer));
	ImGui::SameLine();
	if (ImGui::Button(OptionsLabel))
		ImGui::OpenPopup("##TreeOptions");
	DrawOptionsMenu();

	BuildTree();

	const FPhysicsAssetSelection& Selection = Editor.GetSelection();
	bool bScrollToSelection = false;
	if (LastSelectionType != static_cast<int32>(Selection.Type) || LastSelectionIndex != Selection.Index)
	{
		LastSelectionType = static_cast<int32>(Selection.Type);
		LastSelectionIndex = Selection.Index;
		bScrollToSelection = Selection.Type != EPhysicsAssetSelectionType::None;
	}

	ImGui::PushStyleColor(ImGuiCol_TableRowBg, EditorColor::BgElevated);
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, EditorColor::BgElevatedAlt);
	ImGui::PushStyleColor(ImGuiCol_TreeLines, EditorColor::TextDisabled);
	// 행 배경은 TableSetBgColor가 칠하므로 TreeNode 자체의 Header 사각형은 투명하게 둔다
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##PhysicsTreeTable", 1, TableFlags))
	{
		// IndentSpacing이 FontSize*0.5 + ItemInnerSpacing.x보다 작으면 ImGui가 트리 가로선을 생략한다
		const float MinIndent = ImGui::GetFontSize() * 0.5f + ImGui::GetStyle().ItemInnerSpacing.x + EditorStyle::Scale(7.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, std::max(EditorStyle::Scale(20.0f), MinIndent));

		DrawOrder.Empty();
		for (int32 Root : RootNodes)
			DrawNode(Root, bScrollToSelection);
		LastDrawOrder = DrawOrder;

		ImGui::PopStyleVar();

		// 빈 곳을 누르면 선택을 푼다
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
			Editor.ClearSelection();

		ImGui::EndTable();
	}
	ImGui::PopStyleColor(6);

	// Delete 키로 선택한 바디·조인트를 지운다
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		Editor.DeleteSelection();
	}
}
