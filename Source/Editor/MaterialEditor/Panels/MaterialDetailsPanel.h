#pragma once

#include "MaterialEditorPanel.h"

class FMaterialDetailsPanel : public FMaterialEditorPanel
{
public:
	explicit FMaterialDetailsPanel(FMaterialEditor& InEditor);

protected:
	void DrawContent() override;
};
