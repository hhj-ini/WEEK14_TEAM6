#pragma once

#include "Core/Types.h"

namespace nv
{
	namespace cloth
	{
		class Factory;
	}
}

// 프로세스 수명 동안 NvCloth SDK와 CPU Factory를 관리한다. (FPhysicsCore와 동일한 수명 패턴)
class FClothCore
{
public:
	static FClothCore& Get()
	{
		static FClothCore Instance;
		return Instance;
	}

	bool Init();
	void Shutdown();

	bool IsInitialized() const { return CpuFactory != nullptr; }
	nv::cloth::Factory* GetCpuFactory() const { return CpuFactory; }

	FClothCore(const FClothCore&) = delete;
	FClothCore& operator=(const FClothCore&) = delete;

private:
	FClothCore() = default;
	~FClothCore() = default;

	nv::cloth::Factory* CpuFactory = nullptr;
};
