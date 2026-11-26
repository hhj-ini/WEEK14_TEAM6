#pragma once

#include <filesystem>

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"

class UMaterial;

// .mat JSON 머티리얼 에셋 파일 입출력
namespace MaterialFile
{
	bool SaveToBytes(UMaterial& Material, TArray<uint8>& OutBytes, FString& OutError);
	bool LoadFromBytes(const TArray<uint8>& Bytes, UMaterial& Material, FString& OutError);

	bool Read(const std::filesystem::path& Path, UMaterial& Material, FString& OutError);
	bool Write(const std::filesystem::path& Path, UMaterial& Material, FString& OutError);

	bool IsMaterialPath(const FString& Path);
}
