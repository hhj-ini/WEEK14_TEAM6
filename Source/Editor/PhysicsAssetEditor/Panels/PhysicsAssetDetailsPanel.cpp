#include "EnginePCH.h"
#include "PhysicsAssetDetailsPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditorMath.h"
#include "Editor/Details/PropertyEditor.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Math/EngineMath.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace
{
	float GLabelWidth = 120.0f;

	// 왼쪽 라벨, 오른쪽 위젯 한 줄
	void RowLabel(const char* Label)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(GLabelWidth);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	bool DragRow(const char* Label, const char* Id, float* Value, float Speed, float Min, float Max, const char* Format = "%.3f")
	{
		RowLabel(Label);
		return ImGui::DragFloat(Id, Value, Speed, Min, Max, Format, ImGuiSliderFlags_AlwaysClamp);
	}

	void ReadOnlyRow(const char* Label, const FString& Value)
	{
		RowLabel(Label);
		ImGui::BeginDisabled();
		char Buffer[256];
		snprintf(Buffer, sizeof(Buffer), "%s", Value.c_str());
		ImGui::InputText((FString("##") + Label).c_str(), Buffer, sizeof(Buffer), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
	}

	template <typename EnumT>
	bool ComboRow(const char* Label, const char* Id, EnumT& Value, const char* const* Names, int32 Count)
	{
		RowLabel(Label);
		int32 Current = static_cast<int32>(Value);
		if (ImGui::Combo(Id, &Current, Names, Count))
		{
			Value = static_cast<EnumT>(Current);
			return true;
		}
		return false;
	}

	// Free / Limited / Locked 라디오 한 줄
	bool MotionRow(const char* Label, EAngularConstraintMotion& Motion)
	{
		RowLabel(Label);
		ImGui::PushID(Label);
		bool bChanged = false;
		const char* Names[] = { "Free", "Limited", "Locked" };
		for (int32 i = 0; i < 3; ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(Names[i], static_cast<int32>(Motion) == i))
			{
				Motion = static_cast<EAngularConstraintMotion>(i);
				bChanged = true;
			}
		}
		ImGui::PopID();
		return bChanged;
	}

	// 조인트 프리셋 버튼 한 줄. 고른 조인트 모두에 들어가고, 에디터가 바로 프리뷰를 다시 만든다
	void PresetRow(FPhysicsAssetEditor& Editor)
	{
		using EPreset = FPhysicsAssetEditor::EConstraintPreset;
		RowLabel("Constraint Type");
		const float Width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
		if (ImGui::Button("Ball and Socket", ImVec2(Width, 0.0f)))
			Editor.ApplyConstraintPreset(EPreset::BallAndSocket);
		ImGui::SameLine();
		if (ImGui::Button("Hinge", ImVec2(Width, 0.0f)))
			Editor.ApplyConstraintPreset(EPreset::Hinge);
		ImGui::SameLine();
		if (ImGui::Button("Skeletal", ImVec2(Width, 0.0f)))
			Editor.ApplyConstraintPreset(EPreset::Skeletal);
	}

	using PhysicsAssetEditorMath::QuatFromAxes;

	// 조인트 프레임 한 쪽(위치 + 두 축)을 위치·오일러 회전으로 보여 준다
	bool DrawConstraintFrame(const char* Id, FVector& Pos, FVector& PriAxis, FVector& SecAxis, FRotationConversionCache& Cache)
	{
		ImGui::PushID(Id);
		bool bChanged = FPropertyEditor::DrawVector3("Position", Pos.V, 0.0f);

		FRotator Euler = Cache.QuatToRotator(QuatFromAxes(PriAxis, SecAxis));
		if (FPropertyEditor::DrawRotator("Rotation", Euler))
		{
			const FQuat Q = Cache.RotatorToQuat(Euler);
			PriAxis = Q.RotateVector(FVector(1, 0, 0));
			SecAxis = Q.RotateVector(FVector(0, 1, 0));
			bChanged = true;
		}
		ImGui::PopID();
		return bChanged;
	}

	// 형상 부피(본 공간 단위). 질량 오버라이드가 없을 때 런타임이 쓰는 값과 같은 식이다
	float ComputeVolume(const FKAggregateGeom& Geom)
	{
		float Volume = 0.0f;
		for (const FKSphereElem& S : Geom.SphereElems)
			Volume += 4.0f / 3.0f * PI * S.Radius * S.Radius * S.Radius;
		for (const FKBoxElem& B : Geom.BoxElems)
			Volume += 8.0f * B.Extent.X * B.Extent.Y * B.Extent.Z;
		for (const FKSphylElem& S : Geom.SphylElems)
			Volume += PI * S.Radius * S.Radius * S.Length + 4.0f / 3.0f * PI * S.Radius * S.Radius * S.Radius;
		return Volume;
	}
}

FPhysicsAssetDetailsPanel::FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor)
	: FPhysicsAssetEditorPanel(InEditor, "Details")
{
}

void FPhysicsAssetDetailsPanel::DrawContent()
{
	GLabelWidth = std::max(ImGui::GetContentRegionAvail().x * 0.4f, 80.0f);

	const FPhysicsAssetSelection& Selection = Editor.GetSelection();
	if (LastSelectionType != static_cast<int32>(Selection.Type) || LastSelectionIndex != Selection.Index)
	{
		LastSelectionType = static_cast<int32>(Selection.Type);
		LastSelectionIndex = Selection.Index;
		ChildFrameRotation = FRotationConversionCache();
		ParentFrameRotation = FRotationConversionCache();
		PrimitiveRotations.Empty();
	}

	// 시뮬레이션 중에는 값을 보여 주기만 한다
	const bool bSimulating = Editor.IsSimulating();
	if (bSimulating)
		ImGui::TextDisabled("Stop the simulation to edit.");
	ImGui::BeginDisabled(bSimulating);

	bool bChanged = false;
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	const TArray<int32> BodyIndices = Editor.GetSelectedBodyIndices();
	const TArray<int32> ConstraintIndices = Editor.GetSelectedConstraintIndices();
	if (Asset && BodyIndices.Num() + ConstraintIndices.Num() > 1)
	{
		// 여러 개: 같은 종류끼리면 공통 속성만 보여 주고 한꺼번에 바꾼다
		if (!BodyIndices.IsEmpty() && !ConstraintIndices.IsEmpty())
		{
			ImGui::TextDisabled("%d bodies and %d constraints selected.", BodyIndices.Num(), ConstraintIndices.Num());
			ImGui::TextDisabled("Select only bodies or only constraints to edit them together.");
		}
		else if (!BodyIndices.IsEmpty())
		{
			TArray<UBodySetup*> Bodies;
			for (int32 Index : BodyIndices)
				Bodies.Add(Asset->BodySetup[Index]);
			bChanged = DrawMultiBodyDetails(Bodies);
		}
		else
		{
			TArray<UPhysicsConstraintTemplate*> Templates;
			for (int32 Index : ConstraintIndices)
				Templates.Add(Asset->ConstraintSetup[Index]);
			bChanged = DrawMultiConstraintDetails(Templates);
		}
	}
	else if (UBodySetup* Body = Editor.GetSelectedBody())
		bChanged = DrawBodyDetails(*Body);
	else if (UPhysicsConstraintTemplate* Template = Editor.GetSelectedConstraint())
		bChanged = DrawConstraintDetails(*Template);
	else
		ImGui::TextDisabled("Select a body or constraint.");

	ImGui::EndDisabled();

	if (bChanged && !bSimulating)
		Editor.NotifyAssetEdited();
}

bool FPhysicsAssetDetailsPanel::DrawBodyDetails(UBodySetup& Body)
{
	FBodyInstance& BI = Body.DefaultInstance;
	bool bChanged = false;

	if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 질량: 체크하면 직접 정하고, 끄면 형상 부피 × 물리 머티리얼 밀도로 계산한 값을 보여 준다
		const UPhysicalMaterial* Material = BI.GetPhysicalMaterial();
		const float Density = Material ? Material->Density : 1000.0f;
		const float AutoMass = ComputeVolume(Body.AggGeom) * Density;
		bool bOverride = BI.MassInKgOverride > 0.0f;

		RowLabel("Mass (kg)");
		if (ImGui::Checkbox("##MassOverride", &bOverride))
		{
			BI.MassInKgOverride = bOverride ? std::max(AutoMass, 0.01f) : 0.0f;
			bChanged = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::BeginDisabled(!bOverride);
		float ShownMass = bOverride ? BI.MassInKgOverride : AutoMass;
		if (ImGui::DragFloat("##Mass", &ShownMass, 0.1f, 0.001f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp) && bOverride)
		{
			BI.MassInKgOverride = ShownMass;
			bChanged = true;
		}
		ImGui::EndDisabled();

		bChanged |= DragRow("Linear Damping", "##LinearDamping", &BI.LinearDamping, 0.01f, 0.0f, 1000.0f);
		bChanged |= DragRow("Angular Damping", "##AngularDamping", &BI.AngularDamping, 0.01f, 0.0f, 1000.0f);

		static const char* PhysicsTypeNames[] = { "Default", "Kinematic", "Simulated" };
		bChanged |= ComboRow("Physics Type", "##PhysicsType", Body.PhysicsType, PhysicsTypeNames, 3);

		bChanged |= FPropertyEditor::DrawStructMember(&Body, &BI, *FBodyInstance::StaticStruct(), "PhysMaterialOverride");
	}

	if (ImGui::CollapsingHeader("Body Setup", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ReadOnlyRow("Bone Name", Body.BoneName.ToString());
		bChanged |= DrawPrimitives(Body);
	}

	if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// Disabled여도 바디는 남아 조인트에 매달린다. 다른 바디와 부딪히지만 않는다
		const bool bEnabled = BI.CollisionEnabled == ECollisionEnabled::QueryAndPhysics ||
			BI.CollisionEnabled == ECollisionEnabled::PhysicsOnly;
		int32 Response = bEnabled ? 0 : 1;
		static const char* ResponseNames[] = { "Enabled", "Disabled" };
		RowLabel("Collision Response");
		if (ImGui::Combo("##CollisionResponse", &Response, ResponseNames, 2))
		{
			BI.CollisionEnabled = Response == 0 ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly;
			bChanged = true;
		}
	}

	return bChanged;
}

bool FPhysicsAssetDetailsPanel::DrawPrimitives(UBodySetup& Body)
{
	FKAggregateGeom& Geom = Body.AggGeom;
	PrimitiveRotations.SetNum(Geom.GetElementCount());
	int32 CacheIndex = 0;
	bool bChanged = false;
	const float MinSize = 0.0001f;

	auto DrawRotation = [&](FQuat& Rotation)
	{
		FRotator Euler = PrimitiveRotations[CacheIndex].QuatToRotator(Rotation);
		if (FPropertyEditor::DrawRotator("Rotation", Euler))
		{
			Rotation = PrimitiveRotations[CacheIndex].RotatorToQuat(Euler);
			bChanged = true;
		}
	};

	for (int32 i = 0; i < Geom.SphylElems.Num(); ++i, ++CacheIndex)
	{
		FKSphylElem& E = Geom.SphylElems[i];
		ImGui::PushID(CacheIndex);
		if (ImGui::TreeNodeEx("Capsule", ImGuiTreeNodeFlags_DefaultOpen, "Capsule %d", i))
		{
			bChanged |= FPropertyEditor::DrawVector3("Center", E.Center.V, 0.0f);
			DrawRotation(E.Rotation);
			bChanged |= DragRow("Radius", "##Radius", &E.Radius, 0.001f, MinSize, FLT_MAX);
			bChanged |= DragRow("Length", "##Length", &E.Length, 0.001f, 0.0f, FLT_MAX);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	for (int32 i = 0; i < Geom.BoxElems.Num(); ++i, ++CacheIndex)
	{
		FKBoxElem& E = Geom.BoxElems[i];
		ImGui::PushID(CacheIndex);
		if (ImGui::TreeNodeEx("Box", ImGuiTreeNodeFlags_DefaultOpen, "Box %d", i))
		{
			bChanged |= FPropertyEditor::DrawVector3("Center", E.Center.V, 0.0f);
			DrawRotation(E.Rotation);
			if (FPropertyEditor::DrawVector3("Extent", E.Extent.V, 0.5f))
			{
				E.Extent = FVector(std::max(E.Extent.X, MinSize), std::max(E.Extent.Y, MinSize), std::max(E.Extent.Z, MinSize));
				bChanged = true;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	for (int32 i = 0; i < Geom.SphereElems.Num(); ++i, ++CacheIndex)
	{
		FKSphereElem& E = Geom.SphereElems[i];
		ImGui::PushID(CacheIndex);
		if (ImGui::TreeNodeEx("Sphere", ImGuiTreeNodeFlags_DefaultOpen, "Sphere %d", i))
		{
			bChanged |= FPropertyEditor::DrawVector3("Center", E.Center.V, 0.0f);
			bChanged |= DragRow("Radius", "##Radius", &E.Radius, 0.001f, MinSize, FLT_MAX);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	if (Geom.GetElementCount() == 0)
		ImGui::TextDisabled("No primitives. Right-click the bone in Skeleton Tree to add one.");

	return bChanged;
}

bool FPhysicsAssetDetailsPanel::DrawConstraintDetails(UPhysicsConstraintTemplate& Template)
{
	FConstraintInstance& CI = Template.DefaultInstance;
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	bool bChanged = false;

	if (ImGui::CollapsingHeader("Constraint", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ReadOnlyRow("Joint Name", CI.ConstraintBone1.ToString());
		ReadOnlyRow("Child Bone", CI.ConstraintBone1.ToString());
		ReadOnlyRow("Parent Bone", CI.ConstraintBone2.ToString());
	}

	if (ImGui::CollapsingHeader("Constraint Transforms", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 자식 프레임은 자식 본 공간, 부모 프레임은 부모 본 공간 값이다
		ImGui::TextDisabled("Child");
		bChanged |= DrawConstraintFrame("Child", CI.Pos1, CI.PriAxis1, CI.SecAxis1, ChildFrameRotation);
		ImGui::TextDisabled("Parent");
		bChanged |= DrawConstraintFrame("Parent", CI.Pos2, CI.PriAxis2, CI.SecAxis2, ParentFrameRotation);
	}

	if (ImGui::CollapsingHeader("Constraint Behavior", ImGuiTreeNodeFlags_DefaultOpen) && Asset)
	{
		// 조인트로 이어진 두 바디끼리의 충돌. 체크하면 서로 통과한다
		bool bDisableCollision = !Asset->IsCollisionEnabled(CI.ConstraintBone1, CI.ConstraintBone2);
		RowLabel("Disable Collision");
		if (ImGui::Checkbox("##DisableCollision", &bDisableCollision))
		{
			if (bDisableCollision)
				Asset->DisableCollision(CI.ConstraintBone1, CI.ConstraintBone2);
			else
				Asset->EnableCollision(CI.ConstraintBone1, CI.ConstraintBone2);
			bChanged = true;
		}
	}

	if (ImGui::CollapsingHeader("Linear Limits", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 위치 3축은 항상 잠겨 있다 (D6 조인트를 위치 고정으로 만든다)
		ImGui::BeginDisabled();
		const char* Axes[] = { "X Motion", "Y Motion", "Z Motion" };
		for (const char* Axis : Axes)
		{
			EAngularConstraintMotion Locked = EAngularConstraintMotion::Locked;
			MotionRow(Axis, Locked);
		}
		ImGui::EndDisabled();
	}

	if (ImGui::CollapsingHeader("Angular Limits", ImGuiTreeNodeFlags_DefaultOpen))
	{
		PresetRow(Editor);
		bChanged |= MotionRow("Swing 1 Motion", CI.Swing1Motion);
		bChanged |= MotionRow("Swing 2 Motion", CI.Swing2Motion);
		bChanged |= MotionRow("Twist Motion", CI.TwistMotion);

		ImGui::BeginDisabled(CI.Swing1Motion != EAngularConstraintMotion::Limited);
		bChanged |= DragRow("Swing 1 Limit", "##Swing1Limit", &CI.Swing1LimitDegrees, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
		ImGui::BeginDisabled(CI.Swing2Motion != EAngularConstraintMotion::Limited);
		bChanged |= DragRow("Swing 2 Limit", "##Swing2Limit", &CI.Swing2LimitDegrees, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
		ImGui::BeginDisabled(CI.TwistMotion != EAngularConstraintMotion::Limited);
		bChanged |= DragRow("Twist Limit", "##TwistLimit", &CI.TwistLimitDegrees, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
	}

	return bChanged;
}

namespace
{
	// 값이 서로 다르면 칸에 이 글자를 띄운다
	const char* MultipleValuesText = "Multiple Values";

	template <typename T, typename GetterT>
	bool AllSame(const TArray<T*>& Items, GetterT Get)
	{
		for (int32 i = 1; i < Items.Num(); ++i)
		{
			if (!(Get(*Items[i]) == Get(*Items[0])))
				return false;
		}
		return true;
	}

	// 여러 값에 한 칸. 다르면 "Multiple Values"를 띄우고, 바꾸면 모두 그 값이 된다
	bool MultiDragRow(const char* Label, const char* Id, const TArray<float*>& Values, float Speed, float Min, float Max, const char* Format = "%.3f")
	{
		bool bSame = true;
		for (const float* V : Values)
			bSame &= *V == *Values[0];

		RowLabel(Label);
		float Shown = *Values[0];
		if (!ImGui::DragFloat(Id, &Shown, Speed, Min, Max, bSame ? Format : MultipleValuesText, ImGuiSliderFlags_AlwaysClamp))
			return false;
		for (float* V : Values)
			*V = Shown;
		return true;
	}

	// 서로 다르면 아무 항목도 칠하지 않는 Free / Limited / Locked 라디오 줄
	bool MultiMotionRow(const char* Label, const TArray<EAngularConstraintMotion*>& Motions)
	{
		bool bSame = true;
		for (const EAngularConstraintMotion* M : Motions)
			bSame &= *M == *Motions[0];

		RowLabel(Label);
		ImGui::PushID(Label);
		bool bChanged = false;
		const char* Names[] = { "Free", "Limited", "Locked" };
		for (int32 i = 0; i < 3; ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(Names[i], bSame && static_cast<int32>(*Motions[0]) == i))
			{
				for (EAngularConstraintMotion* M : Motions)
					*M = static_cast<EAngularConstraintMotion>(i);
				bChanged = true;
			}
		}
		ImGui::PopID();
		return bChanged;
	}

	// 서로 다르면 미리보기에 "Multiple Values"를 띄우는 콤보
	bool MultiComboRow(const char* Label, const char* Id, int32 Current, bool bSame, const char* const* Names, int32 Count, int32& OutPicked)
	{
		RowLabel(Label);
		bool bPicked = false;
		if (ImGui::BeginCombo(Id, bSame ? Names[Current] : MultipleValuesText))
		{
			for (int32 i = 0; i < Count; ++i)
			{
				if (ImGui::Selectable(Names[i], bSame && i == Current))
				{
					OutPicked = i;
					bPicked = true;
				}
			}
			ImGui::EndCombo();
		}
		return bPicked;
	}

	// 섞여 있으면 체크박스에 가로줄(혼합 표시)을 그린다
	bool MultiCheckbox(const char* Id, bool& bValue, bool bSame)
	{
		ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, !bSame);
		const bool bChanged = ImGui::Checkbox(Id, &bValue);
		ImGui::PopItemFlag();
		return bChanged;
	}

	bool IsCollisionOn(const FBodyInstance& BI)
	{
		return BI.CollisionEnabled == ECollisionEnabled::QueryAndPhysics || BI.CollisionEnabled == ECollisionEnabled::PhysicsOnly;
	}

	float ComputeAutoMass(const UBodySetup& Body)
	{
		const UPhysicalMaterial* Material = Body.DefaultInstance.GetPhysicalMaterial();
		return ComputeVolume(Body.AggGeom) * (Material ? Material->Density : 1000.0f);
	}
}

bool FPhysicsAssetDetailsPanel::DrawMultiBodyDetails(const TArray<UBodySetup*>& Bodies)
{
	bool bChanged = false;
	ImGui::TextDisabled("%d bodies selected. Shapes are edited one body at a time.", Bodies.Num());

	if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 질량: 체크하면 바디마다 자기 계산 질량에서 직접 값으로 바뀐다. 모두 직접 값일 때만 숫자를 한꺼번에 바꾼다
		const bool bOverrideSame = AllSame(Bodies, [](const UBodySetup& B) { return B.DefaultInstance.MassInKgOverride > 0.0f; });
		bool bOverride = Bodies[0]->DefaultInstance.MassInKgOverride > 0.0f;
		RowLabel("Mass (kg)");
		if (MultiCheckbox("##MassOverride", bOverride, bOverrideSame))
		{
			for (UBodySetup* B : Bodies)
				B->DefaultInstance.MassInKgOverride = bOverride ? std::max(ComputeAutoMass(*B), 0.01f) : 0.0f;
			bChanged = true;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		const bool bAllOverride = bOverrideSame && bOverride;
		const bool bMassSame = AllSame(Bodies, [](const UBodySetup& B) { return B.DefaultInstance.MassInKgOverride; });
		float Mass = Bodies[0]->DefaultInstance.MassInKgOverride;
		ImGui::BeginDisabled(!bAllOverride);
		if (ImGui::DragFloat("##Mass", &Mass, 0.1f, 0.001f, 1000000.0f, bAllOverride && bMassSame ? "%.3f" : MultipleValuesText,
			ImGuiSliderFlags_AlwaysClamp) && bAllOverride)
		{
			for (UBodySetup* B : Bodies)
				B->DefaultInstance.MassInKgOverride = Mass;
			bChanged = true;
		}
		ImGui::EndDisabled();

		TArray<float*> LinearDamping;
		TArray<float*> AngularDamping;
		for (UBodySetup* B : Bodies)
		{
			LinearDamping.Add(&B->DefaultInstance.LinearDamping);
			AngularDamping.Add(&B->DefaultInstance.AngularDamping);
		}
		bChanged |= MultiDragRow("Linear Damping", "##LinearDamping", LinearDamping, 0.01f, 0.0f, 1000.0f);
		bChanged |= MultiDragRow("Angular Damping", "##AngularDamping", AngularDamping, 0.01f, 0.0f, 1000.0f);

		static const char* PhysicsTypeNames[] = { "Default", "Kinematic", "Simulated" };
		int32 Picked = 0;
		if (MultiComboRow("Physics Type", "##PhysicsType", static_cast<int32>(Bodies[0]->PhysicsType),
			AllSame(Bodies, [](const UBodySetup& B) { return B.PhysicsType; }), PhysicsTypeNames, 3, Picked))
		{
			for (UBodySetup* B : Bodies)
				B->PhysicsType = static_cast<EPhysicsType>(Picked);
			bChanged = true;
		}

		// 물리 머티리얼: 첫 바디의 칸으로 고르면 모든 바디에 같은 머티리얼을 넣는다
		if (!AllSame(Bodies, [](const UBodySetup& B) { return B.DefaultInstance.PhysMaterialOverride; }))
			ImGui::TextDisabled("Phys Material Override: %s", MultipleValuesText);
		if (FPropertyEditor::DrawStructMember(Bodies[0], &Bodies[0]->DefaultInstance, *FBodyInstance::StaticStruct(), "PhysMaterialOverride"))
		{
			for (UBodySetup* B : Bodies)
				B->DefaultInstance.PhysMaterialOverride = Bodies[0]->DefaultInstance.PhysMaterialOverride;
			bChanged = true;
		}
	}

	if (ImGui::CollapsingHeader("Collision", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static const char* ResponseNames[] = { "Enabled", "Disabled" };
		int32 Picked = 0;
		if (MultiComboRow("Collision Response", "##CollisionResponse", IsCollisionOn(Bodies[0]->DefaultInstance) ? 0 : 1,
			AllSame(Bodies, [](const UBodySetup& B) { return IsCollisionOn(B.DefaultInstance); }), ResponseNames, 2, Picked))
		{
			for (UBodySetup* B : Bodies)
				B->DefaultInstance.CollisionEnabled = Picked == 0 ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::QueryOnly;
			bChanged = true;
		}
	}

	return bChanged;
}

bool FPhysicsAssetDetailsPanel::DrawMultiConstraintDetails(const TArray<UPhysicsConstraintTemplate*>& Templates)
{
	UPhysicsAsset* Asset = Editor.GetPhysicsAsset();
	bool bChanged = false;
	ImGui::TextDisabled("%d constraints selected. Frames are edited one constraint at a time.", Templates.Num());

	if (ImGui::CollapsingHeader("Constraint Behavior", ImGuiTreeNodeFlags_DefaultOpen) && Asset)
	{
		const auto IsDisabled = [Asset](const UPhysicsConstraintTemplate& T)
		{
			return !Asset->IsCollisionEnabled(T.DefaultInstance.ConstraintBone1, T.DefaultInstance.ConstraintBone2);
		};
		bool bDisableCollision = IsDisabled(*Templates[0]);
		RowLabel("Disable Collision");
		if (MultiCheckbox("##DisableCollision", bDisableCollision, AllSame(Templates, IsDisabled)))
		{
			for (UPhysicsConstraintTemplate* T : Templates)
			{
				const FConstraintInstance& CI = T->DefaultInstance;
				if (bDisableCollision)
					Asset->DisableCollision(CI.ConstraintBone1, CI.ConstraintBone2);
				else
					Asset->EnableCollision(CI.ConstraintBone1, CI.ConstraintBone2);
			}
			bChanged = true;
		}
	}

	if (ImGui::CollapsingHeader("Angular Limits", ImGuiTreeNodeFlags_DefaultOpen))
	{
		PresetRow(Editor);
		TArray<EAngularConstraintMotion*> Swing1, Swing2, Twist;
		TArray<float*> Swing1Limit, Swing2Limit, TwistLimit;
		for (UPhysicsConstraintTemplate* T : Templates)
		{
			FConstraintInstance& CI = T->DefaultInstance;
			Swing1.Add(&CI.Swing1Motion);
			Swing2.Add(&CI.Swing2Motion);
			Twist.Add(&CI.TwistMotion);
			Swing1Limit.Add(&CI.Swing1LimitDegrees);
			Swing2Limit.Add(&CI.Swing2LimitDegrees);
			TwistLimit.Add(&CI.TwistLimitDegrees);
		}
		bChanged |= MultiMotionRow("Swing 1 Motion", Swing1);
		bChanged |= MultiMotionRow("Swing 2 Motion", Swing2);
		bChanged |= MultiMotionRow("Twist Motion", Twist);

		// 한계 각도는 고른 조인트가 모두 Limited일 때만 바꾼다
		const auto AllLimited = [](const TArray<EAngularConstraintMotion*>& Motions)
		{
			return std::all_of(Motions.begin(), Motions.end(),
				[](const EAngularConstraintMotion* M) { return *M == EAngularConstraintMotion::Limited; });
		};
		ImGui::BeginDisabled(!AllLimited(Swing1));
		bChanged |= MultiDragRow("Swing 1 Limit", "##Swing1Limit", Swing1Limit, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!AllLimited(Swing2));
		bChanged |= MultiDragRow("Swing 2 Limit", "##Swing2Limit", Swing2Limit, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
		ImGui::BeginDisabled(!AllLimited(Twist));
		bChanged |= MultiDragRow("Twist Limit", "##TwistLimit", TwistLimit, 0.5f, 0.0f, 179.0f, "%.1f");
		ImGui::EndDisabled();
	}

	return bChanged;
}
