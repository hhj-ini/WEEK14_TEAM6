#include "EnginePCH.h"
#include "PhysicsAssetFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "PhysicsAsset.h"
#include "Asset/AssetUtils.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

bool PhysicsAssetFile::SaveToBytes(UPhysicsAsset& Asset, TArray<uint8>& OutBytes, FString& OutError)
{
	OutError.clear();
	OutBytes.Empty();
	FMemoryWriter Writer(OutBytes);
	{
		// Formatter가 소멸할 때 JSON을 마무리하므로 블록으로 감싼다
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Asset.Serialize(Archive.Open().EnterRecord());
	}
	if (Writer.IsError())
	{
		OutError = "PhysicsAsset: cannot serialize data";
		return false;
	}
	return true;
}

bool PhysicsAssetFile::LoadFromBytes(const TArray<uint8>& Bytes, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	FMemoryReader Reader(Bytes, true);

	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: invalid JSON";
		return false;
	}
	FStructuredArchive Archive(Formatter);
	Asset.Serialize(Archive.Open().EnterRecord());
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: cannot serialize data (version mismatch?)";
		return false;
	}
	return true;
}

bool PhysicsAssetFile::Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicsAsset: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicsAsset: cannot read file";
		return false;
	}

	if (!LoadFromBytes(Bytes, Asset, OutError))
		return false;

	Asset.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
	return true;
}

bool PhysicsAssetFile::Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!SaveToBytes(Asset, Bytes, OutError))
		return false;

	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicsAsset: cannot open output file";
		return false;
	}
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicsAsset: cannot write file";
		return false;
	}
	Asset.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
	return true;
}

bool PhysicsAssetFile::IsPhysicsAssetPath(const FString& Path)
{
	FString Ext = std::filesystem::path(Path).extension().string();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(),
		[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Ext == ".phys";
}
