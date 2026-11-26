#include "EnginePCH.h"
#include "Engine/GameSettings.h"

#include "Core/ConfigFile.h"

namespace
{
	// 창 제목에 한글을 쓸 수 있게 UTF-8을 UTF-16으로 바꾼다.
	std::wstring Utf8ToWide(const FString& Utf8)
	{
		if (Utf8.empty()) return {};
		const int Length = MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), static_cast<int>(Utf8.size()), nullptr, 0);
		std::wstring Wide(Length, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), static_cast<int>(Utf8.size()), Wide.data(), Length);
		return Wide;
	}

	// 문자열 값은 editor.ini가 저장하는 표기와 같다.
	ELightingModel ParseLightingModel(const FString& Name, ELightingModel Fallback)
	{
		if (Name == "Gouraud") return ELightingModel::Gouraud;
		if (Name == "Lambert") return ELightingModel::Lambert;
		if (Name == "Phong") return ELightingModel::Phong;
		if (Name == "Unlit") return ELightingModel::Unlit;
		return Fallback;
	}

	ELightCullingMode ParseLightCullingMode(const FString& Name, ELightCullingMode Fallback)
	{
		if (Name == "Off") return ELightCullingMode::Off;
		if (Name == "Tiled") return ELightCullingMode::Tiled;
		return Fallback;
	}
}

FGameSettings FGameSettings::Load(const FString& Path)
{
	FGameSettings Settings;
	FConfigFile Config;
	if (!Config.Load(Path))
	{
		KRS_LOG(Warning, "Game config not found: {}. Using defaults.", Path);
		return Settings;
	}

	Settings.StartupMap = Config.GetString("Game", "GameDefaultMap", Settings.StartupMap);

	// 음수는 잘못된 값으로 보고 기본값을 쓴다. 0은 주 모니터 해상도라는 뜻이라 허용한다.
	const auto GetUInt = [&Config](const char* Section, const char* Key, uint32 Default, bool bAllowZero)
		{
			const int32 Value = Config.GetInt(Section, Key, static_cast<int32>(Default));
			return (Value > 0 || (bAllowZero && Value == 0)) ? static_cast<uint32>(Value) : Default;
		};

	Settings.WindowTitle = Utf8ToWide(Config.GetString("Window", "Title", "Kurisu Engine"));
	Settings.Width = GetUInt("Window", "ResolutionSizeX", Settings.Width, true);
	Settings.Height = GetUInt("Window", "ResolutionSizeY", Settings.Height, true);
	Settings.bBorderless = Config.GetBool("Window", "Borderless", Settings.bBorderless);
	Settings.bVSync = Config.GetBool("Window", "VSync", Settings.bVSync);

	FSceneRenderOptions& Options = Settings.RenderOptions;
	Options.Settings.LightingModel = ParseLightingModel(Config.GetString("Rendering", "LightingModel", ""), Options.Settings.LightingModel);
	Options.Settings.LightCullingMode = ParseLightCullingMode(Config.GetString("Rendering", "LightCullingMode", ""), Options.Settings.LightCullingMode);
	Options.ShowFlags.AntiAliasing = Config.GetBool("Rendering", "EnableFXAA", Options.ShowFlags.AntiAliasing);
	Options.Settings.bGammaCorrection = Config.GetBool("Rendering", "GammaCorrection", Options.Settings.bGammaCorrection);
	Options.ShowFlags.Vignette = Config.GetBool("Rendering", "ShowVignette", Options.ShowFlags.Vignette);
	Options.ShowFlags.CameraOverlay = Config.GetBool("Rendering", "ShowCameraOverlay", Options.ShowFlags.CameraOverlay);
	Options.ShowFlags.SkeletalMeshes = Config.GetBool("Rendering", "ShowSkeletalMesh", Options.ShowFlags.SkeletalMeshes);
	Options.ShowFlags.Particles = Config.GetBool("Rendering", "ShowParticles", Options.ShowFlags.Particles);
	if (const FString Gamma = Config.GetString("Rendering", "DisplayGamma", ""); !Gamma.empty())
	{
		try { Options.Settings.DisplayGamma = FMath::Clamp(std::stof(Gamma), 1.0f, 3.0f); } catch (...) {}
	}

	Options.Settings.bCacheShadows = Config.GetBool("Rendering", "CacheShadows", Options.Settings.bCacheShadows);

	const int32 PcfMode = Config.GetInt("Rendering", "PcfMode", static_cast<int32>(Options.Settings.PcfMode));
	if (PcfMode >= 0 && PcfMode <= static_cast<int32>(EPcfMode::PCSS))
		Options.Settings.PcfMode = static_cast<EPcfMode>(PcfMode);

	Options.Settings.DirectionalShadowMapSize = GetUInt("Rendering", "DirectionalShadowMapSize", Options.Settings.DirectionalShadowMapSize, false);
	Options.Settings.PointShadowMapSize = GetUInt("Rendering", "PointShadowMapSize", Options.Settings.PointShadowMapSize, false);
	Options.Settings.SpotShadowMapSize = GetUInt("Rendering", "SpotShadowMapSize", Options.Settings.SpotShadowMapSize, false);

	// [ShowFlags] 섹션: 플래그 이름 그대로 키를 쓴다. 예) Fog=0. 위의 [Rendering] 옛 키보다 우선한다.
	for (const FShowFlagInfo& Info : FEngineShowFlags::GetAll())
		Options.ShowFlags.Set(Info, Config.GetBool("ShowFlags", Info.Name, Options.ShowFlags.Get(Info)));
	return Settings;
}
