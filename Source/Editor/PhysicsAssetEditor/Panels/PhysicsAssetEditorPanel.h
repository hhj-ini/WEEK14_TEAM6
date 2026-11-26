#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FPhysicsAssetEditor;

// PhysicsAsset 에디터 패널 베이스. 창 생성·도킹은 FAssetEditorPanelBase가 맡는다.
class FPhysicsAssetEditorPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	FPhysicsAssetEditorPanel(FPhysicsAssetEditor& InEditor, const char* InName);
};
