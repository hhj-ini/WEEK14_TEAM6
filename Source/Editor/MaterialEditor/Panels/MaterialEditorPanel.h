#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FMaterialEditor;

class FMaterialEditorPanel : public TAssetEditorPanel<FMaterialEditor>
{
public:
	FMaterialEditorPanel(FMaterialEditor& InEditor, const char* InName)
		: TAssetEditorPanel<FMaterialEditor>(InEditor, InName, "MaterialPanel")
	{
	}
};
