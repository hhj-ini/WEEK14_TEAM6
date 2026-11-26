#include "EnginePCH.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetDebugRenderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Buffer.h"
#include "Render/Mesh.h"

namespace
{
	const FVector4 MeshLit(0.0f, 0.0f, 0.0f, 1.0f);
	const FVector4 MeshFlat(0.0f, 0.0f, 0.0f, 0.0f);

	// Dir에 수직인 두 축 (화살표·막대 메시를 Dir 방향으로 세울 때 쓴다)
	void MakePerpendicular(const FVector& Dir, FVector& OutA, FVector& OutB)
	{
		const FVector Helper = std::abs(Dir.Z) < 0.9f ? FVector(0, 0, 1) : FVector(1, 0, 0);
		OutA = Helper.Cross(Dir).GetSafeNormal();
		OutB = Dir.Cross(OutA).GetSafeNormal();
	}
}

FPhysicsAssetDebugRenderer::FPhysicsAssetDebugRenderer() = default;
FPhysicsAssetDebugRenderer::~FPhysicsAssetDebugRenderer() = default;

FMatrix FPhysicsAssetDebugRenderer::MakeBasis(const FVector& X, const FVector& Y, const FVector& Z, const FVector& Origin)
{
	// row-vector: 행 i가 로컬 축 i의 월드 방향, 4행이 위치
	FMatrix M = FMatrix::Identity;
	M.M[0][0] = X.X; M.M[0][1] = X.Y; M.M[0][2] = X.Z;
	M.M[1][0] = Y.X; M.M[1][1] = Y.Y; M.M[1][2] = Y.Z;
	M.M[2][0] = Z.X; M.M[2][1] = Z.Y; M.M[2][2] = Z.Z;
	M.M[3][0] = Origin.X; M.M[3][1] = Origin.Y; M.M[3][2] = Origin.Z;
	return M;
}

bool FPhysicsAssetDebugRenderer::EnsureResources()
{
	if (bResourcesReady)
		return true;

	Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/PhysicsDebugShader.hlsl");
	HemisphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("PhysicsHemisphere");
	CylinderMesh = UAssetManager::GetAssetByPath<UStaticMesh>("PhysicsCylinder");
	FanMesh = UAssetManager::GetAssetByPath<UStaticMesh>("PhysicsFan");
	SphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
	CubeMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	ArrowMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Arrow");
	if (!Shader || !HemisphereMesh || !CylinderMesh || !FanMesh || !SphereMesh || !CubeMesh || !ArrowMesh)
		return false;

	// 반투명이라 깊이는 읽기만 한다 (뒤에 그린 것이 앞의 것을 가리지 않게)
	SolidState.Shader = Shader;
	SolidState.Topology = EPrimitiveTopology::TriangleList;
	SolidState.RasterizerState = ERasterizerState::SolidBack;
	SolidState.BlendState = EBlendState::AlphaBlend;
	SolidState.DepthStencilState = EDepthStencilState::ReadOnly;

	TwoSidedState = SolidState;
	TwoSidedState.RasterizerState = ERasterizerState::SolidNone;

	ConstantBuffer = RenderCommand::CreateConstantBuffer(sizeof(FPhysicsDebugConstants));
	bResourcesReady = ConstantBuffer != nullptr;
	return bResourcesReady;
}

void FPhysicsAssetDebugRenderer::Begin(const FMatrix& InViewProjection, bool bSolidShapes)
{
	if (!EnsureResources())
		return;

	ViewProjection = InViewProjection;
	RenderCommand::BindPipelineState(bSolidShapes ? SolidState : TwoSidedState);
}

void FPhysicsAssetDebugRenderer::DrawMesh(UStaticMesh* Mesh, const FMatrix& World, const FVector4& Color, const FVector4& Params)
{
	if (!bResourcesReady || !Mesh || !Mesh->IndexBuffer)
		return;

	FPhysicsDebugConstants Constants;
	Constants.World = World;
	Constants.ViewProjection = ViewProjection;
	Constants.Color = Color;
	Constants.Params = Params;

	RenderCommand::BindMesh(Mesh);
	RenderCommand::UpdateBufferData(ConstantBuffer.get(), &Constants, sizeof(Constants));
	RenderCommand::BindConstantBuffer(0, ConstantBuffer.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::DrawIndexed(Mesh->IndexBuffer->GetIndexCount());
}

void FPhysicsAssetDebugRenderer::DrawCapsule(const FMatrix& ElemToWorld, float Radius, float Length, const FVector4& Color)
{
	const float HalfLength = Length * 0.5f;

	// 위 반구(+Z), 원기둥, 아래 반구(X축으로 뒤집어 -Z). 셋의 둘레 분할이 같아 이음새에서 겹치지 않는다
	DrawMesh(HemisphereMesh, MakeBasis(FVector(Radius, 0, 0), FVector(0, Radius, 0), FVector(0, 0, Radius),
		FVector(0, 0, HalfLength)) * ElemToWorld, Color, MeshLit);
	if (Length > 0.0f)
	{
		DrawMesh(CylinderMesh, MakeBasis(FVector(Radius, 0, 0), FVector(0, Radius, 0), FVector(0, 0, Length),
			FVector(0, 0, 0)) * ElemToWorld, Color, MeshLit);
	}
	DrawMesh(HemisphereMesh, MakeBasis(FVector(Radius, 0, 0), FVector(0, -Radius, 0), FVector(0, 0, -Radius),
		FVector(0, 0, -HalfLength)) * ElemToWorld, Color, MeshLit);
}

void FPhysicsAssetDebugRenderer::DrawSphere(const FMatrix& ElemToWorld, float Radius, const FVector4& Color)
{
	DrawMesh(SphereMesh, MakeBasis(FVector(Radius, 0, 0), FVector(0, Radius, 0), FVector(0, 0, Radius),
		FVector(0, 0, 0)) * ElemToWorld, Color, MeshLit);
}

void FPhysicsAssetDebugRenderer::DrawBox(const FMatrix& ElemToWorld, const FVector& Extent, const FVector4& Color)
{
	// Cube 메시는 한 변이 1이다
	DrawMesh(CubeMesh, MakeBasis(FVector(Extent.X * 2.0f, 0, 0), FVector(0, Extent.Y * 2.0f, 0), FVector(0, 0, Extent.Z * 2.0f),
		FVector(0, 0, 0)) * ElemToWorld, Color, MeshLit);
}

void FPhysicsAssetDebugRenderer::DrawCone(const FMatrix& FrameToWorld, float Angle1, float Angle2, const FVector4& Color)
{
	DrawMesh(FanMesh, FrameToWorld, Color, FVector4(1.0f, Angle1, Angle2, 0.0f));
}

void FPhysicsAssetDebugRenderer::DrawFan(const FMatrix& FrameToWorld, float Angle, const FVector4& Color)
{
	DrawMesh(FanMesh, FrameToWorld, Color, FVector4(2.0f, Angle, 0.0f, 0.0f));
}

void FPhysicsAssetDebugRenderer::DrawArrow(const FVector& Origin, const FVector& Direction, float Length, const FVector4& Color)
{
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.Size() < 0.5f)
		return;

	// Arrow 메시는 +Z로 길이 1.5(몸통 1 + 머리 0.5), 몸통 반지름 0.1
	FVector A, B;
	MakePerpendicular(Dir, A, B);
	const float Thickness = Length * 0.25f;
	DrawMesh(ArrowMesh, MakeBasis(A * Thickness, B * Thickness, Dir * (Length / 1.5f), Origin), Color, MeshFlat);
}

void FPhysicsAssetDebugRenderer::DrawStick(const FVector& Start, const FVector& End, float Radius, const FVector4& Color)
{
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= 0.0f)
		return;

	// 원기둥(반지름 1, 높이 1, 가운데 원점)을 두 점 사이에 세운다
	const FVector Dir = Delta / Length;
	FVector A, B;
	MakePerpendicular(Dir, A, B);
	DrawMesh(CylinderMesh, MakeBasis(A * Radius, B * Radius, Dir * Length, (Start + End) * 0.5f), Color, MeshFlat);
}
