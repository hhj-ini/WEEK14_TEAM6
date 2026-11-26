#include "EnginePCH.h"
#include "PhysicsAsset.h"

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
    FArchive& Ar = Record.GetUnderlyingArchive();
    Super::Serialize(Record);

    int32 Version = Ar.IsSaving() ? 3 : 0;
    Record << SA_VALUE("Version", Version);

    if (Version < 1 || Version > 3)
    {
        Ar.SetError();
        return;
    }

    int32 Num = Ar.IsSaving() ? BodySetup.Num() : 0;
    FStructuredArchive::FArray Bodies = Record.EnterArray("Bodies", Num);

    if (Ar.IsLoading())
    {
        ClearAllBodies();
        ClearAllConstraints();
    }

    for (int32 i = 0; i < Num; ++i)
    {
        FStructuredArchive::FRecord BodyRecord = Bodies.EnterElement().EnterRecord();

        if (Ar.IsLoading())
        {
            UBodySetup* Setup = NewObject<UBodySetup>(this);
            Setup->Serialize(BodyRecord);
            BodySetup.Add(Setup);
        }
        else
        {
            BodySetup[i]->Serialize(BodyRecord);
        }
    }

    if (Version >= 2)
    {
        int32 NumCon = Ar.IsSaving() ? ConstraintSetup.Num() : 0;
        FStructuredArchive::FArray Cons = Record.EnterArray("Constraints", NumCon);

        for (int32 i = 0; i < NumCon; ++i)
        {
            FStructuredArchive::FRecord ConRecord = Cons.EnterElement().EnterRecord();
            if (Ar.IsLoading())
            {
                UPhysicsConstraintTemplate* Template = NewObject<UPhysicsConstraintTemplate>(this);
                Template->Serialize(ConRecord);
                ConstraintSetup.Add(Template);
            }
            else
            {
                ConstraintSetup[i]->Serialize(ConRecord);
            }
        }
    }

    if (Ar.IsLoading())
    {
        CollisionDisableTable.Empty();
    }

    if (Version >= 3)
    {
        int32 NumPairs = Ar.IsSaving() ? CollisionDisableTable.Num() : 0;
        FStructuredArchive::FArray Pairs = Record.EnterArray("CollisionDisable", NumPairs);
        if (Ar.IsLoading())
        {
            CollisionDisableTable.SetNum(NumPairs);
        }

        for (int32 i = 0; i < NumPairs; ++i)
        {
            FStructuredArchive::FRecord PairRecord = Pairs.EnterElement().EnterRecord();
            PairRecord << SA_VALUE("A", CollisionDisableTable[i].BoneA);
            PairRecord << SA_VALUE("B", CollisionDisableTable[i].BoneB);
        }
    }
    else if (Ar.IsLoading())
    {
        for (UPhysicsConstraintTemplate* Template : ConstraintSetup)
        {
            DisableCollision(Template->DefaultInstance.ConstraintBone1, Template->DefaultInstance.ConstraintBone2);
        }
    }
}

int32 UPhysicsAsset::FindBodyIndex(FName BoneName) const
{
    for (int32 i = 0; i < BodySetup.Num(); ++i)
    {
        if (BodySetup[i] && BodySetup[i]->BoneName == BoneName)
        {
            return i;
        }
    }
    return -1;
}

UBodySetup* UPhysicsAsset::FindBodySetup(FName BoneName) const
{
    const int32 Index = FindBodyIndex(BoneName);
    return Index >= 0 ? BodySetup[Index] : nullptr;
}

int32 UPhysicsAsset::FindConstraintIndex(FName ChildBone) const
{
    for (int32 i = 0; i < ConstraintSetup.Num(); ++i)
    {
        if (ConstraintSetup[i] && ConstraintSetup[i]->DefaultInstance.ConstraintBone1 == ChildBone)
            return i;
    }
    return -1;
}

UPhysicsConstraintTemplate* UPhysicsAsset::AddConstraint(FName ChildBone, FName ParentBone)
{
    const int32 Existing = FindConstraintIndex(ChildBone);
    if (Existing >= 0)
        return ConstraintSetup[Existing];

    UPhysicsConstraintTemplate* Template = NewObject<UPhysicsConstraintTemplate>(this);
    Template->DefaultInstance.ConstraintBone1 = ChildBone;
    Template->DefaultInstance.ConstraintBone2 = ParentBone;
    ConstraintSetup.Add(Template);
    return Template;
}

void UPhysicsAsset::ClearAllConstraints()
{
    ConstraintSetup.Empty();
}

UBodySetup* UPhysicsAsset::AddBodySetup(FName BoneName)
{
    if (UBodySetup* Existing = FindBodySetup(BoneName))
    {
        return Existing;
    }

    UBodySetup* NewSetup = NewObject<UBodySetup>(this);
    NewSetup->BoneName = BoneName;
    BodySetup.Add(NewSetup);
    return NewSetup;
}

void UPhysicsAsset::RemoveBodySetup(FName BoneName)
{
    const int32 Index = FindBodyIndex(BoneName);
    if (Index >= 0)
        BodySetup.RemoveAt(Index);

    // 이 본에 매달린 조인트도 지운다. 지우는 중 인덱스가 밀리므로 뒤에서부터 돈다
    for (int32 i = ConstraintSetup.Num() - 1; i >= 0; --i)
    {
        const FConstraintInstance& CI = ConstraintSetup[i]->DefaultInstance;
        if (CI.ConstraintBone1 == BoneName || CI.ConstraintBone2 == BoneName)
            ConstraintSetup.RemoveAt(i);
    }

    for (int32 i = CollisionDisableTable.Num() - 1; i >= 0; --i)
    {
        const FPhysicsAssetCollisionPair& P = CollisionDisableTable[i];
        if (P.BoneA == BoneName || P.BoneB == BoneName)
            CollisionDisableTable.RemoveAt(i);
    }
}

void UPhysicsAsset::ClearAllBodies()
{
    BodySetup.Empty();
    CollisionDisableTable.Empty();
}

void UPhysicsAsset::DisableCollision(FName BoneA, FName BoneB)
{
    if (BoneA == BoneB || !IsCollisionEnabled(BoneA, BoneB))
    {
        return;
    }

    CollisionDisableTable.Add({ BoneA, BoneB });
}

void UPhysicsAsset::EnableCollision(FName BoneA, FName BoneB)
{
    for (int32 i = CollisionDisableTable.Num() - 1; i >= 0; --i)
    {
        if (CollisionDisableTable[i].Matches(BoneA, BoneB))
            CollisionDisableTable.RemoveAt(i);
    }
}

bool UPhysicsAsset::IsCollisionEnabled(FName BoneA, FName BoneB) const
{
    for (const FPhysicsAssetCollisionPair& Pair : CollisionDisableTable)
    {
        if (Pair.Matches(BoneA, BoneB))
            return false;
    }
    return true;
}
