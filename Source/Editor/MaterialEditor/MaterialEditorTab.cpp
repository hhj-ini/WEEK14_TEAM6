#include "EnginePCH.h"
#include "MaterialEditorTab.h"
#include "MaterialEditor.h"
#include "Panels/MaterialViewportPanel.h"
#include "Panels/MaterialDetailsPanel.h"
#include "imgui_internal.h"

FMaterialEditorTab::FMaterialEditorTab(FMaterialEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, MaterialEditor(InEditor)
{
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("MaterialDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("MaterialPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FMaterialViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FMaterialDetailsPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FMaterialEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S"))
		{
			MaterialEditor.Save();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor"))
		{
			MaterialEditor.Close();
		}
		ImGui::EndMenu();
	}

	EditorDock::DrawWindowMenu(Panels);
}

void FMaterialEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);

	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.36f, nullptr, &ViewportId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FMaterialEditorTab::DrawUnsavedPromptModal()
{
	const FString PopupId = "Unsaved Material Changes###MatUnsaved_" + MaterialEditor.GetAssetPath();
	if (MaterialEditor.ConsumeUnsavedPromptRequest())
	{
		ImGui::OpenPopup(PopupId.c_str());
	}

	if (ImGui::BeginPopupModal(PopupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Material '%s' has unsaved changes.", MaterialEditor.GetAssetPath().c_str());
		ImGui::TextUnformatted("Do you want to save before closing?");
		ImGui::Spacing();

		if (ImGui::Button("Save", ImVec2(90.0f, 0.0f)))
		{
			if (MaterialEditor.Save())
			{
				MaterialEditor.CloseWithoutPrompt();
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Don't Save", ImVec2(90.0f, 0.0f)))
		{
			MaterialEditor.CloseWithoutPrompt();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void FMaterialEditorTab::OnRender(bool bActive)
{
	MaterialEditor.SetViewportState(0, 0, {});
	if (MaterialEditor.IsCloseRequested())
	{
		return;
	}

	if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		MaterialEditor.Save();
	}

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f &&
			(!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
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
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
		{
			Panel->OnRender();
		}
	}

	DrawUnsavedPromptModal();
}
