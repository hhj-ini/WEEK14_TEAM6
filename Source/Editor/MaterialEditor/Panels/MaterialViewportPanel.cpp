#include "EnginePCH.h"
#include "MaterialViewportPanel.h"
#include "Editor/MaterialEditor/MaterialEditor.h"
#include "Editor/EditorUI/EditorStyle.h"

FMaterialViewportPanel::FMaterialViewportPanel(FMaterialEditor& InEditor)
	: FMaterialEditorPanel(InEditor, "Preview")
{
}

void FMaterialViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	if (!Client)
	{
		return;
	}

	// 상단 툴바: 저장 / 프리뷰 도형 전환 / 뷰포트 모드 전환
	{
		const bool bDirty = Editor.IsDirty();
		if (bDirty)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Accent);
		}
		if (ImGui::Button(bDirty ? "Save*" : "Save"))
		{
			Editor.Save();
		}
		if (bDirty)
		{
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		auto DrawShapeButton = [this](const char* Label, EMaterialPreviewShape Shape)
		{
			const bool bSelected = Editor.GetPreviewShape() == Shape;
			if (bSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::Accent);
			}
			if (ImGui::Button(Label))
			{
				Editor.SetPreviewShape(Shape);
			}
			if (bSelected)
			{
				ImGui::PopStyleColor();
			}
		};

		DrawShapeButton("Sphere", EMaterialPreviewShape::Sphere);
		ImGui::SameLine();
		DrawShapeButton("Cube", EMaterialPreviewShape::Cube);
		ImGui::SameLine();
		DrawShapeButton("Plane", EMaterialPreviewShape::Plane);

		ImGui::SameLine();
		ImGui::TextDisabled("|");
		ImGui::SameLine();

		const EViewportMode CurrentMode = Client->GetViewportMode();
		const char* ModeLabel = "Lit";
		if (CurrentMode == EViewportMode::Unlit) ModeLabel = "Unlit";
		else if (CurrentMode == EViewportMode::Wireframe) ModeLabel = "Wireframe";
		else if (CurrentMode == EViewportMode::WorldNormal) ModeLabel = "WorldNormal";

		ImGui::SetNextItemWidth(EditorStyle::Scale(110.0f));
		if (ImGui::BeginCombo("##PreviewViewMode", ModeLabel))
		{
			if (ImGui::Selectable("Lit", CurrentMode == EViewportMode::Solid))
				Client->SetViewportMode(EViewportMode::Solid);
			if (ImGui::Selectable("Unlit", CurrentMode == EViewportMode::Unlit))
				Client->SetViewportMode(EViewportMode::Unlit);
			if (ImGui::Selectable("Wireframe", CurrentMode == EViewportMode::Wireframe))
				Client->SetViewportMode(EViewportMode::Wireframe);
			if (ImGui::Selectable("WorldNormal", CurrentMode == EViewportMode::WorldNormal))
				Client->SetViewportMode(EViewportMode::WorldNormal);
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Focus (F)"))
		{
			Editor.FocusPreviewMesh();
		}
	}

	ImGui::Separator();

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##MaterialPreviewViewport", Size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	if (FTexture2D* Target = Client->GetColorTarget(); Target && Target->GetSRV())
	{
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin,
			ImVec2(Origin.x + Size.x, Origin.y + Size.y));
	}

	const bool bHovered = ImGui::IsItemHovered();
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
		ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
	{
		ImGui::SetWindowFocus();
	}

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		Editor.FocusPreviewMesh();
	}

	const ImGuiIO& IO = ImGui::GetIO();
	FViewportCameraInput Input
	{
		.bCaptured   = bFocused && ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Left)),
		.bAllowWheel = bHovered,
		.bForward    = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward   = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight      = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft       = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp         = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown       = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX      = IO.MouseDelta.x,
		.MouseY      = IO.MouseDelta.y,
		.Wheel       = IO.MouseWheel * 120.0f,
	};

	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), Input);
}
