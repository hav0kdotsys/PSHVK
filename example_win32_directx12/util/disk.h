#pragma once

#include <string>
#include <vector>
#include "imgui.h"
#include <Windows.h>

struct VolumeInfo
{
	std::wstring RootPath;   // e.g. L"C:\\"
	std::wstring Label;
	std::wstring FileSystem;
	uint64_t TotalBytes;
	uint64_t FreeBytes;
};

struct DiskInfo
{
	uint64_t SizeBytes;
	std::wstring Model;
	std::wstring Serial;
};

struct PartitionInfo
{
	uint64_t Offset;
	uint64_t Size;
	DWORD Type;
	bool Bootable;
};

struct DiskSelection
{
	int PhysicalIndex = -1; // PhysicalDriveX
	int VolumeIndex = -1; // VolumeInfo index
	int PartitionIndex = -1; // PartitionInfo index
};

enum class FileSystem
{
	NTFS,
	exFAT,
	FAT,
	FAT32
};

enum class PartitionScheme
{
	MBR,
	GPT
};

static const wchar_t* FsToString(FileSystem fs)
{
	switch (fs)
	{
	case FileSystem::NTFS:  return L"NTFS";
	case FileSystem::exFAT: return L"exFAT";
	case FileSystem::FAT:   return L"FAT";
	case FileSystem::FAT32: return L"FAT32";
	}
	return nullptr;
};

static const char* BytesToStr(uint64_t v)
{
	static char buf[64];
	const char* suffix[] = { "B", "KB", "MB", "GB", "TB" };
	double size = (double)v;
	int i = 0;

	while (size >= 1024.0 && i < 4)
	{
		size /= 1024.0;
		i++;
	}

	snprintf(buf, sizeof(buf), "%.2f %s", size, suffix[i]);
	return buf;
}

//bool ReadRaw(
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
//bool WriteRaw(
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


class Disk
{
public:
	static std::vector<VolumeInfo> ListVolumes();
	static HANDLE OpenPhysicalDisk(int index);
	static std::vector<DiskInfo> EnumeratePhysicalDisks();
	static std::vector<PartitionInfo> ListPartitions(int index);
	static bool GetDiskInfo(int physicalIndex, DiskInfo& out);
	static void RefreshPartitionsForSelectedDisk();

	static bool CopyFileSafe(const std::wstring& src, const std::wstring& dst);
	static bool MoveFileSafe(const std::wstring& src, const std::wstring& dst);
	static bool DeleteFileSafe(const std::wstring& path);

	static bool CreateDir(const std::wstring& path);
	static bool IsValidIndex(int idx, int size);
	static std::wstring GetPartitionRootPath(
		int physicalDiskIndex,
		const PartitionInfo& part);

	static std::wstring VolumeGuidToDriveRoot(const std::wstring& volumeGuid);


	static bool GetVolumeFsAndFlags(const std::wstring& root, std::wstring& fs, DWORD& flags);
	static bool IsIsoLikeFs(const std::wstring& fs);
	static bool RunDiskPartScript(const std::wstring& scriptText, std::wstring* outLog);
	static std::wstring BuildDiskPartRecreateScript(
		int diskIndex,
		const std::wstring& fs,
		const std::wstring& label,
		bool quick,
		wchar_t forceLetter /* 0 = auto */);
	static bool ConvertDiskPartitionScheme(
		int physicalDiskIndex,
		PartitionScheme target,
		std::wstring* outLog = nullptr
	);

	// Pretty much wipes the disk and leaves the space unallocated
	static bool RecreateDiskAndFormat(
		int physicalDiskIndex,
		bool gpt,                    // true=GPT, false=MBR
		const std::wstring& fs,       // L"ntfs"/L"exfat"/L"fat32"
		const std::wstring& label,
		bool quick,
		wchar_t forceLetter,
		std::wstring* outLog);

	// Deletes the specified partition from the selected disk
	static bool DeletePartition(
		int physicalDiskIndex,
		int partitionIndex,
		std::wstring* outLog = nullptr);

	// Create a new partition in the unallocated space of selected disk
	static bool CreatePartition(
		int physicalDiskIndex,
		uint64_t sizeMB,           // 0 = use all space
		const std::wstring& fs,
		const std::wstring& label,
		bool quick,
		wchar_t forceLetter,
		std::wstring* outLog = nullptr);

	static bool RenameVolume(
		wchar_t driveLetter,
		const std::wstring& newLabel,
		std::wstring* outLog = nullptr);


	static bool ConvertDiskPartitionSchemeDiskPart(
		int physicalDiskIndex,
		bool toGpt,
		std::wstring* outLog);

	static wchar_t ExtractDriveLetter(const std::wstring& root);
	static wchar_t FindAnyDriveLetterForDisk(int physicalDiskIndex);
	static int GetPhysicalDiskIndexFromVolume(const std::wstring& rootPath);

private:

	static std::string WToUtf8(const std::wstring& w);
	static bool RunDiskPartScriptA(const std::string& script, std::wstring* outLog);

};
