#pragma once
#include <windows.h>
#include <thread>
#include "imgui.h"
#include <string>
#include <vector>
#include <setupapi.h>
#include "initguid.h"
#include "Usbiodef.h"
#include <cfgmgr32.h>
#include <devpkey.h>
#include <devguid.h>

struct Resolution
{
	int Width;
	int Height;
	int Refresh; // Hz (e.g. 60, 75, 100)
};

struct ResolutionUI
{
	int AspectIndex = 0;
	int ResolutionIndex = 0;

	int SelectedRefresh = 60;

	std::vector<Resolution> All;
	std::vector<Resolution> Filtered;
	std::vector<int> RefreshRates;

	bool ApplyConfirm = false;
};


static const char* AspectPresets[] =
{
	"16:9",
	"16:10",
	"4:3",
	"21:9"
};


class Display
{
public:
	static std::vector<Resolution> UniqueResolutions(const std::vector<Resolution>& input);
	static void UpdateRefreshRates(ResolutionUI& ui);
	static std::vector<Resolution> EnumerateResolutions();
	static bool ApplyResolution(const Resolution& r);
	static void RestoreResolution();
	static std::vector<Resolution> FilterByAspect(
		const std::vector<Resolution>& all,
		int aspectIndex);
};


class FPSLimiter
{
public:
	FPSLimiter();

	// Set target FPS (30, 60, 90, 120, etc.)
	void SetTargetFPS(int fps);

	// Call at the END of each frame (after Present)
	void Limit();

private:
	LARGE_INTEGER freq;
	double frameTarget;
	double lastTime;
};


class HVKSYS
{
public:
	HVKSYS();

	// Call once per frame
	void Update();

	// ---- getters ----
	float GetCPUUsage() const;        // %
	float GetGPUUsage() const;        // % (best-effort)
	uint64_t GetGPUMemoryUsed() const; // bytes
	uint64_t GetGPUMemoryTotal() const;

	const std::wstring& GetGPUName() const;

	// Rendering Engine helper
	static bool SupportsDX12();
	static bool InitDX12(HWND hwnd);
	static bool InitDX11(HWND hwnd);
	static void CleanupRenderTargetDX11();
	static void CreateRenderTargetDX11();
	static void CleanupDeviceD3D11();


private:
	// ---- CPU ----
	ULONGLONG prevIdle = 0;
	ULONGLONG prevKernel = 0;
	ULONGLONG prevUser = 0;
	float cpuUsage = 0.0f;

	void UpdateCPU();

	// ---- GPU ----
	std::wstring gpuName;
	uint64_t gpuMemUsed = 0;
	uint64_t gpuMemTotal = 0;

	void InitGPU();
	void UpdateGPU();
};


struct UsbSignature
{
	std::string vid;
	std::string pid;
	std::string serial;
	std::string manufacturer;
	std::string product;

	std::wstring classGuid;
	std::wstring driverKey;

	GUID containerId{};
	DWORD reportedIdHash = 0;
};


class USBHelper 
{
public:
	static void FillExtendedUsbInfo(
		HDEVINFO hInfo,
		SP_DEVINFO_DATA& devInfo,
		UsbSignature& out);
	static std::wstring GuidToWString(const GUID& g);
	static bool ExtractVidPid(const std::string& src, std::string& vid, std::string& pid);
	static std::string BuildCompositeKey(const UsbSignature& sig);
	static bool GetInfoByDriveLetter(char driveLetter, UsbSignature& out);
	static bool UsbMatches(const UsbSignature& a, const UsbSignature& b);
};