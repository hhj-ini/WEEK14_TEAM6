#pragma once

#include "MaterialEditorPanel.h"

class FMaterialViewportPanel : public FMaterialEditorPanel
{
public:
	explicit FMaterialViewportPanel(FMaterialEditor& InEditor);

protected:
	void DrawContent() override;
};
