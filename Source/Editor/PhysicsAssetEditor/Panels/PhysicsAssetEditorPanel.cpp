#include "EnginePCH.h"
#include "PhysicsAssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

FPhysicsAssetEditorPanel::FPhysicsAssetEditorPanel(FPhysicsAssetEditor& InEditor, const char* InName)
	: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, InName, "PhysicsAssetPanel")
{
}
