#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Containers/Array.h"
#include "imgui.h"

class FMaterialEditor;

class FMaterialEditorTab : public FAssetEditorTab
{
public:
	explicit FMaterialEditorTab(FMaterialEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);
	void DrawUnsavedPromptModal();

	FMaterialEditor& MaterialEditor;
	TArray<TUniquePtr<FAssetEditorPanelBase>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
