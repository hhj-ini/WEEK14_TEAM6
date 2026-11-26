#include "EnginePCH.h"
#include "SkeletalMeshViewportPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Editor/Viewports/ViewportToolbarWidgets.h"
#include "Render/SkeletalMesh.h"
#include "Clothing/ClothingAsset.h"
#include <cstdio>
#include <filesystem>

FSkeletalMeshViewportPanel::FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Viewport")
{
}

void FSkeletalMeshViewportPanel::DrawSectionContextMenu()
{
	if (!ImGui::BeginPopup("SkeletalMeshSectionContextMenu"))
	{
		return;
	}

	USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh || ContextMenuSectionIndex < 0)
	{
		ImGui::EndPopup();
		return;
	}

	ImGui::TextDisabled("ASSET");
	ImGui::Separator();
	ImGui::Text("  LOD0 - Section %d", ContextMenuSectionIndex);
	ImGui::Spacing();
	ImGui::TextDisabled("CLOTHING");
	ImGui::Separator();

	const TArray<UClothingAssetCommon*>& Assets = Mesh->GetClothingAssets();
	UClothingAssetCommon* BoundAsset = Mesh->GetClothingAssetForSection(ContextMenuSectionIndex);

	if (ImGui::BeginMenu("Apply Clothing Data...", !Assets.IsEmpty()))
	{
		ImGui::TextDisabled("Available Assets");
		ImGui::Separator();
		for (int32 i = 0; i < Assets.Num(); ++i)
		{
			UClothingAssetCommon* Asset = Assets[i];
			if (!Asset)
				continue;

			const FString Label = Asset->GetAssetName() + " - LOD0";
			const bool bSelected = (Asset == BoundAsset);
			if (ImGui::MenuItem(Label.c_str(), nullptr, bSelected))
			{
				Editor.ApplyClothingToSection(ContextMenuSectionIndex, Asset);
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Remove Clothing Data", nullptr, false, BoundAsset != nullptr))
	{
		Editor.RemoveClothingFromSection(ContextMenuSectionIndex);
	}

	if (ImGui::BeginMenu("Create Clothing Data from Section"))
	{
		ImGui::TextDisabled("Asset Name");
		ImGui::SetNextItemWidth(190.0f);
		ImGui::InputText("##ContextClothName", CreateClothNameBuf, sizeof(CreateClothNameBuf));

		if (ImGui::Button("Create"))
		{
			Editor.CreateClothingFromSection(ContextMenuSectionIndex, FString(CreateClothNameBuf));
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Create & Apply"))
		{
			if (UClothingAssetCommon* Created = Editor.CreateClothingFromSection(ContextMenuSectionIndex, FString(CreateClothNameBuf)))
			{
				Editor.ApplyClothingToSection(ContextMenuSectionIndex, Created);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}

	ImGui::EndPopup();
}

void FSkeletalMeshViewportPanel::DrawContent()
{
	DrawPreviewViewToolbar(Editor, ToolbarState, [&]()
	{
		const bool bPaintOn = Editor.IsClothPaintActive();
		if (bPaintOn)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.45f, 0.85f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.54f, 0.94f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.36f, 0.72f, 1.0f));
		}
		if (ImGui::Button(bPaintOn ? "Deactivate Cloth Paint" : "Activate Cloth Paint"))
		{
			Editor.SetClothPaintActive(!bPaintOn);
		}
		if (bPaintOn)
		{
			ImGui::PopStyleColor(3);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Toggle Unreal-style Cloth Paint mode (pauses simulation while painting; resumes on deactivate).");
		}

		if (Editor.IsClothingDirty())
		{
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.25f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.64f, 0.32f, 1.0f));
			if (ImGui::Button("Save Cloth*"))
			{
				Editor.SaveClothingAssets();
			}
			ImGui::PopStyleColor(2);
		}
	});

	// 이동/회전/크기 세 가지 (EGizmoMode 순서)
	constexpr int32 GizmoModeCount = 3;

	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
	{
		return;
	}

	const ImVec2 Origin = ImGui::GetCursorScreenPos();

	// 이미지 영역을 입력 항목으로 등록하여 드래그 중에도 카메라 조작을 유지합니다.
	ImGui::InvisibleButton("##SkeletalMeshViewport", Size,
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

	const ImGuiIO& IO = ImGui::GetIO();
	const FVector2 MouseUV((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y);

	// 우클릭(드래그 없는 클릭) 시 메시 섹션 피킹 -> 클로딩 컨텍스트 메뉴 표시 (언리얼 Image 2, 3)
	if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		bRightClickTracking = true;
		RightClickStartPos = IO.MousePos;
	}
	if (bRightClickTracking && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		bRightClickTracking = false;
		const float Dx = IO.MousePos.x - RightClickStartPos.x;
		const float Dy = IO.MousePos.y - RightClickStartPos.y;
		if (bHovered && (Dx * Dx + Dy * Dy) < 16.0f)
		{
			int32 HitSection = -1;
			if (Editor.PickMeshSection(MouseUV, HitSection))
			{
				ContextMenuSectionIndex = HitSection;
				FString MeshStem = std::filesystem::path(Editor.GetAssetPath()).stem().string();
				if (MeshStem.size() > 4 && MeshStem.compare(MeshStem.size() - 4, 4, ".fbx") == 0)
				{
					MeshStem = MeshStem.substr(0, MeshStem.size() - 4);
				}
				const int32 ExistingCount = Editor.GetMesh() ? static_cast<int32>(Editor.GetMesh()->GetClothingAssets().Num()) : 0;
				std::snprintf(CreateClothNameBuf, sizeof(CreateClothNameBuf), "%s_Clothing_%d", MeshStem.c_str(), ExistingCount);
				ImGui::OpenPopup("SkeletalMeshSectionContextMenu");
			}
		}
	}

	DrawSectionContextMenu();
	const bool bContextMenuOpen = ImGui::IsPopupOpen("SkeletalMeshSectionContextMenu");

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered && !bContextMenuOpen && ImGui::IsKeyPressed(ImGuiKey_F, false))
	{
		Editor.FocusMesh();
	}

	// 브러시 반경 단축키 ([ / ])
	if (Editor.IsClothPaintActive() && bFocused && !IO.WantTextInput && !bContextMenuOpen)
	{
		FClothPaintSettings& PaintSettings = Editor.GetClothPaintSettings();
		if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true))
		{
			PaintSettings.BrushRadius = FMath::Clamp(PaintSettings.BrushRadius * 0.85f, 0.05f, 200.0f);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true))
		{
			PaintSettings.BrushRadius = FMath::Clamp(PaintSettings.BrushRadius * 1.18f, 0.05f, 200.0f);
		}
	}

	// 분리된 플랫폼 창의 입력도 받을 수 있도록 ImGui 입력을 사용합니다.
	//
	// TODO: 여기서도 FInputSystem을 사용할 수 있도록, 구조 개선하기
	// ImGui가 생성하는 HWND에 직접 WndProc을 연결하고 FInputSystem에 전달시켜야함

	const bool bMouseDown = !bContextMenuOpen && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = !bContextMenuOpen && ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const FViewportPointerInput PointerInput
	{
		.MouseUV = MouseUV,
		.bHovered = bHovered && !bContextMenuOpen,
		.bFocused = bFocused && !IO.AppFocusLost && !bContextMenuOpen,
		.bMousePressed = bHovered && !bCameraDragging && !bContextMenuOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
		.bShiftDown = IO.KeyShift,
	};

	FViewportCameraInput Input
	{
		.bCaptured		= bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsGizmoDragging(),
		.bAllowWheel	= bHovered && !bMouseDown && !bContextMenuOpen && !Editor.IsGizmoDragging(),
		.bForward		= ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward		= ImGui::IsKeyDown(ImGuiKey_S),
		.bRight			= ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft			= ImGui::IsKeyDown(ImGuiKey_A),
		.bUp			= ImGui::IsKeyDown(ImGuiKey_E),
		.bDown			= ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX			= IO.MouseDelta.x,
		.MouseY			= IO.MouseDelta.y,

		// ImGui의 휠 단위를 기존 카메라 이동 함수가 사용하는 Windows 휠 단위로 변환합니다.
		.Wheel			= IO.MouseWheel * 120.0f,
	};

	// 키보드로 Gizmo 키 변경 (Cloth Paint 모드에서는 기즈모 단축키 비활성화)
	if (!bCameraDragging && !bContextMenuOpen && !Editor.IsClothPaintActive())
	{
		if (ImGui::IsKeyDown(ImGuiKey_W))
		{
			Editor.SetGizmoMode(EGizmoMode::Location);
		}
		if (ImGui::IsKeyDown(ImGuiKey_E))
		{
			Editor.SetGizmoMode(EGizmoMode::Rotation);
		}
		if (ImGui::IsKeyDown(ImGuiKey_R))
		{
			Editor.SetGizmoMode(EGizmoMode::Scale);
		}

		// 뷰포트에서 Space를 누를 때마다 이동, 회전, 크기 모드를 순환합니다.
		if (bFocused && bHovered && !IO.WantTextInput && !IO.AppFocusLost &&
			!Editor.IsGizmoDragging() && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		{
			const int32 NextMode = (static_cast<int32>(Editor.GetGizmoMode()) + 1) % GizmoModeCount;
			Editor.SetGizmoMode(static_cast<EGizmoMode>(NextMode));
		}
	}

	// 렌더 타깃 변경은 Editor의 다음 Render에서 수행합니다.
	uint32 Width = static_cast<uint32>(Size.x);
	uint32 Height = static_cast<uint32>(Size.y);
	Editor.SetViewportState(Width, Height, Input, PointerInput);

	// 레벨 뷰포트처럼 왼쪽 위에 기즈모 모드 버튼을 겹쳐 그린다. 이미지 위에 겹친 자식 창이라 뷰포트 입력과 섞이지 않는다.
	if (!Editor.IsClothPaintActive())
	{
		const int Clicked = ViewportToolbarWidgets::TransformModeOverlay("##SkeletalGizmoToolbar", Origin,
			static_cast<int>(Editor.GetGizmoMode()));
		if (Clicked >= 0)
		{
			Editor.SetGizmoMode(static_cast<EGizmoMode>(Clicked));
		}
	}
}
