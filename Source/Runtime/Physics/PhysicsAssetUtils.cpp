#include "EnginePCH.h"
#include "PhysicsAssetUtils.h"

#include <cfloat>
#include <algorithm>

#include "PhysicsAsset.h"
#include "BodySetup.h"
#include "Render/SkeletalMesh.h"
#include "Math/EngineMath.h"

namespace
{
    // 정점 -> 본 배정
    void CalcBoneVertInfos(const USkeletalMesh& Mesh, EPhysAssetFitVertWeight Mode,
        TArray<TArray<FVector>>& OutVerts)
    {
        const FSkeletalMeshLODRenderData& LOD = Mesh.GetRenderData().LODRenderData[0];
        OutVerts.SetNum(Mesh.GetNumBones());

        for (int32 V = 0; V < LOD.BindVertices.Num(); ++V)
        {
            const FVector& Pos = LOD.BindVertices[V].Position;
            const FSkinWeight& W = LOD.SkinWeights[V];
                
            if (Mode == EPhysAssetFitVertWeight::DominantWeight)
            {
                int32 BestBone = -1;
                float BestWeight = 0.0f;

                for (int32 K = 0; K < MaxBoneInfluences; ++K)
                {
                    if (W.Weight[K] > BestWeight)
                    {
                        BestWeight = W.Weight[K];
                        BestBone = W.BoneIndex[K];
                    }
                }

                if (BestBone >= 0)
                {
                    OutVerts[BestBone].Add(Pos);
                }   
            }
            else
            {
                for (int32 K = 0; K < MaxBoneInfluences; ++K)
                {
                    if (W.Weight[K] > 0.0f)
                    {
                        OutVerts[W.BoneIndex[K]].Add(Pos);
                    }
                }
            }
        }
    }

    // 컴포넌트 공간 정점들을 뼈 공간으로 변환
    void ToBoneLocal(const FBoneInfo& Bone, const TArray<FVector>& CompVerts, TArray<FVector>& OutLocal)
    {
        OutLocal.Empty();
        OutLocal.Reserve(CompVerts.Num());
        for (const FVector& P : CompVerts)
        {
            OutLocal.Add(Bone.InverseBindPose.TransformPosition(P));
        }
    }

    // 정점들의 AABB 계산
    bool CalcBox(const TArray<FVector>& Verts, FVector& OutMin, FVector& OutMax)
    {
        if (Verts.IsEmpty())
        {
            return false;
        }

        OutMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
        OutMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for (const FVector& P : Verts)
        {
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                OutMin[Axis] = FMath::Min(OutMin[Axis], P[Axis]);
                OutMax[Axis] = FMath::Max(OutMax[Axis], P[Axis]);
            }
        }
        return true;
    }

    // 본 크기 = 본 로컬 aabb의 가장 긴 변
    float CalcBoneSize(const FBoneInfo& Bone, const TArray<FVector>& CompVerts)
    {
        TArray<FVector> Local;
        ToBoneLocal(Bone, CompVerts, Local);
        FVector Min, Max;

        if (!CalcBox(Local, Min, Max))
        {
            return 0.0f;
        }

        const FVector Size = Max - Min;
        return FMath::Max(FMath::Max(Size.X, Size.Y), Size.Z);
    }


    bool FitSphyl(const TArray<FVector>& LocalVerts, FKSphylElem& Out)
    {
        FVector Min, Max;
        if (!CalcBox(LocalVerts, Min, Max))
        {
            return false;
        }

        const FVector Center = (Min + Max) * 0.5f;
        const FVector Half = (Max - Min) * 0.5f;

        // 가장 긴 축 ==> 캡슐이 뻗는 방향임.
        int32 LongAxis = 0;
        if (Half[1] > Half[LongAxis])
        {
            LongAxis = 1;
        }
        
        if (Half[2] > Half[LongAxis])
        {
            LongAxis = 2;
        }

        const int32 A = (LongAxis + 1) % 3;
        const int32 B = (LongAxis + 2) % 3;

        const float Radius = FMath::Max(Half[A], Half[B]);
        if (Radius <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        Out.Radius = Radius;
        Out.Length = FMath::Max(0.0f, 2.0f * (Half[LongAxis] - Radius));
        Out.Center = Center;

        switch (LongAxis)
        {
        case 0:
        {
            Out.Rotation = FQuat::MakeFromAxisAngle(FVector(0, 1, 0), PI * 0.5f);
            break;
        }
        case 1:
        {
            Out.Rotation = FQuat::MakeFromAxisAngle(FVector(1, 0, 0), PI * 0.5f);
            break;
        }
        default:
        {
            Out.Rotation = FQuat::Identity();
            break;
        }
        }

        return true;
    }

    FTransform ComposeTransform(const FTransform& Local, const FTransform& Parent)
    {
        FTransform Out;
        Out.Rotation = (Parent.Rotation * Local.Rotation).Normalized();
        Out.Scale = FVector(Local.Scale.X * Parent.Scale.X, Local.Scale.Y * Parent.Scale.Y, Local.Scale.Z * Parent.Scale.Z);
        const FVector Scaled(Local.Location.X * Parent.Scale.X, Local.Location.Y * Parent.Scale.Y, Local.Location.Z * Parent.Scale.Z);
        Out.Location = Parent.Location + Parent.Rotation.RotateVector(Scaled);
        return Out;
    }

    // 바인드 포즈의 본별 컴포넌트 공간 트랜스폼. 부모가 항상 앞 인덱스라 앞에서부터 한 번에 된다.
    void CalcRefPoseComponentSpace(const TArray<FBoneInfo>& Bones, TArray<FTransform>& Out)
    {
        Out.SetNum(Bones.Num());
        for (int32 B = 0; B < Bones.Num(); ++B)
        {
            const int32 P = Bones[B].ParentIndex;
            Out[B] = (P >= 0) ? ComposeTransform(Bones[B].LocalBindPose, Out[P]) : Bones[B].LocalBindPose;
        }
    }

    // B의 조상 중 바디가 있는 가장 가까운 본. 없으면 -1.
    // 루트·IK 본·병합된 본은 바디가 없으므로 건너뛴다 (작업 3에서 생긴 구멍을 여기서 메운다)
    int32 FindParentBoneWithBody(const UPhysicsAsset& Asset, const TArray<FBoneInfo>& Bones, int32 B)
    {
        for (int32 P = Bones[B].ParentIndex; P >= 0; P = Bones[P].ParentIndex)
        {
            if (Asset.FindBodyIndex(FName(Bones[P].Name)) >= 0)
                return P;
        }
        return -1;
    }

    void CreateConstraints(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp)
    {
        const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
        for (UBodySetup* ChildSetup : Asset.BodySetup)
        {
            const int32 Child = Mesh.FindBoneIndex(ChildSetup->BoneName.ToString());
            if (Child < 0)
                continue;

                const int32 Parent = FindParentBoneWithBody(Asset, Bones, Child);
                if (Parent < 0)
                    continue;   // 최상위 바디(보통 pelvis)는 조인트가 없다

                UPhysicsConstraintTemplate* Template = Asset.AddConstraint(ChildSetup->BoneName, FName(Bones[Parent].Name));
                FConstraintInstance& CI = Template->DefaultInstance;

                // Frame1: 자식 로컬. 위치는 자식 본 원점, X(Twist)축은 캡슐의 긴 축 ──
                // FKSphylElem은 로컬 Z로 서므로 Rotation×Z가 긴 축, Rotation×X는 그에 수직인 축
                FQuat CapsuleRot = FQuat::Identity();
                if (!ChildSetup->AggGeom.SphylElems.IsEmpty())
                    CapsuleRot = ChildSetup->AggGeom.SphylElems[0].Rotation;

                CI.Pos1 = FVector(0, 0, 0);
                CI.PriAxis1 = CapsuleRot.RotateVector(FVector(0, 0, 1));
                CI.SecAxis1 = CapsuleRot.RotateVector(FVector(1, 0, 0));

                // Frame2: 같은 점·같은 축을 부모 본 공간에서 본 값 ──
                // 바인드 포즈에서 Frame1과 정확히 겹친다 → 시뮬 시작 때 튀지 않는다
                const FTransform ChildInParent = RefComp[Child].GetRelativeTransform(RefComp[Parent]);
                CI.Pos2 = ChildInParent.Location;   // Pos1이 원점이라 회전 항이 사라진다
                CI.PriAxis2 = ChildInParent.Rotation.RotateVector(CI.PriAxis1);
                CI.SecAxis2 = ChildInParent.Rotation.RotateVector(CI.SecAxis1);
        }
    }

    FVector TransformPoint(const FTransform& T, const FVector& P)
    {
        const FVector Scaled(P.X * T.Scale.X, P.Y * T.Scale.Y, P.Z * T.Scale.Z);
        return T.Location + T.Rotation.RotateVector(Scaled);
    }

    float SegmentSegmentDistSq(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2)
    {
        constexpr float Eps = 1e-8f;
        const FVector D1 = Q1 - P1;
        const FVector D2 = Q2 - P2;
        const FVector R = P1 - P2;
        const float A = D1.Dot(D1);
        const float E = D2.Dot(D2);
        const float F = D2.Dot(R);

        float S = 0.0f, T = 0.0f;
        if (A <= Eps && E <= Eps)
            return R.Dot(R);                                   // 둘 다 점
        if (A <= Eps)
        {
            T = std::clamp(F / E, 0.0f, 1.0f);                 // 첫 번째가 점
        }
        else
        {
            const float C = D1.Dot(R);
            if (E <= Eps)
            {
                S = std::clamp(-C / A, 0.0f, 1.0f);            // 두 번째가 점
            }
            else
            {
                const float B = D1.Dot(D2);
                const float Denom = A * E - B * B;             // 0이면 평행
                S = (Denom > Eps) ? std::clamp((B * F - C * E) / Denom, 0.0f, 1.0f) : 0.0f;
                T = (B * S + F) / E;
                // T가 선분 밖이면 끝점으로 자르고 S를 다시 구한다
                if (T < 0.0f) { T = 0.0f; S = std::clamp(-C / A, 0.0f, 1.0f); }
                else if (T > 1.0f) { T = 1.0f; S = std::clamp((B - C) / A, 0.0f, 1.0f); }
            }
        }
        const FVector C1 = P1 + D1 * S;
        const FVector C2 = P2 + D2 * T;
        return (C1 - C2).SizeSquared();
    }

    // 바디의 첫 캡슐을 컴포넌트 공간 선분으로. 캡슐이 없으면 false
    bool GetCapsuleSegment(const UBodySetup& Setup, const FTransform& BoneComp, FVector& OutA, FVector& OutB, float& OutRadius)
    {
        if (Setup.AggGeom.SphylElems.IsEmpty())
            return false;
        const FKSphylElem& S = Setup.AggGeom.SphylElems[0];
        const FVector HalfAxis = S.Rotation.RotateVector(FVector(0, 0, 1)) * (S.Length * 0.5f);
        OutA = TransformPoint(BoneComp, S.Center + HalfAxis);
        OutB = TransformPoint(BoneComp, S.Center - HalfAxis);
        OutRadius = S.Radius;   // 바인드 포즈 스케일은 1로 본다
        return true;
    }

    void DisableAdjacentAndOverlappingCollisions(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const TArray<FTransform>& RefComp)
    {
        // 인접: 조인트로 이어진 쌍. PhysX 조인트도 기본으로 꺼 주지만,
        //    테이블에도 넣어 두면 조인트 설정과 무관하게 의도가 에셋에 남는다
        for (UPhysicsConstraintTemplate* Template : Asset.ConstraintSetup)
            Asset.DisableCollision(Template->DefaultInstance.ConstraintBone1, Template->DefaultInstance.ConstraintBone2);

        // 바인드 포즈에서 이미 겹치는 쌍 (양쪽 허벅지, 쇄골-가슴 등). 그대로 두면 첫 스텝에 서로 밀어낸다
        const int32 N = Asset.BodySetup.Num();
        for (int32 I = 0; I < N; ++I)
        {
            const UBodySetup* A = Asset.BodySetup[I];
            const int32 BoneA = Mesh.FindBoneIndex(A->BoneName.ToString());
            FVector A0, A1; float RA;
            if (BoneA < 0 || !GetCapsuleSegment(*A, RefComp[BoneA], A0, A1, RA))
                continue;

            for (int32 J = I + 1; J < N; ++J)   // 쌍마다 한 번
            {
                const UBodySetup* B = Asset.BodySetup[J];
                const int32 BoneB = Mesh.FindBoneIndex(B->BoneName.ToString());
                FVector B0, B1; float RB;
                if (BoneB < 0 || !GetCapsuleSegment(*B, RefComp[BoneB], B0, B1, RB))
                    continue;

                const float RSum = RA + RB;
                if (SegmentSegmentDistSq(A0, A1, B0, B1) < RSum * RSum)   // sqrt 없이 제곱끼리 비교
                    Asset.DisableCollision(A->BoneName, B->BoneName);
            }
        }
    }
}

bool FPhysicsAssetUtils::CreateFromSkeletalMesh(UPhysicsAsset& Asset, const USkeletalMesh& Mesh, const FPhysAssetCreateParams& Params, FString& OutError)
{
    OutError.clear();
    const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
    if (Bones.IsEmpty() || Mesh.GetRenderData().LODRenderData.IsEmpty())
    {
        OutError = "PhysicsAsset: Mesh has no skeleton or render data";
        return false;
    }

    Asset.ClearAllBodies();
    Asset.ClearAllConstraints();

    // 정점 배정
    TArray<TArray<FVector>> BoneVerts;
    CalcBoneVertInfos(Mesh, Params.VertWeight, BoneVerts);

    // 작은 본 병합
    const FBox& MeshBox = Mesh.GetRenderData().LODRenderData[0].BindAABB;
    const FVector MeshSize = MeshBox.Max - MeshBox.Min;
    const float MinBoneSize = FMath::Max(FMath::Max(MeshSize.X, MeshSize.Y), MeshSize.Z) * Params.MinBoneSizeRatio;

    // 자식이 부모보다 먼저 처리되게 함.
    for (int32 B = Bones.Num() - 1; B > 0; --B)
    {
        if (BoneVerts[B].IsEmpty())
        {
            continue;
        }

        const int32 Parent = Bones[B].ParentIndex;
        if (Parent < 0)
        {
            continue;
        }

        if (CalcBoneSize(Bones[B], BoneVerts[B]) < MinBoneSize)
        {
            BoneVerts[Parent].Append(BoneVerts[B]);
            BoneVerts[B].Empty();
        }
    }

    TArray<FVector> Local;
    for (int32 B = 0; B < Bones.Num(); ++B)
    {
        if (BoneVerts[B].Num() < Params.MinVertsPreBody)
        {
            continue;
        }

        ToBoneLocal(Bones[B], BoneVerts[B], Local);

        FKSphylElem Sphyl;
        if (!FitSphyl(Local, Sphyl))
        {
            continue;
        }

        UBodySetup* Setup = Asset.AddBodySetup(FName(Bones[B].Name));
        Setup->AggGeom = FKAggregateGeom();
        Setup->AggGeom.SphylElems.Add(Sphyl);
    }

    if (Asset.BodySetup.IsEmpty())
    {
        OutError = "PhysicsAsset : no bone had enough vertices to make a body";
        return false;
    }

    TArray<FTransform> RefComp;
    CalcRefPoseComponentSpace(Bones, RefComp);

    CreateConstraints(Asset, Mesh, RefComp);
    DisableAdjacentAndOverlappingCollisions(Asset, Mesh, RefComp);
    KRS_LOG(Info, "[Physics] PhysicsAsset generated: {} bodies from {} bones", Asset.BodySetup.Num(), Bones.Num());
    return true;
}
