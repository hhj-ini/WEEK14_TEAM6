#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Panels/PhysicsAssetSkeletonTreePanel.h"
#include "Panels/PhysicsAssetGraphPanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetViewportPanel.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, PhysicsEditor(InEditor)
{
	// 탭의 부모 창이 달라져도 애셋별 도킹 ID를 유지한다
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("PhysicsAssetDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("PhysicsAssetPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FPhysicsAssetViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetSkeletonTreePanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetGraphPanel>(InEditor));
	Panels.Add(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FPhysicsAssetEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S"))
		{
			PhysicsEditor.Save();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor"))
		{
			PhysicsEditor.Close();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Tools"))
	{
		const bool bCanEdit = !PhysicsEditor.IsSimulating();
		if (ImGui::MenuItem("Regenerate Selected Bodies", nullptr, false, bCanEdit && PhysicsEditor.GetSelectedBodyCount() > 0))
			PhysicsEditor.RegenerateSelectedBodies();
		if (ImGui::MenuItem("Regenerate All Bodies...", nullptr, false, bCanEdit))
			bOpenRegenerateConfirm = true;   // 메뉴 막대 안에서는 모달을 열 수 없어 다음에 연다
		ImGui::EndMenu();
	}

	EditorDock::DrawWindowMenu(Panels);
}

void FPhysicsAssetEditorTab::DrawRegenerateConfirm()
{
	if (bOpenRegenerateConfirm)
	{
		ImGui::OpenPopup("Regenerate All Bodies##PhysicsAsset");
		bOpenRegenerateConfirm = false;
	}

	if (ImGui::BeginPopupModal("Regenerate All Bodies##PhysicsAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Rebuild every body, constraint and collision pair from the mesh?");
		ImGui::TextDisabled("All edits to this physics asset will be replaced. It is saved only when you save.");
		ImGui::Spacing();
		if (ImGui::Button("Regenerate", ImVec2(120.0f, 0.0f)))
		{
			PhysicsEditor.RegenerateAllBodies();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID LeftId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.22f, nullptr, &ViewportId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.26f, nullptr, &ViewportId);
	const ImGuiID GraphId = ImGui::DockBuilderSplitNode(LeftId, ImGuiDir_Down, 0.4f, nullptr, &LeftId);

	ImGui::DockBuilderDockWindow(Panels[Viewport]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[SkeletonTree]->GetWindowName(), LeftId);
	ImGui::DockBuilderDockWindow(Panels[Graph]->GetWindowName(), GraphId);
	ImGui::DockBuilderDockWindow(Panels[Details]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FPhysicsAssetEditorTab::OnRender(bool bActive)
{
	PhysicsEditor.SetViewportState(0, 0, {});
	if (PhysicsEditor.IsCloseRequested())
	{
		return;
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			// 탭을 새로 열면 저장된 배치를 지우고 기본 배치로 시작한다
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		const ImGuiWindowClass WindowClass = EditorDock::MakeDockSpaceClass(PanelClassId);
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
		{
			ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
		}
	}

	for (auto& Panel : Panels)
	{
		// 분리된 패널은 부모 탭이 가려져도 따로 그린다
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}

	if (bActive)
		DrawRegenerateConfirm();

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bTabFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_DockHierarchy) ||
		HasFocusedSubWindow();
	if (bActive && bTabFocused && IO.KeyCtrl && !IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		PhysicsEditor.Save();
	}
	// Alt+I 전체 시뮬레이트, Alt+Shift+I 선택 시뮬레이트. 같은 키를 다시 누르면 멈춘다
	if (bActive && bTabFocused && IO.KeyAlt && !IO.KeyCtrl && !IO.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_I, false))
	{
		PhysicsEditor.RunSimulation(IO.KeyShift);
	}
}
