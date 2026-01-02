#pragma once
#include <sstream>
#include <filesystem>
#include "json.hpp"
#include <fstream>
#include <random>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

class HVKIO
{
public:
	static void DownloadPSHVKAssets();
	static std::string GetLocalAppData();
	static std::wstring GetLocalAppDataW();
	static bool CreateInstanceFile();
	static bool ValidateInstanceFile();

	static void EnsureDirectory(const std::wstring& path);

private:
	static const char* GenerateHvkUUID();
	static size_t WriteToString(void* ptr, size_t size, size_t nmemb, void* data);
	static std::string HttpGet(const std::string& url);
	static bool DownloadFile(const std::string& url, const std::string& outPath);
	static void DownloadGithubFolder(
		const std::string& apiUrl,
		const fs::path& localPath);


};

