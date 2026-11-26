#pragma once

#include <filesystem>

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"

class UPhysicalMaterial;

// .pmat 물리 머티리얼 에셋 파일 입출력 (JSON, UPROPERTY를 그대로 저장한다)
namespace PhysicalMaterialFile
{
	bool SaveToBytes(UPhysicalMaterial& Material, TArray<uint8>& OutBytes, FString& OutError);
	bool LoadFromBytes(const TArray<uint8>& Bytes, UPhysicalMaterial& Material, FString& OutError);

	bool Read(const std::filesystem::path& Path, UPhysicalMaterial& Material, FString& OutError);
	bool Write(const std::filesystem::path& Path, UPhysicalMaterial& Material, FString& OutError);

	bool IsPhysicalMaterialPath(const FString& Path);
}
