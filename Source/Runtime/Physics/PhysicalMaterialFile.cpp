#include "EnginePCH.h"
#include "PhysicalMaterialFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "PhysicalMaterial.h"
#include "Asset/AssetUtils.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

bool PhysicalMaterialFile::SaveToBytes(UPhysicalMaterial& Material, TArray<uint8>& OutBytes, FString& OutError)
{
	OutError.clear();
	OutBytes.Empty();
	FMemoryWriter Writer(OutBytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Material.Serialize(Archive.Open().EnterRecord());
	}

	if (Writer.IsError())
	{
		OutError = "PhysicalMaterial: cannot serialize data";
		return false;
	}
	return true;
}

bool PhysicalMaterialFile::LoadFromBytes(const TArray<uint8>& Bytes, UPhysicalMaterial& Material, FString& OutError)
{
	OutError.clear();
	FMemoryReader Reader(Bytes, true);

	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicalMaterial: invalid JSON";
		return false;
	}
	FStructuredArchive Archive(Formatter);
	Material.Serialize(Archive.Open().EnterRecord());
	if (Reader.IsError())
	{
		OutError = "PhysicalMaterial: cannot serialize data";
		return false;
	}
	return true;
}

bool PhysicalMaterialFile::Read(const std::filesystem::path& Path, UPhysicalMaterial& Material, FString& OutError)
{
	OutError.clear();
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicalMaterial: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicalMaterial: cannot read file";
		return false;
	}

	if (!LoadFromBytes(Bytes, Material, OutError))
	{
		return false;
	}

	Material.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
	return true;
}

bool PhysicalMaterialFile::Write(const std::filesystem::path& Path, UPhysicalMaterial& Material, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!SaveToBytes(Material, Bytes, OutError))
	{
		return false;
	}

	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicalMaterial: cannot open output file";
		return false;
	}

	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicalMaterial: cannot write file";
		return false;
	}
	Material.SetAssetPath(FAssetUtils::GetAssetKey(Path.generic_string()));
	return true;
}

bool PhysicalMaterialFile::IsPhysicalMaterialPath(const FString& Path)
{
	FString Extension = std::filesystem::path(Path).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
	return Extension == ".pmat";
}
