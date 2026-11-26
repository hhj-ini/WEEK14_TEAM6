#include "EnginePCH.h"
#include "SkeletalMeshClothingPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Render/SkeletalMesh.h"
#include "Clothing/ClothingAsset.h"
#include <cstdio>

FSkeletalMeshClothingPanel::FSkeletalMeshClothingPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Clothing")
{
}

void FSkeletalMeshClothingPanel::DrawContent()
{
	USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh)
	{
		ImGui::TextDisabled("No Skeletal Mesh loaded.");
		return;
	}

	const auto& LODs = Mesh->GetRenderData().LODRenderData;
	const int32 NumSections = LODs.IsEmpty() ? 0 : static_cast<int32>(LODs[0].Sections.Num());

	// 1. Clothing Data 목록 + 추가 버튼 (언리얼 Image 4 상단)
	if (ImGui::CollapsingHeader("Clothing Data", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("+ Add Clothing"))
		{
			const int32 NextIdx = static_cast<int32>(Mesh->GetClothingAssets().Num());
			std::snprintf(NewAssetNameBuf, sizeof(NewAssetNameBuf), "Clothing_%d", NextIdx);
			NewAssetSectionIndex = 0;
			ImGui::OpenPopup("AddClothingAssetPopup");
		}

		ImGui::SameLine();
		const bool bPaintOn = Editor.IsClothPaintActive();
		if (bPaintOn)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.45f, 0.85f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.54f, 0.94f, 1.0f));
		}
		if (ImGui::Button(bPaintOn ? "Deactivate Paint" : "Activate Paint"))
		{
			Editor.SetClothPaintActive(!bPaintOn);
		}
		if (bPaintOn)
		{
			ImGui::PopStyleColor(2);
		}

		if (ImGui::BeginPopup("AddClothingAssetPopup"))
		{
			ImGui::TextUnformatted("Create Clothing Data");
			ImGui::Separator();
			ImGui::InputText("Name", NewAssetNameBuf, sizeof(NewAssetNameBuf));
			if (NumSections > 1)
			{
				ImGui::SliderInt("Section", &NewAssetSectionIndex, 0, NumSections - 1);
			}
			else
			{
				NewAssetSectionIndex = 0;
				ImGui::TextDisabled("Section: 0 (LOD0)");
			}

			if (ImGui::Button("Create & Apply"))
			{
				if (UClothingAssetCommon* NewAsset = Editor.CreateClothingFromSection(NewAssetSectionIndex, FString(NewAssetNameBuf)))
				{
					Editor.ApplyClothingToSection(NewAssetSectionIndex, NewAsset);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Create Only"))
			{
				Editor.CreateClothingFromSection(NewAssetSectionIndex, FString(NewAssetNameBuf));
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		const TArray<UClothingAssetCommon*>& Assets = Mesh->GetClothingAssets();
		const TArray<int32>& Bindings = Mesh->GetSectionClothingBindings();
		UClothingAssetCommon* Selected = Editor.GetSelectedClothingAsset();

		if (ImGui::BeginChild("##ClothingDataList", ImVec2(0.0f, 96.0f), true))
		{
			if (Assets.IsEmpty())
			{
				ImGui::TextDisabled("No clothing data.");
				ImGui::TextDisabled("Right-click mesh in viewport to create.");
			}
			else
			{
				UClothingAssetCommon* PendingDelete = nullptr;
				for (int32 i = 0; i < Assets.Num(); ++i)
				{
					UClothingAssetCommon* Asset = Assets[i];
					if (!Asset)
						continue;

					ImGui::PushID(i);

					// 바인딩된 섹션 표시 문자열 생성
					FString BoundStr;
					for (int32 SecIdx = 0; SecIdx < Bindings.Num(); ++SecIdx)
					{
						if (Bindings[SecIdx] == i)
						{
							if (!BoundStr.empty()) BoundStr += ",";
							BoundStr += "S" + std::to_string(SecIdx);
						}
					}

					FString RowLabel = Asset->GetAssetName();
					if (!BoundStr.empty())
					{
						RowLabel += "  [" + BoundStr + "]";
					}
					else
					{
						RowLabel += "  (unbound)";
					}

					const bool bIsSelected = (Asset == Selected);
					const float DeleteBtnWidth = 22.0f;
					if (ImGui::Selectable(RowLabel.c_str(), bIsSelected, 0,
						ImVec2(std::max(40.0f, ImGui::GetContentRegionAvail().x - DeleteBtnWidth - 4.0f), 0.0f)))
					{
						Editor.SelectClothingAsset(Asset);
					}

					ImGui::SameLine();
					if (ImGui::SmallButton("X"))
					{
						PendingDelete = Asset;
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Delete clothing asset");
					}

					ImGui::PopID();
				}

				if (PendingDelete)
				{
					Editor.DeleteClothingAsset(PendingDelete);
				}
			}
		}
		ImGui::EndChild();
	}

	UClothingAssetCommon* Selected = Editor.GetSelectedClothingAsset();
	if (Selected)
	{
		const auto& Phys = Selected->GetPhysicalMeshData();
		ImGui::TextDisabled("Source: LOD0 - Section %d | Sim: %d verts, %d tris",
			Selected->GetSourceSectionIndex(),
			static_cast<int>(Phys.Vertices.Num()),
			static_cast<int>(Phys.Indices.Num() / 3));
	}

	// 2. Cloth Painting 설정 (언리얼 Image 4 중단~하단)
	if (ImGui::CollapsingHeader("Cloth Painting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		FClothPaintSettings& Settings = Editor.GetClothPaintSettings();
		bool bOverlayChanged = false;

		const char* ToolNames[] = { "Brush", "Fill", "Smooth" };
		int ToolIdx = static_cast<int>(Settings.Tool);
		if (ImGui::Combo("Tool", &ToolIdx, ToolNames, IM_ARRAYSIZE(ToolNames)))
		{
			Settings.Tool = static_cast<EClothPaintTool>(ToolIdx);
		}

		ImGui::Separator();
		ImGui::TextDisabled("View");
		if (ImGui::DragFloat("View Min", &Settings.ViewMin, 0.5f, 0.0f, 1000.0f, "%.1f"))
		{
			Settings.ViewMin = std::max(0.0f, Settings.ViewMin);
			if (Settings.ViewMax <= Settings.ViewMin + 0.01f)
				Settings.ViewMax = Settings.ViewMin + 1.0f;
			bOverlayChanged = true;
		}
		if (ImGui::DragFloat("View Max", &Settings.ViewMax, 0.5f, 0.1f, 1000.0f, "%.1f"))
		{
			Settings.ViewMax = std::max(Settings.ViewMin + 0.1f, Settings.ViewMax);
			bOverlayChanged = true;
		}

		ImGui::Separator();
		ImGui::TextDisabled("Tool Settings");
		if (ImGui::DragFloat("Paint Value", &Settings.PaintValue, 0.5f, 0.0f, 500.0f, "%.1f"))
		{
			Settings.PaintValue = std::max(0.0f, Settings.PaintValue);
		}

		ImGui::Separator();
		ImGui::TextDisabled("Brush");
		if (ImGui::DragFloat("Radius", &Settings.BrushRadius, 0.25f, 0.05f, 200.0f, "%.2f"))
		{
			Settings.BrushRadius = std::max(0.05f, Settings.BrushRadius);
		}
		ImGui::SliderFloat("Strength", &Settings.BrushStrength, 0.01f, 1.0f, "%.2f");
		ImGui::SliderFloat("Falloff", &Settings.BrushFalloff, 0.0f, 1.0f, "%.2f");
		ImGui::Checkbox("Ignore Backfacing", &Settings.bIgnoreBackfacing);

		if (bOverlayChanged && Editor.IsClothPaintActive())
		{
			Editor.RefreshClothPaintOverlay();
		}

		ImGui::Spacing();
		ImGui::TextDisabled("LMB: Paint | Shift+LMB: Erase (0) | [ / ]: Radius | Ctrl+Z/Y: Undo/Redo");

		ImGui::Separator();
		ImGui::TextDisabled("Quick Helpers");
		const bool bHasAsset = (Selected != nullptr);
		const bool bHasBone = (Editor.GetSelectedBoneIndex() >= 0);

		ImGui::BeginDisabled(!bHasAsset || !bHasBone);
		if (ImGui::Button("Fill from Selected Bone"))
		{
			Editor.QuickFillClothFromSelectedBone();
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Fill MaxDistance along the bone selected in Bone Hierarchy and its children.");
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(!bHasAsset);
		if (ImGui::Button("Clear All (0)"))
		{
			Editor.ClearSelectedClothingWeights();
		}
		ImGui::EndDisabled();
	}

	// 3. NvCloth 물리 파라미터 (실시간 반영)
	if (Selected && ImGui::CollapsingHeader("Cloth Config (NvCloth)"))
	{
		FClothConfigNv& Cfg = Selected->GetConfig();
		bool bCfgChanged = false;

		bCfgChanged |= ImGui::DragFloat3("Gravity (m/s^2)", &Cfg.Gravity.X, 0.1f, -50.0f, 50.0f, "%.2f");
		bCfgChanged |= ImGui::DragFloat3("Wind (m/s)", &Cfg.WindVelocity.X, 0.1f, -50.0f, 50.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Drag", &Cfg.DragCoefficient, 0.0f, 2.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Lift", &Cfg.LiftCoefficient, 0.0f, 2.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Damping", &Cfg.Damping, 0.0f, 1.0f, "%.3f");
		bCfgChanged |= ImGui::SliderFloat("Stretch Stiffness", &Cfg.StretchStiffness, 0.0f, 1.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Bend Stiffness", &Cfg.BendStiffness, 0.0f, 1.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Shear Stiffness", &Cfg.ShearStiffness, 0.0f, 1.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Tether Stiffness", &Cfg.TetherStiffness, 0.0f, 1.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Max Distance Scale", &Cfg.MaxDistanceScale, 0.0f, 3.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Solver Frequency", &Cfg.SolverFrequency, 30.0f, 240.0f, "%.0f Hz");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Substeps per second. Lower (30-60 Hz) improves FPS; higher (120 Hz) makes cloth stiffer.");
		}
		bCfgChanged |= ImGui::SliderFloat("Linear Inertia", &Cfg.LinearInertiaScale, 0.0f, 1.0f, "%.2f");
		bCfgChanged |= ImGui::SliderFloat("Angular Inertia", &Cfg.AngularInertiaScale, 0.0f, 1.0f, "%.2f");
		ImGui::Separator();
		ImGui::TextDisabled("Self-Collision");
		bCfgChanged |= ImGui::SliderFloat("Self Collision Dist", &Cfg.SelfCollisionDistance, 0.0f, 20.0f, "%.2f");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Minimum separation between cloth particles in local mesh units (cm for FBX). 0 disables self-collision.");
		}
		bCfgChanged |= ImGui::SliderFloat("Self Collision Stiff", &Cfg.SelfCollisionStiffness, 0.0f, 1.0f, "%.2f");

		if (bCfgChanged)
		{
			Editor.ApplyClothConfigLive();
		}
	}

	// 4. 바이너리 저장 (.cloth.bin) 및 상태 메시지
	ImGui::Separator();
	const bool bDirty = Editor.IsClothingDirty();
	if (bDirty)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.25f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.64f, 0.32f, 1.0f));
	}
	if (ImGui::Button(bDirty ? "Save Clothing (.cloth.bin)*" : "Save Clothing (.cloth.bin)", ImVec2(-FLT_MIN, 0.0f)))
	{
		Editor.SaveClothingAssets();
	}
	if (bDirty)
	{
		ImGui::PopStyleColor(2);
	}

	if (!Editor.GetClothStatusMessage().empty())
	{
		ImGui::Spacing();
		ImGui::TextWrapped("%s", Editor.GetClothStatusMessage().c_str());
	}
}
