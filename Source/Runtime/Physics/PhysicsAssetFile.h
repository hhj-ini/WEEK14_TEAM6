#pragma once

#include <filesystem>
#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/Types.h"

class UPhysicsAsset;

namespace PhysicsAssetFile
{
	bool SaveToBytes(UPhysicsAsset& Asset, TArray<uint8>& OutBytes, FString& OutError);
	bool LoadFromBytes(const TArray<uint8>& Bytes, UPhysicsAsset& Asset, FString& OutError);

	bool Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
	bool Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);

	bool IsPhysicsAssetPath(const FString& Path);
}