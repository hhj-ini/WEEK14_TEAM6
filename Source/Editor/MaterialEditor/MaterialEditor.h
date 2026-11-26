#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Camera/ViewportCameraMovement.h"

class UMaterial;
class UStaticMesh;
class UStaticMeshComponent;

enum class EMaterialPreviewShape : uint8
{
	Sphere,
	Cube,
	Plane
};

class FMaterialEditor : public FAssetEditor
{
public:
	FMaterialEditor(
		class UEditorEngine& InEditorEngine,
		class FEditorManager& InEditorManager,
		const FString& InAssetPath,
		UMaterial& InMaterial
	);

	void Tick(float DeltaTime) override;
	void OnRenderPreview() override;

	const char* GetEditorTypeName() const override { return "Material"; }
	bool UsesPreviewSkybox() const override { return true; }
	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;

	void FocusPreviewMesh();
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);

	UMaterial* GetMaterial() const { return Material; }
	EMaterialPreviewShape GetPreviewShape() const { return PreviewShape; }
	void SetPreviewShape(EMaterialPreviewShape Shape);

	void MarkDirty();
	bool Save();
	void CloseWithoutPrompt();
	bool ConsumeUnsavedPromptRequest();

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	UStaticMesh* ResolvePreviewMesh(EMaterialPreviewShape Shape) const;

	UMaterial* Material = nullptr;
	UStaticMeshComponent* MeshComponent = nullptr;
	EMaterialPreviewShape PreviewShape = EMaterialPreviewShape::Sphere;

	FViewportCameraInput CameraInput;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;

	bool bDirty = false;
	bool bCloseConfirmed = false;
	bool bUnsavedPromptRequested = false;
};
