#include "EnginePCH.h"
#include "Render/VertexFactory.h"
#include "Render/Vertex.h"
#include "Render/RenderCommand.h"
#include <cstddef>
#include <iterator>

namespace
{
	constexpr D3D11_INPUT_CLASSIFICATION PerVertex = D3D11_INPUT_PER_VERTEX_DATA;
	constexpr D3D11_INPUT_CLASSIFICATION PerInstance = D3D11_INPUT_PER_INSTANCE_DATA;

	const D3D11_INPUT_ELEMENT_DESC GLocalElements[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Position), PerVertex, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Normal),   PerVertex, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Tangent),  PerVertex, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(FVertex, UV),       PerVertex, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Color),    PerVertex, 0 },
	};

	const D3D11_INPUT_ELEMENT_DESC GGpuSkinElements[] =
	{
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Position), PerVertex, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Normal),   PerVertex, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Tangent),  PerVertex, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(FVertex, UV),       PerVertex, 0 },
		{ "COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Color),    PerVertex, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, offsetof(FGpuSkinVertex, BoneIndex), PerVertex, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FGpuSkinVertex, Weight),    PerVertex, 0 },
	};

	const D3D11_INPUT_ELEMENT_DESC GMeshParticleElements[] =
	{
		{ "POSITION",           0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Position), PerVertex, 0 },
		{ "NORMAL",             0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(FVertex, Normal),   PerVertex, 0 },
		{ "TANGENT",            0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Tangent),  PerVertex, 0 },
		{ "TEXCOORD",           0, DXGI_FORMAT_R32G32_FLOAT,       0, offsetof(FVertex, UV),       PerVertex, 0 },
		{ "COLOR",              0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(FVertex, Color),    PerVertex, 0 },
		{ "INSTANCE_TRANSFORM", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(FParticleMeshInstance, Transform0), PerInstance, 1 },
		{ "INSTANCE_TRANSFORM", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(FParticleMeshInstance, Transform1), PerInstance, 1 },
		{ "INSTANCE_TRANSFORM", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(FParticleMeshInstance, Transform2), PerInstance, 1 },
		{ "INSTANCE_COLOR",     0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(FParticleMeshInstance, Color),      PerInstance, 1 },
	};

	// FLandscapeVertex: 격자 좌표 (X, Y)만. 높이는 텍스처에서 읽는다
	const D3D11_INPUT_ELEMENT_DESC GLandscapeElements[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, PerVertex, 0 },
	};

	const FVertexFactoryTypeInfo GTypeInfos[] =
	{
		{ "Local",          "VF_LOCAL",           GLocalElements },
		{ "GpuSkin",        "VF_GPU_SKIN",        GGpuSkinElements },
		{ "MeshParticle",   "VF_MESH_PARTICLE",   GMeshParticleElements },
		{ "SpriteParticle", "VF_SPRITE_PARTICLE", {} },
		{ "Trail",          "VF_TRAIL",           {} },
		{ "Landscape",      "VF_LANDSCAPE",       GLandscapeElements },
	};
	static_assert(std::size(GTypeInfos) == static_cast<size_t>(EVertexFactoryType::Count));
}

const FVertexFactoryTypeInfo& GetVertexFactoryTypeInfo(EVertexFactoryType Type)
{
	return GTypeInfos[static_cast<uint8>(Type)];
}

void FVertexFactory::Bind() const
{
	RenderCommand::BindVertexBuffer(VertexBuffer);
	RenderCommand::BindIndexBuffer(IndexBuffer);
}

void FGpuSkinVertexFactory::Bind() const
{
	FVertexFactory::Bind();
	RenderCommand::BindShaderResource(BoneMatrixBufferSlot, BoneBuffer, EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(ClothMappingBufferSlot, ClothMappingBuffer, EShaderBindFlagBits::Vertex);
	RenderCommand::BindShaderResource(ClothSimulBufferSlot, ClothSimulBuffer, EShaderBindFlagBits::Vertex);
}

void FInstancedVertexFactory::Bind() const
{
	FVertexFactory::Bind();
	RenderCommand::BindInstanceBuffer(InstanceBuffer);
}
