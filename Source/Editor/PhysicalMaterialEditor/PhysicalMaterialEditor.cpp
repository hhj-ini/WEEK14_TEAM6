#include "EnginePCH.h"
#include "Editor/PhysicalMaterialEditor/PhysicalMaterialEditor.h"
#include "Editor/Details/PropertyEditor.h"
#include "Asset/AssetUtils.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicalMaterialFile.h"
#include "imgui.h"

FPhysicalMaterialEditor::FPhysicalMaterialEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
	const FString& InAssetPath, UPhysicalMaterial& InMaterial)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Material(&InMaterial)
{
}

TUniquePtr<IEditorTab> FPhysicalMaterialEditor::CreateTab()
{
	return MakeUnique<FPhysicalMaterialEditorTab>(*this);
}

bool FPhysicalMaterialEditor::Save()
{
	if (!Material)
		return false;

	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	FString Error;
	if (!PhysicalMaterialFile::Write(std::filesystem::path(FilePath.empty() ? GetAssetPath() : FilePath), *Material, Error))
	{
		KRS_LOG(Error, "Failed to save physical material '{}': {}", GetAssetPath(), Error);
		return false;
	}

	bDirty = false;
	KRS_LOG(Info, "Saved physical material: {}", GetAssetPath());
	return true;
}

FPhysicalMaterialEditorTab::FPhysicalMaterialEditorTab(FPhysicalMaterialEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, PhysicalMaterialEditor(InEditor)
{
}

void FPhysicalMaterialEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S"))
		{
			PhysicalMaterialEditor.Save();
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Close Editor"))
		{
			PhysicalMaterialEditor.Close();
		}
		ImGui::EndMenu();
	}
}

void FPhysicalMaterialEditorTab::OnRender(bool bActive)
{
	if (PhysicalMaterialEditor.IsCloseRequested() || !bActive)
		return;

	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		PhysicalMaterialEditor.Save();
	}

	UPhysicalMaterial* Material = PhysicalMaterialEditor.GetMaterial();
	if (!Material)
		return;

	ImGui::BeginChild("##PhysicalMaterialProperties", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
	ImGui::TextDisabled("%s", PhysicalMaterialEditor.GetAssetPath().c_str());
	ImGui::Separator();
	// 값이 바뀌면 PostEditChangeProperty가 불려 PxMaterial에 바로 반영된다.
	if (FPropertyEditor::DrawObjectProperties(Material))
	{
		PhysicalMaterialEditor.MarkDirty();
	}
	ImGui::EndChild();
}
