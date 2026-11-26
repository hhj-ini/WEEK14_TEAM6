#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/AssetEditor/AssetEditorTab.h"

class UPhysicalMaterial;

// .pmat 편집기. 미리보기 없이 프로퍼티만 보여 준다 (UE의 Physical Material 에셋 창과 같은 구성).
// 값은 로드된 에셋 객체를 직접 고치므로, 이 머티리얼을 쓰는 바디에 바로 반영된다. 파일 저장은 Save로 한다.
class FPhysicalMaterialEditor : public FAssetEditor
{
public:
	FPhysicalMaterialEditor(class UEditorEngine& InEditorEngine, class FEditorManager& InEditorManager,
		const FString& InAssetPath, UPhysicalMaterial& InMaterial);

	const char* GetEditorTypeName() const override { return "Physical Material"; }
	bool IsDirty() const override { return bDirty; }
	void Tick(float DeltaTime) override {}   // 미리보기 장면이 없다

	UPhysicalMaterial* GetMaterial() const { return Material; }
	void MarkDirty() { bDirty = true; }
	bool Save();

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override { return true; }   // 미리보기 장면·뷰포트를 만들지 않는다
	void ShutdownResources() override {}

private:
	UPhysicalMaterial* Material = nullptr;
	bool bDirty = false;
};

class FPhysicalMaterialEditorTab : public FAssetEditorTab
{
public:
	explicit FPhysicalMaterialEditorTab(FPhysicalMaterialEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	FPhysicalMaterialEditor& PhysicalMaterialEditor;
};
