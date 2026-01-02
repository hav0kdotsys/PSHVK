#include "system.h"
#include <chrono>
#include "../settings.h"
#include <directx/d3dx12.h>


#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

std::vector<Resolution> Display::UniqueResolutions(
	const std::vector<Resolution>& input)
{
	std::vector<Resolution> out;

	for (const auto& r : input)
	{
		bool exists = false;
		for (const auto& e : out)
		{
			if (e.Width == r.Width && e.Height == r.Height)
			{
				exists = true;
				break;
			}
		}

		if (!exists)
			out.push_back(r);
	}

	return out;
}


void Display::UpdateRefreshRates(ResolutionUI& ui)
{
	ui.RefreshRates.clear();

	if (ui.Filtered.empty())
		return;

	const auto& base = ui.Filtered[ui.ResolutionIndex];

	// IMPORTANT: scan ALL modes, not Filtered
	for (const auto& r : ui.All)
	{
		if (r.Width == base.Width &&
			r.Height == base.Height)
		{
			if (std::find(
				ui.RefreshRates.begin(),
				ui.RefreshRates.end(),
				r.Refresh) == ui.RefreshRates.end())
			{
				ui.RefreshRates.push_back(r.Refresh);
			}
		}
	}

	std::sort(ui.RefreshRates.begin(), ui.RefreshRates.end());

	// snap selection to first valid rate if needed
	if (!ui.RefreshRates.empty())
		ui.SelectedRefresh = ui.RefreshRates[0];
}


std::vector<Resolution> Display::EnumerateResolutions()
{
	std::vector<Resolution> out;

	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);

	for (int i = 0; EnumDisplaySettingsW(nullptr, i, &dm); i++)
	{
		Resolution r{};
		r.Width = dm.dmPelsWidth;
		r.Height = dm.dmPelsHeight;
		r.Refresh = dm.dmDisplayFrequency;

		// avoid exact duplicates
		bool exists = false;
		for (auto& e : out)
		{
			if (e.Width == r.Width &&
				e.Height == r.Height &&
				e.Refresh == r.Refresh)
			{
				exists = true;
				break;
			}
		}

		if (!exists)
			out.push_back(r);
	}

	return out;
}


bool Display::ApplyResolution(const Resolution& r)
{
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);

	dm.dmPelsWidth = r.Width;
	dm.dmPelsHeight = r.Height;
	dm.dmDisplayFrequency = r.Refresh;

	dm.dmFields =
		DM_PELSWIDTH |
		DM_PELSHEIGHT |
		DM_DISPLAYFREQUENCY;

	LONG res = ChangeDisplaySettingsW(&dm, CDS_FULLSCREEN);
	return (res == DISP_CHANGE_SUCCESSFUL);
}

void Display::RestoreResolution()
{
	ChangeDisplaySettingsW(nullptr, 0);
}

std::vector<Resolution> Display::FilterByAspect(
	const std::vector<Resolution>& all,
	int aspectIndex)
{
	std::vector<Resolution> out;

	auto match = [&](int w, int h)
		{
			switch (aspectIndex)
			{
			case 0: return w * 9 == h * 16;   // 16:9
			case 1: return w * 10 == h * 16;  // 16:10
			case 2: return w * 3 == h * 4;    // 4:3
			case 3: return w * 9 == h * 21;   // 21:9
			default: return true;
			}
		};

	for (auto& r : all)
	{
		if (match(r.Width, r.Height))
			out.push_back(r);
	}

	return out;
}



FPSLimiter::FPSLimiter()
{
	QueryPerformanceFrequency(&freq);

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	lastTime = (double)now.QuadPart / freq.QuadPart;
	frameTarget = 1.0 / 60.0;   // default 60 FPS
}

void FPSLimiter::SetTargetFPS(int fps)
{
	if (fps <= 0)
		fps = 60;

	frameTarget = 1.0 / (double)fps;
}

void FPSLimiter::Limit()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);

	double current = (double)now.QuadPart / freq.QuadPart;
	double elapsed = current - lastTime;

	if (elapsed < frameTarget)
	{
		double sleepTime = frameTarget - elapsed;

		// coarse sleep
		if (sleepTime > 0.002)
			Sleep((DWORD)((sleepTime - 0.001) * 1000.0));

		// microspin until exact time
		for (;;)
		{
			QueryPerformanceCounter(&now);
			current = (double)now.QuadPart / freq.QuadPart;

			if ((current - lastTime) >= frameTarget)
				break;

			SwitchToThread();
		}
	}

	// anchor next frame time
	QueryPerformanceCounter(&now);
	lastTime = (double)now.QuadPart / freq.QuadPart;
}

#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static ULONGLONG FileTimeToULL(const FILETIME& ft)
{
	return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

HVKSYS::HVKSYS()
{
	InitGPU();
}

// ------------------------------------------------------------
// public
// ------------------------------------------------------------

void HVKSYS::Update()
{
	UpdateCPU();
	UpdateGPU();
}

float HVKSYS::GetCPUUsage() const
{
	return cpuUsage;
}

float HVKSYS::GetGPUUsage() const
{
	// optional later (DXGI engine stats)
	return 0.0f;
}

uint64_t HVKSYS::GetGPUMemoryUsed() const
{
	return gpuMemUsed;
}

uint64_t HVKSYS::GetGPUMemoryTotal() const
{
	return gpuMemTotal;
}

const std::wstring& HVKSYS::GetGPUName() const
{
	return gpuName;
}

// ------------------------------------------------------------
// CPU usage
// ------------------------------------------------------------

void HVKSYS::UpdateCPU()
{
	FILETIME idleFT, kernelFT, userFT;
	if (!GetSystemTimes(&idleFT, &kernelFT, &userFT))
		return;

	ULONGLONG idle = FileTimeToULL(idleFT);
	ULONGLONG kernel = FileTimeToULL(kernelFT);
	ULONGLONG user = FileTimeToULL(userFT);

	ULONGLONG idleDelta = idle - prevIdle;
	ULONGLONG totalDelta =
		(kernel - prevKernel) + (user - prevUser);

	if (totalDelta > 0)
	{
		cpuUsage =
			100.0f *
			(1.0f - (float)idleDelta / (float)totalDelta);
	}

	prevIdle = idle;
	prevKernel = kernel;
	prevUser = user;
}

// ------------------------------------------------------------
// GPU info (DXGI)
// ------------------------------------------------------------

void HVKSYS::InitGPU()
{
	IDXGIFactory4* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return;

	IDXGIAdapter1* adapter = nullptr;
	if (FAILED(factory->EnumAdapters1(0, &adapter)))
	{
		factory->Release();
		return;
	}

	DXGI_ADAPTER_DESC1 desc{};
	adapter->GetDesc1(&desc);

	gpuName = desc.Description;
	gpuMemTotal = desc.DedicatedVideoMemory;

	adapter->Release();
	factory->Release();
}

void HVKSYS::UpdateGPU()
{
	IDXGIFactory4* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return;

	IDXGIAdapter3* adapter = nullptr;
	if (FAILED(factory->EnumAdapters1(0, (IDXGIAdapter1**)&adapter)))
	{
		factory->Release();
		return;
	}

	DXGI_QUERY_VIDEO_MEMORY_INFO info{};
	if (SUCCEEDED(adapter->QueryVideoMemoryInfo(
		0,
		DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
		&info)))
	{
		gpuMemUsed = info.CurrentUsage;
	}

	adapter->Release();
	factory->Release();
}

// ------------------------------------------------------------
// Rendering Engine (DX12 / DX11)
// ------------------------------------------------------------

bool HVKSYS::SupportsDX12()
{
	IDXGIFactory4* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return false;

	IDXGIAdapter1* adapter = nullptr;
	for (UINT i = 0;
		factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
		i++)
	{
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);

		// skip software adapters
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			adapter->Release();
			continue;
		}

		// test DX12 device creation
		HRESULT hr = D3D12CreateDevice(
			adapter,
			D3D_FEATURE_LEVEL_11_0,
			__uuidof(ID3D12Device),
			nullptr);

		adapter->Release();

		if (SUCCEEDED(hr))
		{
			factory->Release();
			return true;
		}
	}

	factory->Release();
	return false;
}


bool USBHelper::ExtractVidPid(const std::string& src, std::string& vid, std::string& pid)
{
	auto v = src.find("VID_");
	auto p = src.find("PID_");

	if (v == std::string::npos || p == std::string::npos)
		return false;

	vid = src.substr(v + 4, 4);
	pid = src.substr(p + 4, 4);
	return true;
}

std::string USBHelper::BuildCompositeKey(const UsbSignature& sig)
{
	return sig.vid + "|" +
		sig.pid + "|" +
		sig.serial + "|" +
		sig.manufacturer + "|" +
		sig.product;
}


bool USBHelper::GetInfoByDriveLetter(char driveLetter, UsbSignature& out)
{
	// 1) Open volume (E:, F:, etc)
	char volumePath[] = "\\\\.\\X:";
	volumePath[4] = driveLetter;

	HANDLE hVolume = CreateFileA(
		volumePath,
		0,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	if (hVolume == INVALID_HANDLE_VALUE)
		return false;

	STORAGE_DEVICE_NUMBER volDevNum{};
	DWORD bytes = 0;

	if (!DeviceIoControl(
		hVolume,
		IOCTL_STORAGE_GET_DEVICE_NUMBER,
		nullptr,
		0,
		&volDevNum,
		sizeof(volDevNum),
		&bytes,
		nullptr))
	{
		CloseHandle(hVolume);
		return false;
	}

	CloseHandle(hVolume);

	// 2) Enumerate disk drives
	HDEVINFO hDiskInfo = SetupDiGetClassDevsA(
		&GUID_DEVCLASS_DISKDRIVE,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	);

	if (hDiskInfo == INVALID_HANDLE_VALUE)
		return false;

	SP_DEVINFO_DATA diskDev{};
	diskDev.cbSize = sizeof(diskDev);

	char buffer[512];

	for (DWORD i = 0; SetupDiEnumDeviceInfo(hDiskInfo, i, &diskDev); ++i)
	{
		// Get disk instance ID
		if (!SetupDiGetDeviceInstanceIdA(
			hDiskInfo,
			&diskDev,
			buffer,
			sizeof(buffer),
			nullptr))
			continue;

		// Open physical drive N
		char physPath[64];
		sprintf_s(physPath, "\\\\.\\PhysicalDrive%lu", i);

		HANDLE hDisk = CreateFileA(
			physPath,
			0,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr
		);

		if (hDisk == INVALID_HANDLE_VALUE)
			continue;

		STORAGE_DEVICE_NUMBER diskNum{};
		if (!DeviceIoControl(
			hDisk,
			IOCTL_STORAGE_GET_DEVICE_NUMBER,
			nullptr,
			0,
			&diskNum,
			sizeof(diskNum),
			&bytes,
			nullptr))
		{
			CloseHandle(hDisk);
			continue;
		}

		CloseHandle(hDisk);

		// Match volume → disk
		if (diskNum.DeviceNumber != volDevNum.DeviceNumber)
			continue;

		// 3) Walk up to parent USB device
		DEVINST devInst = diskDev.DevInst;
		DEVINST parentInst = 0;

		while (CM_Get_Parent(&parentInst, devInst, 0) == CR_SUCCESS)
		{
			char parentId[MAX_DEVICE_ID_LEN]{};

			if (CM_Get_Device_IDA(
				parentInst,
				parentId,
				MAX_DEVICE_ID_LEN,
				0) != CR_SUCCESS)
				break;

			std::string pidStr = parentId;

			// Look for USB VID/PID node
			if (pidStr.find("USB\\VID_") != std::string::npos)
			{
				// Extract VID / PID / serial
				ExtractVidPid(pidStr, out.vid, out.pid);

				auto slash = pidStr.rfind('\\');
				if (slash != std::string::npos)
				{
					std::string serial = pidStr.substr(slash + 1);
					if (serial.find('&') == std::string::npos)
						out.serial = serial;
				}

				// Now pull properties from THIS devnode
				SP_DEVINFO_DATA usbDev{};
				usbDev.cbSize = sizeof(usbDev);

				usbDev.DevInst = parentInst;

				// Manufacturer
				SetupDiGetDeviceRegistryPropertyA(
					hDiskInfo,
					&usbDev,
					SPDRP_MFG,
					nullptr,
					(PBYTE)buffer,
					sizeof(buffer),
					nullptr);

				out.manufacturer = buffer;

				// Friendly name
				SetupDiGetDeviceRegistryPropertyA(
					hDiskInfo,
					&usbDev,
					SPDRP_FRIENDLYNAME,
					nullptr,
					(PBYTE)buffer,
					sizeof(buffer),
					nullptr);

				out.product = buffer;

				// Extended properties
				FillExtendedUsbInfo(hDiskInfo, usbDev, out);

				SetupDiDestroyDeviceInfoList(hDiskInfo);
				return true;
			}

			devInst = parentInst;
		}
	}

	SetupDiDestroyDeviceInfoList(hDiskInfo);
	return false;
}

bool USBHelper::UsbMatches(const UsbSignature& a, const UsbSignature& b)
{
	if (a.vid != b.vid) return false;
	if (a.pid != b.pid) return false;

	if (!a.serial.empty() && !b.serial.empty())
		return a.serial == b.serial;

	return a.manufacturer == b.manufacturer &&
		a.product == b.product;
}

std::wstring USBHelper::GuidToWString(const GUID& g)
{
	wchar_t buf[64]{};
	StringFromGUID2(g, buf, 64);
	return buf;
}

void USBHelper::FillExtendedUsbInfo(
	HDEVINFO hInfo,
	SP_DEVINFO_DATA& devInfo,
	UsbSignature& out)
{
	wchar_t wbuf[512];
	DWORD size = 0;

	// CLASS GUID
	if (SetupDiGetDeviceRegistryPropertyW(
		hInfo, &devInfo, SPDRP_CLASSGUID,
		nullptr, (PBYTE)wbuf, sizeof(wbuf), nullptr))
	{
		out.classGuid = wbuf;
	}

	// DRIVER KEY  (HKLM\SYSTEM\CCS\Control\Class\{GUID}\XXXX)
	if (SetupDiGetDeviceRegistryPropertyW(
		hInfo, &devInfo, SPDRP_DRIVER,
		nullptr, (PBYTE)wbuf, sizeof(wbuf), nullptr))
	{
		out.driverKey = wbuf;
	}

	DEVPROPTYPE type{};

	// BASE CONTAINER ID
	if (SetupDiGetDevicePropertyW(
		hInfo,
		&devInfo,
		&DEVPKEY_Device_ContainerId,
		&type,
		(PBYTE)&out.containerId,
		sizeof(GUID),
		&size,
		0))
	{
		// filled
	}

	// REPORTED DEVICE IDS HASH
	if (SetupDiGetDevicePropertyW(
		hInfo,
		&devInfo,
		&DEVPKEY_Device_ReportedDeviceIdsHash,
		&type,
		(PBYTE)&out.reportedIdHash,
		sizeof(DWORD),
		&size,
		0))
	{
		// filled
	}
}



