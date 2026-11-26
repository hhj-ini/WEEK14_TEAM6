#include "EnginePCH.h"
#include "Engine/PhotoCapture.h"

#include "Core/Misc/Paths.h"
#include "Render/RenderDevice.h"
#include "Render/Texture2D.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <thread>
#include <vector>

namespace
{
	namespace fs = std::filesystem;
	using Microsoft::WRL::ComPtr;

	// 32bpp BGRA 픽셀을 PNG로 쓴다. 쓰는 도중 끊겨도 깨진 파일이 남지 않게 임시 파일에 쓰고 이름을 바꾼다
	bool WritePNG(const fs::path& Path, uint32 Width, uint32 Height, const std::vector<uint8>& Pixels)
	{
		std::error_code Ec;
		fs::create_directories(Path.parent_path(), Ec);
		if (Ec) return false;

		const fs::path Temp = Path.wstring() + L".tmp";
		const HRESULT ComResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		if (FAILED(ComResult) && ComResult != RPC_E_CHANGED_MODE) return false;

		const uint32 Stride = Width * 4;
		const bool bEncoded = [&]()
		{
			ComPtr<IWICImagingFactory> Factory;
			ComPtr<IWICStream> Stream;
			ComPtr<IWICBitmapEncoder> Encoder;
			ComPtr<IWICBitmapFrameEncode> Frame;
			WICPixelFormatGUID Format = GUID_WICPixelFormat32bppBGRA;
			return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory))) &&
				SUCCEEDED(Factory->CreateStream(&Stream)) &&
				SUCCEEDED(Stream->InitializeFromFilename(Temp.c_str(), GENERIC_WRITE)) &&
				SUCCEEDED(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder)) &&
				SUCCEEDED(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache)) &&
				SUCCEEDED(Encoder->CreateNewFrame(&Frame, nullptr)) &&
				SUCCEEDED(Frame->Initialize(nullptr)) &&
				SUCCEEDED(Frame->SetSize(Width, Height)) &&
				SUCCEEDED(Frame->SetPixelFormat(&Format)) && IsEqualGUID(Format, GUID_WICPixelFormat32bppBGRA) &&
				SUCCEEDED(Frame->WritePixels(Height, Stride, Stride * Height, const_cast<uint8*>(Pixels.data()))) &&
				SUCCEEDED(Frame->Commit()) && SUCCEEDED(Encoder->Commit());
		}();
		if (SUCCEEDED(ComResult)) CoUninitialize();

		if (!bEncoded)
		{
			fs::remove(Temp, Ec);
			return false;
		}
		fs::rename(Temp, Path, Ec);
		return !Ec;
	}

	fs::path MakePhotoPath()
	{
		const auto Now = std::chrono::system_clock::now();
		const std::time_t Time = std::chrono::system_clock::to_time_t(Now);
		const auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch()).count() % 1000;
		std::tm Local{};
		localtime_s(&Local, &Time);

		char Name[64];
		std::snprintf(Name, sizeof(Name), "Photo_%04d%02d%02d_%02d%02d%02d_%03d.png",
			Local.tm_year + 1900, Local.tm_mon + 1, Local.tm_mday,
			Local.tm_hour, Local.tm_min, Local.tm_sec, static_cast<int>(Millis));
		return FPaths::ProjectSavedDir() / L"Photos" / Name;
	}
}

void FPhotoCapture::Request(float FrameHeightFraction, float FrameAspect)
{
	bPending = true;
	PendingFrameHeight = FrameHeightFraction;
	PendingFrameAspect = FrameAspect;
}

void FPhotoCapture::ExecutePending(FTexture2D* FinalColor)
{
	if (!bPending || !FinalColor || !FinalColor->GetRawPtr()) return;
	bPending = false;
	if (FinalColor->GetFormat() != DXGI_FORMAT_R8G8B8A8_UNORM) return;

	const uint32 ScreenW = FinalColor->GetWidth();
	const uint32 ScreenH = FinalColor->GetHeight();
	// 뷰파인더 프레임과 같은 계산이라야 보이는 그대로 찍힌다
	const uint32 CropH = std::min(ScreenH, static_cast<uint32>(ScreenH * PendingFrameHeight + 0.5f));
	const uint32 CropW = std::min(ScreenW, static_cast<uint32>(CropH * PendingFrameAspect + 0.5f));
	const uint32 CropX = (ScreenW - CropW) / 2;
	const uint32 CropY = (ScreenH - CropH) / 2;
	if (!CropW || !CropH) return;

	ID3D11Device* Device = FRenderDevice::Get()->GetDevice();
	ID3D11DeviceContext* Context = FRenderDevice::Get()->GetContext();

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = CropW;
	Desc.Height = CropH;
	Desc.MipLevels = Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_STAGING;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ComPtr<ID3D11Texture2D> Staging;
	if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &Staging))) return;

	// 자르기는 GPU 복사에서 끝낸다. 화면 전체를 CPU로 가져올 필요가 없다
	const D3D11_BOX Box{ CropX, CropY, 0, CropX + CropW, CropY + CropH, 1 };
	Context->CopySubresourceRegion(Staging.Get(), 0, 0, 0, 0, FinalColor->GetRawPtr(), 0, &Box);

	// 셔터 한 번에 한 번만 기다리므로 여기서는 GPU를 기다려도 된다
	D3D11_MAPPED_SUBRESOURCE Mapped{};
	if (FAILED(Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped))) return;

	const uint32 Stride = CropW * 4;
	std::vector<uint8> Pixels(static_cast<size_t>(Stride) * CropH);
	for (uint32 Y = 0; Y < CropH; ++Y)
	{
		uint8* Row = Pixels.data() + static_cast<size_t>(Y) * Stride;
		memcpy(Row, static_cast<const uint8*>(Mapped.pData) + static_cast<size_t>(Y) * Mapped.RowPitch, Stride);
		// WIC 인코더 입력은 BGRA이고, 장면 알파는 의미가 없으므로 불투명으로 채운다
		for (uint32 X = 0; X < CropW; ++X)
		{
			std::swap(Row[X * 4], Row[X * 4 + 2]);
			Row[X * 4 + 3] = 255;
		}
	}
	Context->Unmap(Staging.Get(), 0);

	// PNG 압축은 수십 ms가 걸려서, 게임이 멈추지 않게 다른 스레드에서 쓴다
	std::thread([Pixels = std::move(Pixels), CropW, CropH, Path = MakePhotoPath()]()
		{
			if (WritePNG(Path, CropW, CropH, Pixels))
				SavedCount.fetch_add(1);
		}).detach();
}
