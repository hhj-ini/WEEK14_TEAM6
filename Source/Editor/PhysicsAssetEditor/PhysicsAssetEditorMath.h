#pragma once

#include "Math/Vector.h"
#include "Math/Quat.h"
#include "Math/Matrix.h"

#include <cmath>

namespace PhysicsAssetEditorMath
{
	// 두 축(X = Pri, Y = Sec)으로 정한 프레임의 회전. Sec는 Pri에 직교하도록 다시 맞춘다
	inline FQuat QuatFromAxes(const FVector& Pri, const FVector& Sec)
	{
		const FVector X = Pri.GetSafeNormal(SMALL_NUMBER, FVector(1, 0, 0));
		FVector Y = (Sec - X * X.Dot(Sec)).GetSafeNormal();
		if (Y.Size() < 0.5f)
			Y = (std::abs(X.Z) < 0.9f ? FVector(0, 0, 1) : FVector(0, 1, 0)).Cross(X).GetSafeNormal();
		const FVector Z = X.Cross(Y);

		// 열이 X, Y, Z인 회전 행렬 → 쿼터니언
		const float M00 = X.X, M01 = Y.X, M02 = Z.X;
		const float M10 = X.Y, M11 = Y.Y, M12 = Z.Y;
		const float M20 = X.Z, M21 = Y.Z, M22 = Z.Z;
		const float Trace = M00 + M11 + M22;

		FQuat Q;
		if (Trace > 0.0f)
		{
			const float S = 0.5f / std::sqrt(Trace + 1.0f);
			Q = FQuat((M21 - M12) * S, (M02 - M20) * S, (M10 - M01) * S, 0.25f / S);
		}
		else if (M00 > M11 && M00 > M22)
		{
			const float S = 2.0f * std::sqrt(1.0f + M00 - M11 - M22);
			Q = FQuat(0.25f * S, (M01 + M10) / S, (M02 + M20) / S, (M21 - M12) / S);
		}
		else if (M11 > M22)
		{
			const float S = 2.0f * std::sqrt(1.0f + M11 - M00 - M22);
			Q = FQuat((M01 + M10) / S, 0.25f * S, (M12 + M21) / S, (M02 - M20) / S);
		}
		else
		{
			const float S = 2.0f * std::sqrt(1.0f + M22 - M00 - M11);
			Q = FQuat((M02 + M20) / S, (M12 + M21) / S, 0.25f * S, (M10 - M01) / S);
		}
		return Q.Normalized();
	}

	// row-vector 행렬의 회전 부분 (행 0, 1이 로컬 X, Y의 월드 방향). 스케일은 버린다
	inline FQuat QuatFromMatrix(const FMatrix& M)
	{
		return QuatFromAxes(FVector(M.M[0][0], M.M[0][1], M.M[0][2]), FVector(M.M[1][0], M.M[1][1], M.M[1][2]));
	}

	// 위치·회전(스케일 1)으로 row-vector 행렬을 만든다
	inline FMatrix MakeMatrix(const FVector& Location, const FQuat& Rotation)
	{
		FMatrix M = Rotation.ToFMatrix();
		M.M[3][0] = Location.X;
		M.M[3][1] = Location.Y;
		M.M[3][2] = Location.Z;
		return M;
	}
}
