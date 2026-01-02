#include "web_helper.h"
#include <ShlObj.h>
#include "curl.h"

#include <cstdio>
#include <mutex>

static std::mutex g_dlLogMutex;

static void DLLog(const char* fmt, ...)
{
	std::lock_guard<std::mutex> lock(g_dlLogMutex);

	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	printf("\n");
}

struct CurlDlCtx
{
	std::string url;
	std::string outPath;
	curl_off_t lastReported = -1;
};

static int CurlProgressCb(void* clientp,
	curl_off_t dltotal, curl_off_t dlnow,
	curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
	auto* ctx = static_cast<CurlDlCtx*>(clientp);

	// throttle logs so you don't spam and slow downloads
	if (dlnow == ctx->lastReported) return 0;
	if (dlnow < 0) return 0;

	// log every ~256KB (adjust if you want)
	const curl_off_t step = 256 * 1024;
	if (ctx->lastReported >= 0 && (dlnow - ctx->lastReported) < step)
		return 0;

	ctx->lastReported = dlnow;

	if (dltotal > 0)
	{
		double pct = (double)dlnow * 100.0 / (double)dltotal;
		DLLog("[DL] %.1f%% (%lld / %lld) %s -> %s",
			pct,
			(long long)dlnow, (long long)dltotal,
			ctx->url.c_str(),
			ctx->outPath.c_str());
	}
	else
	{
		DLLog("[DL] %lld bytes %s -> %s",
			(long long)dlnow,
			ctx->url.c_str(),
			ctx->outPath.c_str());
	}

	return 0; // return non-zero to abort
}

bool HVKIO::DownloadFile(const std::string& url, const std::string& outPath)
{
	DLLog("[DL] START  %s -> %s", url.c_str(), outPath.c_str());

	CURL* curl = curl_easy_init();
	if (!curl)
	{
		DLLog("[DL] FAIL   curl_easy_init returned null");
		return false;
	}

	std::ofstream file(outPath, std::ios::binary);
	if (!file)
	{
		DLLog("[DL] FAIL   cannot open output file: %s", outPath.c_str());
		curl_easy_cleanup(curl);
		return false;
	}

	CurlDlCtx ctx;
	ctx.url = url;
	ctx.outPath = outPath;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PSHVK-Updater");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	// write file
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		+[](void* ptr, size_t size, size_t nmemb, void* stream) -> size_t {
			// DO NOT log ptr as a string (not null-terminated + binary)
			std::ofstream* out = static_cast<std::ofstream*>(stream);
			out->write(static_cast<const char*>(ptr), size * nmemb);
			return size * nmemb;
		});

	// progress
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCb);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

	CURLcode res = curl_easy_perform(curl);

	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

	curl_easy_cleanup(curl);
	file.close();

	if (res != CURLE_OK)
	{
		DLLog("[DL] FAIL   curl=%d (%s) http=%ld  %s",
			(int)res, curl_easy_strerror(res), httpCode, url.c_str());
		return false;
	}

	if (httpCode >= 400)
	{
		DLLog("[DL] FAIL   http=%ld  %s", httpCode, url.c_str());
		return false;
	}

	DLLog("[DL] OK     http=%ld  %s", httpCode, outPath.c_str());
	return true;
}



//bool HVKIO::DownloadFile(const std::string& url, const std::string& outPath)
//{
//	CURL* curl = curl_easy_init();
//	if (!curl) return false;
//
//	std::ofstream file(outPath, std::ios::binary);
//	if (!file) return false;
//
//	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
//	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
//	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
//	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
//		+[](void* ptr, size_t size, size_t nmemb, void* stream) -> size_t {
//			std::ofstream* out = static_cast<std::ofstream*>(stream);
//			out->write((char*)ptr, size * nmemb);
//			return size * nmemb;
//		});
//
//	CURLcode res = curl_easy_perform(curl);
//	curl_easy_cleanup(curl);
//	file.close();
//
//	return res == CURLE_OK;
//}

std::string HVKIO::GetLocalAppData()
{
	char path[MAX_PATH];
	SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
	return std::string(path);
}

size_t HVKIO::WriteToString(void* ptr, size_t size, size_t nmemb, void* data)
{
	((std::string*)data)->append((char*)ptr, size * nmemb);
	return size * nmemb;
}

std::string HVKIO::HttpGet(const std::string& url)
{
	CURL* curl = curl_easy_init();
	std::string response;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "PSHVK-Updater");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	return response;
}

void HVKIO::DownloadGithubFolder(const std::string& apiUrl, const fs::path& localPath)
{
	DLLog("[GH] Folder START  api=%s  local=%s", apiUrl.c_str(), localPath.string().c_str());

	fs::create_directories(localPath);

	std::string body = HttpGet(apiUrl);
	if (body.empty())
	{
		DLLog("[GH] Folder FAIL   empty response: %s", apiUrl.c_str());
		return;
	}

	json data;
	try
	{
		data = json::parse(body);
	}
	catch (const std::exception& e)
	{
		DLLog("[GH] Folder FAIL   json parse error: %s  api=%s", e.what(), apiUrl.c_str());
		return;
	}

	if (!data.is_array())
	{
		DLLog("[GH] Folder FAIL   github response not array  api=%s", apiUrl.c_str());
		return;
	}

	DLLog("[GH] Items=%zu  api=%s", (size_t)data.size(), apiUrl.c_str());

	for (auto& item : data)
	{
		std::string type = item.value("type", "");
		std::string name = item.value("name", "");

		if (type.empty() || name.empty())
		{
			DLLog("[GH] Skip invalid item in api=%s", apiUrl.c_str());
			continue;
		}

		fs::path outPath = localPath / name;

		if (type == "file")
		{
			std::string downloadUrl = item.value("download_url", "");
			if (downloadUrl.empty())
			{
				DLLog("[GH] File SKIP missing download_url: %s", outPath.string().c_str());
				continue;
			}

			DLLog("[GH] File  %s", outPath.string().c_str());

			bool ok = DownloadFile(downloadUrl, outPath.string());
			if (!ok)
				DLLog("[GH] File FAIL %s", outPath.string().c_str());
		}
		else if (type == "dir")
		{
			std::string nextApi = item.value("url", "");
			if (nextApi.empty())
			{
				DLLog("[GH] Dir SKIP missing url: %s", outPath.string().c_str());
				continue;
			}

			DLLog("[GH] Dir   %s", outPath.string().c_str());
			DownloadGithubFolder(nextApi, outPath);
		}
		else
		{
			DLLog("[GH] Skip type=%s name=%s", type.c_str(), name.c_str());
		}
	}

	DLLog("[GH] Folder END    local=%s", localPath.string().c_str());
}


//void HVKIO::DownloadGithubFolder(
//	const std::string& apiUrl,
//	const fs::path& localPath)
//{
//	fs::create_directories(localPath);
//
//	auto data = json::parse(HttpGet(apiUrl));
//
//	for (auto& item : data)
//	{
//		std::string type = item["type"];
//		std::string name = item["name"];
//
//		fs::path outPath = localPath / name;
//
//		if (type == "file")
//		{
//			std::string downloadUrl = item["download_url"];
//			DownloadFile(downloadUrl, outPath.string());
//		}
//		else if (type == "dir")
//		{
//			std::string nextApi = item["url"];
//			DownloadGithubFolder(nextApi, outPath);
//		}
//	}
//}

void HVKIO::DownloadPSHVKAssets()
{
	std::string base =
		GetLocalAppData() + "\\PSHVK\\assets";

	std::string api =
		"https://api.github.com/repos/"
		"hav0kdotsys/PSHVK/contents/assets";

	DownloadGithubFolder(api, base);
}

std::wstring HVKIO::GetLocalAppDataW()
{
	wchar_t path[MAX_PATH];
	SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
	return std::wstring(path);
}

void HVKIO::EnsureDirectory(const std::wstring& path)
{
	CreateDirectoryW(path.c_str(), nullptr);
}

const char* HVKIO::GenerateHvkUUID()
{
	static char buffer[36]; // 35 chars + null terminator
	const char charset[] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<> dis(0, 61);

	// Fill first 16 chars
	for (int i = 0; i < 16; ++i)
		buffer[i] = charset[dis(gen)];

	// Insert _HVK_
	buffer[16] = '_';
	buffer[17] = 'H';
	buffer[18] = 'V';
	buffer[19] = 'K';
	buffer[20] = '_';

	// Fill last 16 chars
	for (int i = 0; i < 16; ++i)
		buffer[21 + i] = charset[dis(gen)];

	buffer[35] = '\0'; // null terminator
	return buffer;
}