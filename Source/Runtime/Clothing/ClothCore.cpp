#include "EnginePCH.h"
#include "ClothCore.h"
#include "NvClothIncludes.h"
#include "Physics/PhysicsCore.h"

#include <cstdlib>
#include <format>

namespace
{
	// NvCloth는 16바이트 정렬 메모리를 요구한다 (Callbacks.h).
	class FNvClothAllocator final : public physx::PxAllocatorCallback
	{
	public:
		void* allocate(size_t Size, const char* /*TypeName*/, const char* /*Filename*/, int /*Line*/) override
		{
			return _aligned_malloc(Size, 16);
		}

		void deallocate(void* Ptr) override
		{
			_aligned_free(Ptr);
		}
	};

	class FNvClothErrorCallback final : public physx::PxErrorCallback
	{
	public:
		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			ELogVerbosity Verbosity = ELogVerbosity::Error;
			switch (Code)
			{
			case physx::PxErrorCode::eDEBUG_INFO:
				Verbosity = ELogVerbosity::Info;
				break;
			case physx::PxErrorCode::eDEBUG_WARNING:
			case physx::PxErrorCode::ePERF_WARNING:
				Verbosity = ELogVerbosity::Warning;
				break;
			default:
				break;
			}
			FPhysicsCore::Get().EnqueueLog(Verbosity, std::format("[NvCloth] {} ({}:{})", Message, File, Line));
		}
	};

	class FNvClothAssertHandler final : public physx::PxAssertHandler
	{
	public:
		void operator()(const char* Exp, const char* File, int Line, bool& Ignore) override
		{
			FPhysicsCore::Get().EnqueueLog(ELogVerbosity::Error,
				std::format("[NvCloth Assert] '{}' failed at {}:{}", Exp, File, Line));
			Ignore = true;
		}
	};

	FNvClothAllocator ClothAllocator;
	FNvClothErrorCallback ClothErrorCallback;
	FNvClothAssertHandler ClothAssertHandler;
}

bool FClothCore::Init()
{
	if (CpuFactory)
		return true;

	nv::cloth::InitializeNvCloth(&ClothAllocator, &ClothErrorCallback, &ClothAssertHandler, nullptr);

	CpuFactory = NvClothCreateFactoryCPU();
	if (!CpuFactory)
	{
		KRS_LOG(Error, "[Cloth] NvClothCreateFactoryCPU failed!");
		return false;
	}

	KRS_LOG(Info, "[Cloth] NvCloth 1.1.6 CPU Factory initialized.");
	return true;
}

void FClothCore::Shutdown()
{
	if (CpuFactory)
	{
		NvClothDestroyFactory(CpuFactory);
		CpuFactory = nullptr;
		KRS_LOG(Info, "[Cloth] NvCloth CPU Factory shut down.");
	}
}
