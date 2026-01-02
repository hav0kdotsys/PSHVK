#include "disk.h"
#include <Shlwapi.h>
#include <vector>
#include <winioctl.h>
#include <string>




#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")

typedef VOID(WINAPI* PFMIFS_FORMAT)(
	PWSTR DriveRoot,
	DWORD MediaType,
	PWSTR FileSystem,
	PWSTR Label,
	BOOL Quick,
	DWORD ClusterSize,
	void* Callback
	);

static std::wstring AnsiToWide(const char* s)
{
	if (!s || !*s)
		return L"";

	int len = MultiByteToWideChar(
		CP_ACP,
		0,
		s,
		-1,
		nullptr,
		0);

	if (len <= 0)
		return L"";

	std::wstring out(len, L'\0');

	MultiByteToWideChar(
		CP_ACP,
		0,
		s,
		-1,
		&out[0],
		len);

	// remove trailing null added by WinAPI
	if (!out.empty() && out.back() == L'\0')
		out.pop_back();

	return out;
}

std::wstring Disk::VolumeGuidToDriveRoot(const std::wstring& volumeGuid)
{
	wchar_t paths[512]{};
	DWORD len = 0;

	if (!GetVolumePathNamesForVolumeNameW(
		volumeGuid.c_str(),
		paths,
		ARRAYSIZE(paths),
		&len))
		return L"";

	// First entry is the drive root (e.g. E:\)
	return std::wstring(paths);
}



// ------------------------------------------------------------
// ListVolumes
// ------------------------------------------------------------
std::vector<VolumeInfo> Disk::ListVolumes()
{
	std::vector<VolumeInfo> out;

	wchar_t buffer[512];
	DWORD len = GetLogicalDriveStringsW(512, buffer);

	for (wchar_t* drive = buffer; *drive; drive += wcslen(drive) + 1)
	{
		VolumeInfo v{};
		v.RootPath = drive;

		wchar_t label[MAX_PATH]{};
		wchar_t fs[MAX_PATH]{};
		DWORD serial = 0, flags = 0;

		if (!GetVolumeInformationW(
			drive,
			label,
			MAX_PATH,
			&serial,
			nullptr,
			&flags,
			fs,
			MAX_PATH))
			continue;

		ULARGE_INTEGER freeBytes{}, totalBytes{};
		if (!GetDiskFreeSpaceExW(drive, &freeBytes, &totalBytes, nullptr))
			continue;

		v.Label = label;
		v.FileSystem = fs;
		v.TotalBytes = totalBytes.QuadPart;
		v.FreeBytes = freeBytes.QuadPart;

		out.push_back(v);
	}

	return out;
}

bool Disk::ConvertDiskPartitionSchemeDiskPart(
	int physicalDiskIndex,
	bool toGpt,
	std::wstring* outLog)
{
	std::string script;
	script += "select disk " + std::to_string(physicalDiskIndex) + "\r\n";
	script += "attributes disk clear readonly\r\n";
	script += "clean\r\n";
	script += toGpt ? "convert gpt\r\n" : "convert mbr\r\n";
	script += "exit\r\n";

	return RunDiskPartScriptA(script, outLog);
}


bool Disk::RecreateDiskAndFormat(
	int physicalDiskIndex,
	bool gpt,                    // true=GPT, false=MBR
	const std::wstring& fs,       // L"ntfs"/L"exfat"/L"fat32"
	const std::wstring& label,
	bool quick,
	wchar_t forceLetter,
	std::wstring* outLog)
{
	std::string fsA = WToUtf8(fs);
	std::string labelA = WToUtf8(label);

	std::string script;
	script += "select disk " + std::to_string(physicalDiskIndex) + "\r\n";
	script += "attributes disk clear readonly\r\n";
	script += "clean\r\n";
	script += gpt ? "convert gpt\r\n" : "convert mbr\r\n";
	script += "create partition primary\r\n";

	script += "format fs=" + fsA;
	if (quick) script += " quick";
	if (!labelA.empty())
		script += " label=\"" + labelA + "\"";
	script += "\r\n";

	if (forceLetter)
	{
		script += "assign letter=";
		script += (char)forceLetter;
		script += "\r\n";
	}
	else
	{
		script += "assign\r\n";
	}

	script += "exit\r\n";

	return RunDiskPartScriptA(script, outLog);
}


// ------------------------------------------------------------
// DISKPART SCRIPT RUNNER
// ------------------------------------------------------------

bool Disk::RunDiskPartScriptA(const std::string& script, std::wstring* outLog)
{
	char tempPath[MAX_PATH]{};
	if (!GetTempPathA(MAX_PATH, tempPath))
		return false;

	char scriptFile[MAX_PATH]{};
	if (!GetTempFileNameA(tempPath, "dp", 0, scriptFile))
		return false;

	// Write script as ANSI/UTF-8 text
	{
		HANDLE h = CreateFileA(scriptFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;

		DWORD written = 0;
		WriteFile(h, script.data(), (DWORD)script.size(), &written, nullptr);
		CloseHandle(h);
	}

	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE rPipe = nullptr, wPipe = nullptr;
	if (!CreatePipe(&rPipe, &wPipe, &sa, 0))
		return false;

	SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

	std::string cmd = "diskpart.exe /s \"";
	cmd += scriptFile;
	cmd += "\"";

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = wPipe;
	si.hStdError = wPipe;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION pi{};

	// CreateProcessA needs mutable buffer
	std::vector<char> cmdLine(cmd.begin(), cmd.end());
	cmdLine.push_back('\0');

	if (!CreateProcessA(
		nullptr,
		cmdLine.data(),
		nullptr, nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr, nullptr,
		&si,
		&pi))
	{
		CloseHandle(rPipe);
		CloseHandle(wPipe);
		DeleteFileA(scriptFile);
		return false;
	}

	CloseHandle(wPipe);

	std::string logA;
	char buf[4096];
	DWORD read = 0;

	while (ReadFile(rPipe, buf, sizeof(buf) - 1, &read, nullptr) && read > 0)
	{
		buf[read] = 0;
		logA += buf;
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(rPipe);

	DeleteFileA(scriptFile);

	if (outLog)
	{
		int wlen = MultiByteToWideChar(CP_UTF8, 0, logA.c_str(), -1, nullptr, 0);
		if (wlen <= 1)
			wlen = MultiByteToWideChar(CP_ACP, 0, logA.c_str(), -1, nullptr, 0);

		if (wlen > 1)
		{
			std::wstring w(wlen - 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, logA.c_str(), -1, w.data(), wlen);
			if (w.empty())
				MultiByteToWideChar(CP_ACP, 0, logA.c_str(), -1, w.data(), wlen);
			*outLog = w;
		}
		else
		{
			*outLog = L"";
		}
	}

	return exitCode == 0;
}

std::string Disk::WToUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1) return {};
	std::string out(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), len, nullptr, nullptr);
	return out;
}

wchar_t Disk::ExtractDriveLetter(const std::wstring& root)
{
	// expects "E:\" or "E:"
	if (root.size() >= 1 && ((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')))
		return (wchar_t)towupper(root[0]);
	return 0;
}

bool Disk::CopyFileSafe(const std::wstring& src, const std::wstring& dst)
{
	return CopyFileW(src.c_str(), dst.c_str(), FALSE);
}

bool Disk::MoveFileSafe(const std::wstring& src, const std::wstring& dst)
{
	return MoveFileExW(
		src.c_str(),
		dst.c_str(),
		MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING
	);
}

bool Disk::DeleteFileSafe(const std::wstring& path)
{
	return DeleteFileW(path.c_str());
}

bool Disk::CreateDir(const std::wstring& path)
{
	if (PathFileExistsW(path.c_str()))
		return true;

	return CreateDirectoryExW(nullptr, path.c_str(), nullptr) != FALSE;

}

HANDLE Disk::OpenPhysicalDisk(int index)
{
	wchar_t path[64];
	swprintf(path, 64, L"\\\\.\\PhysicalDrive%d", index);

	HANDLE h = CreateFileW(
		path,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	if (h == INVALID_HANDLE_VALUE)
		return INVALID_HANDLE_VALUE;

	return h;
}


std::vector<DiskInfo> Disk::EnumeratePhysicalDisks()
{
	std::vector<DiskInfo> out;

	for (int i = 0; i < 32; i++)
	{
		DiskInfo d{};
		if (GetDiskInfo(i, d))
			out.push_back(d);
	}
	return out;
}

bool Disk::GetDiskInfo(int physicalIndex, DiskInfo& out)
{
	HANDLE h = OpenPhysicalDisk(physicalIndex);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	// ---- Disk size ----
	DISK_GEOMETRY_EX geo{};
	DWORD bytes = 0;

	if (!DeviceIoControl(
		h,
		IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
		nullptr, 0,
		&geo, sizeof(geo),
		&bytes, nullptr))
	{
		CloseHandle(h);
		return false;
	}

	out.SizeBytes = geo.DiskSize.QuadPart;

	// ---- Model / serial ----
	STORAGE_PROPERTY_QUERY q{};
	q.PropertyId = StorageDeviceProperty;
	q.QueryType = PropertyStandardQuery;

	BYTE buffer[1024]{};

	if (DeviceIoControl(
		h,
		IOCTL_STORAGE_QUERY_PROPERTY,
		&q, sizeof(q),
		buffer, sizeof(buffer),
		&bytes, nullptr))
	{
		auto desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;

		if (desc->ProductIdOffset)
		{
			const char* model =
				(const char*)buffer + desc->ProductIdOffset;
			out.Model = AnsiToWide(model);
		}

		if (desc->SerialNumberOffset)
		{
			const char* serial =
				(const char*)buffer + desc->SerialNumberOffset;
			out.Serial = AnsiToWide(serial);
		}

	}

	CloseHandle(h);
	return true;
}


std::vector<PartitionInfo> Disk::ListPartitions(int index)
{
	std::vector<PartitionInfo> out;
	HANDLE h = OpenPhysicalDisk(index);
	if (h == INVALID_HANDLE_VALUE)
		return out;

	BYTE buffer[4096]{};
	DWORD bytes = 0;

	if (!DeviceIoControl(
		h,
		IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
		nullptr, 0,
		buffer, sizeof(buffer),
		&bytes, nullptr))
	{
		CloseHandle(h);
		return out;
	}

	auto layout = (DRIVE_LAYOUT_INFORMATION_EX*)buffer;

	for (DWORD i = 0; i < layout->PartitionCount; i++)
	{
		auto& p = layout->PartitionEntry[i];
		PartitionInfo pi{};

		pi.Offset = p.StartingOffset.QuadPart;
		pi.Size = p.PartitionLength.QuadPart;
		pi.Type = p.PartitionStyle;
		pi.Bootable = p.Mbr.BootIndicator;

		out.push_back(pi);
	}

	CloseHandle(h);
	return out;
}


//static bool ReadRaw(
//    HANDLE h,
//    uint64_t offset,
//    void* buffer,
//    DWORD size)
//{
//    LARGE_INTEGER li;
//    li.QuadPart = offset;
//    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
//
//    DWORD read = 0;
//    return ReadFile(h, buffer, size, &read, nullptr);
//}
//
//static bool WriteRaw(
//    HANDLE h,
//    uint64_t offset,
//    const void* buffer,
//    DWORD size)
//{
//    LARGE_INTEGER li;
//    li.QuadPart = offset;
//    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
//
//    DWORD written = 0;
//    return WriteFile(h, buffer, size, &written, nullptr);
//}

static bool IsFat32Allowed(const std::wstring& root)
{
	ULARGE_INTEGER total{};
	if (!GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, nullptr))
		return false;

	return total.QuadPart <= (32ULL * 1024 * 1024 * 1024);
}

std::wstring Disk::GetPartitionRootPath(
	int physicalDiskIndex,
	const PartitionInfo& part)
{
	wchar_t volumeName[MAX_PATH];
	HANDLE hFind = FindFirstVolumeW(volumeName, ARRAYSIZE(volumeName));
	if (hFind == INVALID_HANDLE_VALUE)
		return L"";

	do
	{
		// Open volume
		HANDLE hVol = CreateFileW(
			volumeName,
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			OPEN_EXISTING,
			0,
			nullptr);

		if (hVol == INVALID_HANDLE_VALUE)
			continue;

		// Query disk extents
		VOLUME_DISK_EXTENTS extents{};
		DWORD bytesReturned = 0;

		if (DeviceIoControl(
			hVol,
			IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
			nullptr,
			0,
			&extents,
			sizeof(extents),
			&bytesReturned,
			nullptr))
		{
			for (DWORD i = 0; i < extents.NumberOfDiskExtents; i++)
			{
				const DISK_EXTENT& e = extents.Extents[i];

				if ((int)e.DiskNumber == physicalDiskIndex &&
					e.StartingOffset.QuadPart == part.Offset)
				{
					CloseHandle(hVol);
					FindVolumeClose(hFind);

					// Return the volume GUID root
					return std::wstring(volumeName);
				}
			}
		}

		CloseHandle(hVol);

	} while (FindNextVolumeW(hFind, volumeName, ARRAYSIZE(volumeName)));

	FindVolumeClose(hFind);
	return L"";
}

bool Disk::GetVolumeFsAndFlags(const std::wstring& root, std::wstring& fs, DWORD& flags)
{
	wchar_t fsName[MAX_PATH]{};
	flags = 0;

	if (!GetVolumeInformationW(
		root.c_str(),
		nullptr, 0,
		nullptr,
		nullptr,
		&flags,
		fsName,
		ARRAYSIZE(fsName)))
		return false;

	fs = fsName;
	return true;
}

bool Disk::IsIsoLikeFs(const std::wstring& fs)
{
	return (_wcsicmp(fs.c_str(), L"CDFS") == 0) || (_wcsicmp(fs.c_str(), L"UDF") == 0);
}

bool Disk::RunDiskPartScript(const std::wstring& scriptText, std::wstring* outLog)
{
	wchar_t tempPath[MAX_PATH]{};
	if (!GetTempPathW(ARRAYSIZE(tempPath), tempPath))
		return false;

	wchar_t scriptFile[MAX_PATH]{};
	if (!GetTempFileNameW(tempPath, L"dp", 0, scriptFile))
		return false;

	// write script
	{
		HANDLE h = CreateFileW(scriptFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;

		DWORD written = 0;
		std::wstring s = scriptText;
		WriteFile(h, s.c_str(), (DWORD)(s.size() * sizeof(wchar_t)), &written, nullptr);
		CloseHandle(h);
	}

	// pipes for stdout
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE rPipe = nullptr, wPipe = nullptr;
	if (!CreatePipe(&rPipe, &wPipe, &sa, 0))
		return false;

	SetHandleInformation(rPipe, HANDLE_FLAG_INHERIT, 0);

	std::wstring cmd = L"diskpart.exe /s \"";
	cmd += scriptFile;
	cmd += L"\"";

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = wPipe;
	si.hStdError = wPipe;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	PROCESS_INFORMATION pi{};

	std::wstring cmdLine = cmd; // CreateProcess needs mutable buffer
	if (!CreateProcessW(
		nullptr,
		cmdLine.data(),
		nullptr, nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr, nullptr,
		&si,
		&pi))
	{
		CloseHandle(rPipe);
		CloseHandle(wPipe);
		DeleteFileW(scriptFile);
		return false;
	}

	CloseHandle(wPipe);

	// read all output
	std::wstring log;
	char buf[4096];
	DWORD read = 0;
	while (ReadFile(rPipe, buf, sizeof(buf) - 1, &read, nullptr) && read > 0)
	{
		buf[read] = 0;

		// best-effort UTF-8/ANSI -> wide
		int wlen = MultiByteToWideChar(CP_ACP, 0, buf, -1, nullptr, 0);
		if (wlen > 1)
		{
			std::wstring tmp(wlen - 1, L'\0');
			MultiByteToWideChar(CP_ACP, 0, buf, -1, tmp.data(), wlen);
			log += tmp;
		}
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(rPipe);

	DeleteFileW(scriptFile);

	if (outLog) *outLog = log;

	return exitCode == 0;
}

std::wstring Disk::BuildDiskPartRecreateScript(
	int diskIndex,
	const std::wstring& fs,
	const std::wstring& label,
	bool quick,
	wchar_t forceLetter /* 0 = auto */)
{
	// NOTE: clean is destructive. keep this behind a confirmation modal.
	std::wstring s;
	s += L"select disk " + std::to_wstring(diskIndex) + L"\r\n";
	s += L"attributes disk clear readonly\r\n";
	s += L"clean\r\n";
	s += L"convert mbr\r\n"; // (Rufus often uses MBR for widest compat; use GPT if you prefer)
	s += L"create partition primary\r\n";

	// DiskPart format syntax:
	// format fs=ntfs quick label="NAME"
	s += L"format fs=" + fs;
	if (quick) s += L" quick";
	if (!label.empty())
		s += L" label=\"" + label + L"\"";
	s += L"\r\n";

	if (forceLetter)
		s += std::wstring(L"assign letter=") + forceLetter + L"\r\n";
	else
		s += L"assign\r\n";

	s += L"exit\r\n";
	return s;
}


bool Disk::ConvertDiskPartitionScheme(
	int physicalDiskIndex,
	PartitionScheme target,
	std::wstring* outLog)
{
	std::wstring script;
	script += L"select disk " + std::to_wstring(physicalDiskIndex) + L"\r\n";
	script += L"attributes disk clear readonly\r\n";
	script += L"clean\r\n";

	if (target == PartitionScheme::MBR)
		script += L"convert mbr\r\n";
	else
		script += L"convert gpt\r\n";

	script += L"exit\r\n";

	return RunDiskPartScript(script, outLog);
}

bool Disk::DeletePartition(
	int physicalDiskIndex,
	int partitionIndex,
	std::wstring* outLog)
{
	std::string script;
	script += "select disk " + std::to_string(physicalDiskIndex) + "\r\n";
	script += "select partition " + std::to_string(partitionIndex + 1) + "\r\n";
	script += "delete partition override\r\n";
	script += "exit\r\n";

	return RunDiskPartScriptA(script, outLog);
}

bool Disk::CreatePartition(
	int physicalDiskIndex,
	uint64_t sizeMB,                 // 0 = use all unallocated
	const std::wstring& fs,
	const std::wstring& label,
	bool quick,
	wchar_t forceLetter,
	std::wstring* outLog)
{
	std::string fsA = WToUtf8(fs);
	std::string labelA = WToUtf8(label);

	std::string script;
	script += "select disk " + std::to_string(physicalDiskIndex) + "\r\n";

	script += "create partition primary";
	if (sizeMB > 0)
		script += " size=" + std::to_string(sizeMB);
	script += "\r\n";

	// IMPORTANT: always select the newly created volume
	script += "select volume last\r\n";

	script += "format fs=" + fsA;
	if (quick)
		script += " quick";
	script += "\r\n";

	if (forceLetter)
	{
		script += "assign letter=";
		script += (char)forceLetter;
		script += "\r\n";
	}
	else
	{
		script += "assign\r\n";
	}

	if (!labelA.empty())
	{
		script += "label ";
		script += labelA;
		script += "\r\n";
	}

	script += "exit\r\n";

	return RunDiskPartScriptA(script, outLog);
}


bool Disk::RenameVolume(
	wchar_t driveLetter,
	const std::wstring& newLabel,
	std::wstring* outLog)
{
	if (!driveLetter || newLabel.empty())
		return false;

	std::string labelA = WToUtf8(newLabel);

	std::string script;
	script += "select volume ";
	script += (char)driveLetter;
	script += "\r\n";
	script += "label ";
	script += labelA;
	script += "\r\n";
	script += "exit\r\n";

	return RunDiskPartScriptA(script, outLog);
}

int Disk::GetPhysicalDiskIndexFromVolume(const std::wstring& rootPath)
{
	wchar_t volumeName[MAX_PATH] = {};
	wchar_t deviceName[MAX_PATH] = {};

	if (!GetVolumeNameForVolumeMountPointW(
		rootPath.c_str(),
		volumeName,
		ARRAYSIZE(volumeName)))
		return -1;

	if (!QueryDosDeviceW(
		volumeName + 4, // skip "\\?\"
		deviceName,
		ARRAYSIZE(deviceName)))
		return -1;

	// deviceName example:
	// "\Device\Harddisk2\Partition1"

	int diskIndex = -1;
	swscanf_s(deviceName, L"\\Device\\Harddisk%d", &diskIndex);

	return diskIndex;
}

wchar_t Disk::FindAnyDriveLetterForDisk(int physicalDiskIndex)
{
	if (physicalDiskIndex < 0)
		return 0;

	// Enumerate all volumes
	std::vector<VolumeInfo> volumes = Disk::ListVolumes();

	for (const auto& v : volumes)
	{
		if (v.RootPath.empty())
			continue;

		// Example RootPath: L"E:\\"
		wchar_t letter = v.RootPath[0];

		if (!iswalpha(letter))
			continue;

		// Resolve physical disk for this volume
		int diskIndex = Disk::GetPhysicalDiskIndexFromVolume(v.RootPath);

		if (diskIndex == physicalDiskIndex)
			return towupper(letter);
	}

	return 0; // no mounted volume found for this disk
}