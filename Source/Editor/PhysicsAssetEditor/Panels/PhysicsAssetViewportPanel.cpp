#include "EnginePCH.h"
#include "PhysicsAssetViewportPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"

FPhysicsAssetViewportPanel::FPhysicsAssetViewportPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Viewport")
{
}

void FPhysicsAssetViewportPanel::DrawSimulateButton()
{
	using namespace ViewportToolbarWidgets;

	const bool bSimulating = Editor.IsSimulating();
	const char* Label = bSimulating ? "Stop" : "Simulate";
	const float Height = ImGui::GetFrameHeight();
	const float Pad = ImGui::GetStyle().FramePadding.x;
	const float Gap = EditorStyle::Scale(5.0f);
	const float Width = Pad * 2.0f + IconSize() + Gap + ImGui::CalcTextSize(Label).x;

	// [아이콘][라벨] 버튼. 아이콘은 원래 색(빨간 재생 삼각형) 그대로 그린다
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	EditorStyle::PushSelectedButton(bSimulating);
	if (ImGui::Button("##Simulate", { Width, Height }))
		Editor.ToggleSimulation();
	EditorStyle::PopSelectedButton(bSimulating);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", bSimulating ? "Stop and return to the reference pose"
			: (Editor.IsSelectedSimulation() ? "Simulate the selected body and its children (Alt+Shift+I)" : "Simulate the whole ragdoll (Alt+I)"));
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const float CenterY = Pos.y + Height * 0.5f;
	float X = Pos.x + Pad;
	if (const ImTextureID Texture = GetIcon("PhysicsSimulate"); Texture != ImTextureID_Invalid)
		DrawList->AddImage(Texture, { X, CenterY - IconSize() * 0.5f }, { X + IconSize(), CenterY + IconSize() * 0.5f });
	X += IconSize() + Gap;
	DrawList->AddText({ X, CenterY - ImGui::GetTextLineHeight() * 0.5f }, TextColor(), Label);

	// ⋮ 옵션 버튼
	ImGui::SameLine(0.0f, EditorStyle::Scale(1.0f));
	const ImVec2 DotsPos = ImGui::GetCursorScreenPos();
	const float DotsWidth = EditorStyle::Scale(14.0f);
	if (ImGui::Button("##SimulateOptions", { DotsWidth, Height }))
		ImGui::OpenPopup("##SimulateOptionsPopup");
	for (int32 i = -1; i <= 1; ++i)
	{
		DrawList->AddCircleFilled({ DotsPos.x + DotsWidth * 0.5f, CenterY + i * EditorStyle::Scale(4.0f) },
			EditorStyle::Scale(1.3f), TextColor());
	}

	if (ImGui::BeginPopup("##SimulateOptionsPopup"))
	{
		// 항목 오른쪽 끝에 단축키를 회색으로 적는다
		const auto Shortcut = [](const char* Text)
		{
			const ImVec2 Max = ImGui::GetItemRectMax();
			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Size = ImGui::CalcTextSize(Text);
			ImGui::GetWindowDrawList()->AddText({ Max.x - Size.x - EditorStyle::Scale(8.0f), (Min.y + Max.y - Size.y) * 0.5f },
				ImGui::GetColorU32(ImGuiCol_TextDisabled), Text);
		};

		ImGui::Dummy({ EditorStyle::Scale(260.0f), 0.0f });
		SectionHeader("Simulation");
		const bool bSelected = Editor.IsSelectedSimulation();
		if (IconMenuItem("PhysicsSimulate", "Simulate", !bSelected, EIndicator::Radio))
			Editor.RunSimulation(false);
		Shortcut("Alt+I");
		if (IconMenuItem("PhysicsSimulate", "Simulate Selected", bSelected, EIndicator::Radio))
			Editor.RunSimulation(true);
		Shortcut("Alt+Shift+I");

		SectionHeader("Simulation Options");
		if (IconMenuItem(nullptr, "No Gravity", Editor.bNoGravitySimulation, EIndicator::Check))
			Editor.bNoGravitySimulation = !Editor.bNoGravitySimulation;
		if (IconMenuItem(nullptr, "Enable Floor Collision", Editor.IsFloorCollisionEnabled(), EIndicator::Check))
			Editor.SetFloorCollisionEnabled(!Editor.IsFloorCollisionEnabled());
		ImGui::EndPopup();
	}
}

void FPhysicsAssetViewportPanel::DrawLeftToolbar()
{
	const bool bSimulating = Editor.IsSimulating();
	DrawSimulateButton();
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();

	ImGui::BeginDisabled(bSimulating);
	struct FModeButton { const char* Label; EGizmoMode Mode; const char* Tooltip; };
	const FModeButton Modes[] = {
		{ "Move", EGizmoMode::Location, "Move (W)" },
		{ "Rotate", EGizmoMode::Rotation, "Rotate (E)" },
		{ "Scale", EGizmoMode::Scale, "Scale (R)" },
	};

	for (int32 i = 0; i < IM_ARRAYSIZE(Modes); ++i)
	{
		const FModeButton& Button = Modes[i];
		if (i > 0)
			ImGui::SameLine();
		const bool bSelected = Editor.GetGizmoMode() == Button.Mode;
		EditorStyle::PushSelectedButton(bSelected);
		if (ImGui::SmallButton(Button.Label))
			Editor.SetGizmoMode(Button.Mode);
		EditorStyle::PopSelectedButton(bSelected);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Button.Tooltip);
	}
	ImGui::EndDisabled();
}

namespace
{
	using namespace ViewportToolbarWidgets;

	// 다른 표시 메뉴 항목과 글자 줄이 맞게 앞을 비운 하위 메뉴.
	// 아래 Scene Elements의 "Mesh"와 ID가 겹치지 않게 "##Physics"를 붙인다
	bool BeginPaddedMenu(const char* Label)
	{
		return ImGui::BeginMenu((FString("      ") + Label + "##Physics").c_str());
	}

	template <typename EnumT>
	void RadioItems(EnumT& Value, const char* const* Names, int32 Count)
	{
		// Edit·Simulation 구역에 같은 라벨(Solid, None 등)이 있으므로 값마다 ID를 나눈다
		ImGui::PushID(&Value);
		for (int32 i = 0; i < Count; ++i)
		{
			if (IconMenuItem(nullptr, Names[i], static_cast<int32>(Value) == i, EIndicator::Radio))
				Value = static_cast<EnumT>(i);
		}
		ImGui::PopID();
	}

	void CheckItem(const char* Label, bool& bValue)
	{
		if (IconMenuItem(nullptr, Label, bValue, EIndicator::Check))
			bValue = !bValue;
	}

	// [라벨] [값 칸] 한 줄
	bool FloatItem(const char* Label, const char* Id, float& Value, float Speed, float Min, float Max)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + EditorStyle::Scale(22.0f));
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(EditorStyle::Scale(170.0f));
		ImGui::SetNextItemWidth(EditorStyle::Scale(110.0f));
		return ImGui::DragFloat(Id, &Value, Speed, Min, Max, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	}
}

void FPhysicsAssetViewportPanel::DrawPhysicsRenderingMenu()
{
	FPhysicsAssetRenderSettings& S = Editor.RenderSettings;
	SectionHeader("Physics Rendering");

	if (BeginPaddedMenu("Mass Properties"))
	{
		static const char* CoMNames[] = { "All", "Selected", "None" };
		SectionHeader("Center of Mass Drawing");
		CheckItem("Mass Values", S.bShowMassValue);
		CheckItem("Hide Kinematic Bodies", S.bHideKinematicCoM);
		FloatItem("Marker Scale", "##CoMScale", S.CoMMarkerScale, 0.05f, 0.1f, 10.0f);
		SectionHeader("Center of Mass Drawing (Edit)");
		RadioItems(S.CoMEdit, CoMNames, 3);
		SectionHeader("Center of Mass Drawing (Simulation)");
		RadioItems(S.CoMSimulation, CoMNames, 3);
		ImGui::EndMenu();
	}

	if (BeginPaddedMenu("Mesh"))
	{
		static const char* MeshNames[] = { "Solid", "Wireframe", "None" };
		const FPhysicsAssetRenderSettings Before = S;
		SectionHeader("Mesh Drawing (Edit)");
		RadioItems(S.MeshEdit, MeshNames, 3);
		SectionHeader("Mesh Drawing (Simulation)");
		RadioItems(S.MeshSimulation, MeshNames, 3);
		if (Before.MeshEdit != S.MeshEdit || Before.MeshSimulation != S.MeshSimulation)
			Editor.ApplyMeshDrawMode();
		ImGui::EndMenu();
	}

	if (BeginPaddedMenu("Bodies"))
	{
		static const char* BodyNames[] = { "Solid", "Wireframe", "Solid + Wireframe", "None" };
		SectionHeader("Body Drawing");
		CheckItem("Selected Solid Only", S.bSelectedSolidOnly);
		CheckItem("Hide Simulated Bodies", S.bHideSimulatedBodies);
		CheckItem("Hide Kinematic Bodies", S.bHideKinematicBodies);
		FloatItem("Collision Opacity", "##CollisionOpacity", S.CollisionOpacity, 0.01f, 0.0f, 1.0f);
		SectionHeader("Body Drawing (Edit)");
		RadioItems(S.BodyEdit, BodyNames, 4);
		SectionHeader("Body Drawing (Simulation)");
		RadioItems(S.BodySimulation, BodyNames, 4);
		ImGui::EndMenu();
	}

	if (BeginPaddedMenu("Constraints"))
	{
		static const char* ConstraintNames[] = { "None", "All Positions", "All Limits" };
		SectionHeader("Constraints");
		CheckItem("Draw Constraints as Points", S.bConstraintsAsPoints);
		CheckItem("Selected Constraints Only", S.bSelectedConstraintsOnly);
		FloatItem("Constraint Scale", "##ConstraintScale", S.ConstraintScale, 0.05f, 0.1f, 10.0f);
		SectionHeader("Constraint Drawing (Edit)");
		RadioItems(S.ConstraintEdit, ConstraintNames, 3);
		SectionHeader("Constraint Drawing (Simulation)");
		RadioItems(S.ConstraintSimulation, ConstraintNames, 3);
		ImGui::EndMenu();
	}
}

void FPhysicsAssetViewportPanel::DrawContent()
{
	DrawPreviewViewToolbar(Editor, ToolbarState, [this]() { DrawLeftToolbar(); }, [this]() { DrawPhysicsRenderingMenu(); });

	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
		return;

	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	// 이미지 영역을 입력 항목으로 등록해 드래그 중에도 카메라·기즈모 조작을 유지한다
	ImGui::InvisibleButton("##PhysicsViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (FTexture2D* Target = Client->GetColorTarget(); Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ImVec2(Origin.x + Size.x, Origin.y + Size.y));
	}
	Editor.DrawViewportLabels(ImGui::GetWindowDrawList(), Origin, Size);   // 질량 값
	Editor.DrawGrabHandle(ImGui::GetWindowDrawList(), Origin, Size);       // 잡기 표시

	const bool bHovered = ImGui::IsItemHovered();
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		ImGui::SetWindowFocus();

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);

	// 단축키: 카메라를 움직이는 중(오른쪽 버튼)에는 W/E가 카메라 이동이므로 받지 않는다
	if (bFocused && bHovered && !bCameraDragging && !IO.WantTextInput && !IO.KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_F, false)) Editor.FocusMesh();
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) Editor.SetGizmoMode(EGizmoMode::Location);
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) Editor.SetGizmoMode(EGizmoMode::Rotation);
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) Editor.SetGizmoMode(EGizmoMode::Scale);
		if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) Editor.DeleteSelection();
	}

	const FViewportPointerInput PointerInput
	{
		.MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
		.bHovered = bHovered,
		.bFocused = bFocused && !IO.AppFocusLost,
		.bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
		.bShiftDown = IO.KeyShift,
		.bAltDown = IO.KeyAlt,
		.bCtrlDown = IO.KeyCtrl,
		.bRightMousePressed = bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right),
		.bRightMouseDown = bCameraDragging,
	};

	// 시뮬레이션 중 Ctrl+우클릭은 잡기다. 잡는 동안(Ctrl을 떼도) 카메라는 돌리지 않는다
	const bool bGrabInput = Editor.IsGrabbing() || (Editor.IsSimulating() && IO.KeyCtrl);

	const FViewportCameraInput CameraInput
	{
		.bCaptured = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !bGrabInput,
		.bAllowWheel = bHovered && !bMouseDown,
		.bForward = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX = IO.MouseDelta.x,
		.MouseY = IO.MouseDelta.y,
		// ImGui의 휠 단위를 카메라 이동 함수가 쓰는 Windows 휠 단위로 바꾼다
		.Wheel = IO.MouseWheel * 120.0f,
	};

	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), CameraInput, PointerInput);
}
