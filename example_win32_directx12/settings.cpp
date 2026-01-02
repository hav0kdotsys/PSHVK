#include "settings.h"
#include <sstream>

struct HvkJsonWriter
{
	std::ostringstream ss;
	int indent = 0;

	void Indent()
	{
		for (int i = 0; i < indent; ++i)
			ss << '\t';
	}

	void BeginObject()
	{
		ss << "{\n";
		indent++;
	}

	void EndObject(bool comma = false)
	{
		ss << "\n";
		indent--;
		Indent();
		ss << "}";
		if (comma) ss << ",";
		ss << "\n";
	}

	void Key(const char* key)
	{
		Indent();
		ss << "\"" << key << "\": ";
	}

	void String(const std::string& v, bool comma = true)
	{
		ss << "\"";
		for (char c : v)
		{
			switch (c)
			{
			case '\\': ss << "\\\\"; break;
			case '"':  ss << "\\\""; break;
			case '\n': ss << "\\n";  break;
			case '\r': ss << "\\r";  break;
			case '\t': ss << "\\t";  break;
			default:
				ss << c;
				break;
			}
		}
		ss << "\"";
		if (comma) ss << ",";
		ss << "\n";
	}


	void WString(const std::wstring& v, bool comma = true)
	{
		String(std::string(v.begin(), v.end()), comma);
	}

	void Bool(bool v, bool comma = true)
	{
		ss << (v ? "true" : "false");
		if (comma) ss << ",";
		ss << "\n";
	}

	template<typename T>
	void Number(T v, bool comma = true)
	{
		ss << v;
		if (comma) ss << ",";
		ss << "\n";
	}
};

static void WriteImVec4(HvkJsonWriter& w, const ImVec4& v, bool comma = true)
{
	w.ss << "[ "
		<< v.x << ", "
		<< v.y << ", "
		<< v.z << ", "
		<< v.w << " ]";
	if (comma) w.ss << ",";
	w.ss << "\n";
}

void c_usersettings::ExportToHvk(const std::wstring path)
{
	std::wstring dir = HVKIO::GetLocalAppDataW() + L"\\PSHVK";
	HVKIO::EnsureDirectory(dir);

	HvkJsonWriter w;
	w.BeginObject();

	// render
	w.Key("render");
	w.BeginObject();

	w.Key("wm_render_interval");
	w.Number(user->render.wm_render_interval);

	w.Key("target_fps");
	w.Number(user->render.target_fps);

	w.Key("bg_image_path");
	w.WString(user->render.bg_image_path, false);

	w.EndObject(true);

	// binds
	w.Key("binds");
	w.BeginObject();

	w.Key("toggle_main");
	w.Number(user->binds.toggle_main);

	w.Key("toggle_dev");
	w.Number(user->binds.toggle_dev);

	w.Key("shutdown");
	w.Number(user->binds.shutdown, false);

	w.EndObject(true);

	// style
	w.Key("style");
	w.BeginObject();

	w.Key("wm_bg_color");
	WriteImVec4(w, user->style.wm_bg_color);

	w.Key("wm_text_color");
	WriteImVec4(w, user->style.wm_text_color);

	w.Key("wm_opacity");
	w.Number(user->style.wm_opacity);

	w.Key("main_bg_color");
	WriteImVec4(w, user->style.main_bg_color);

	w.Key("main_text_color");
	WriteImVec4(w, user->style.main_text_color);

	w.Key("main_border_color");
	WriteImVec4(w, user->style.main_border_color);

	w.Key("main_opacity");
	w.Number(user->style.main_opacity);

	w.Key("tabbar_text_color");
	WriteImVec4(w, user->style.tabbar_text_color);

	w.Key("tabbar_selected_color");
	WriteImVec4(w, user->style.tabbar_selected_color);

	w.Key("tabbar_inactive_opacity");
	w.Number(user->style.tabbar_inactive_opacity);

	w.Key("button_color");
	WriteImVec4(w, user->style.button_color);

	w.Key("button_text_color");
	WriteImVec4(w, user->style.button_text_color);

	w.Key("button_hover_color");
	WriteImVec4(w, user->style.button_hover_color);

	w.Key("button_hover_text_color");
	WriteImVec4(w, user->style.button_hover_text_color);

	w.Key("button_active_color");
	WriteImVec4(w, user->style.button_active_color);

	w.Key("loading_theme");
	w.Number((int)user->style.loading_theme);

	w.Key("bg_theme");
	w.Number((int)user->style.bg_theme);

	w.Key("main_secondary_color");
	WriteImVec4(w, user->style.main_secondary_color, false); // <-- last item

	w.EndObject(false);
	w.EndObject();

	std::string out = w.ss.str();

	HANDLE hFile = CreateFileW(
		path.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return;

	DWORD written;
	WriteFile(hFile, out.data(), (DWORD)out.size(), &written, nullptr);
	CloseHandle(hFile);
}


void c_settings::ExportToHvk(const std::wstring path)
{
	std::wstring dir = HVKIO::GetLocalAppDataW() + L"\\PSHVK";
	HVKIO::EnsureDirectory(dir);

	HvkJsonWriter w;
	w.BeginObject();

	w.Key("is_first_run");
	w.Bool(settings->is_first_run);

	w.Key("g_MainTab");
	w.Number(settings->g_MainTab);

	w.Key("vsync");
	w.Bool(settings->vsync);

	w.Key("isLoading");
	w.Bool(settings->isLoading);

	// visibility
	w.Key("visibility");
	w.BeginObject();

	w.Key("win_main");
	w.Bool(settings->visibility.win_main);

	w.Key("win_dev");
	w.Bool(settings->visibility.win_dev);

	w.Key("win_selector");
	w.Bool(settings->visibility.win_selector);

	w.Key("disk_info");
	w.Bool(settings->visibility.disk_info);

	w.Key("part_info");
	w.Bool(settings->visibility.part_info);

	w.Key("disk_and_part_info");
	w.Bool(settings->visibility.disk_and_part_info, false);

	w.EndObject(true);

	// format ui
	w.Key("format_ui");
	w.BeginObject();

	auto& ui = settings->fmtui.g_FormatUI;

	w.Key("SelectedDisk");
	w.Number(ui.SelectedDisk);

	w.Key("SelectedPartition");
	w.Number(ui.SelectedPartition);

	w.Key("VolumeLabel");
	w.String(ui.VolumeLabel);

	w.Key("FileSystem");
	w.Number(ui.FileSystem);

	w.Key("QuickFormat");
	w.Bool(ui.QuickFormat);

	w.Key("ConfirmPopup");
	w.Bool(ui.ConfirmPopup);

	w.Key("ConfirmRecreate");
	w.Bool(ui.ConfirmRecreate);

	w.Key("ConfirmCreatePartition");
	w.Bool(ui.ConfirmCreatePartition);

	w.Key("ConfirmDeletePartition");
	w.Bool(ui.ConfirmDeletePartition);

	w.Key("RenameLabel");
	w.String(ui.RenameLabel, false);

	w.EndObject(true);

	// theme combos
	w.Key("themecombos");
	w.BeginObject();

	w.Key("LoadingThemeIdx");
	w.Number(settings->themecombos.LoadingThemeIdx);

	w.Key("BgThemeIdx");
	w.Number(settings->themecombos.BgThemeIdx, false);

	w.EndObject(false);
	w.EndObject();

	std::string out = w.ss.str();

	HANDLE hFile = CreateFileW(
		path.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return;

	DWORD written;
	WriteFile(hFile, out.data(), (DWORD)out.size(), &written, nullptr);
	CloseHandle(hFile);
}

static bool ReadFileToString(const std::wstring& path, std::string& out)
{
	HANDLE hFile = CreateFileW(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD size = GetFileSize(hFile, nullptr);
	if (size == INVALID_FILE_SIZE || size == 0)
	{
		CloseHandle(hFile);
		return false;
	}

	out.resize(size);
	DWORD read = 0;
	ReadFile(hFile, out.data(), size, &read, nullptr);
	CloseHandle(hFile);

	return read == size;
}

static ImVec4 ReadImVec4(const json& j, const ImVec4& fallback)
{
	if (!j.is_array() || j.size() != 4)
		return fallback;

	// must all be numbers
	for (int i = 0; i < 4; ++i)
		if (!j[i].is_number())
			return fallback;

	ImVec4 out = fallback;

	out.x = (float)j[0].get<double>();
	out.y = (float)j[1].get<double>();
	out.z = (float)j[2].get<double>();
	out.w = (float)j[3].get<double>();

	return out;
}


bool c_usersettings::ImportFromHvk(const std::wstring& path)
{
	printf("User Settings Import Requested. \n\n");

	std::string text;
	if (!ReadFileToString(path, text))
	{
		printf("Failed to read path.\n\n");
		return false;
	}

	json j;
	try {
		printf("First bytes: %02X %02X %02X\n\n",
			(unsigned char)text[0],
			(unsigned char)text[1],
			(unsigned char)text[2]);

		j = json::parse(text);
	}
	catch (const std::exception& e) {
		printf("JSON parse error: %s\n", e.what());
		return false;
	}


	if (j.contains("render"))
	{
		auto& r = j["render"];
		if (r.contains("wm_render_interval")) user->render.wm_render_interval = r["wm_render_interval"];
		if (r.contains("target_fps"))         user->render.target_fps = r["target_fps"];
		if (r.contains("bg_image_path"))
		{
			std::string s = r["bg_image_path"];
			user->render.bg_image_path = std::wstring(s.begin(), s.end());
		}
	}

	if (j.contains("binds"))
	{
		auto& b = j["binds"];
		if (b.contains("toggle_main")) user->binds.toggle_main = b["toggle_main"];
		if (b.contains("toggle_dev"))  user->binds.toggle_dev = b["toggle_dev"];
		if (b.contains("shutdown"))    user->binds.shutdown = b["shutdown"];
	}

	if (j.contains("style"))
	{
		auto& s = j["style"];

		if (s.contains("wm_bg_color"))
			user->style.wm_bg_color = ReadImVec4(s["wm_bg_color"], user->style.wm_bg_color);

		if (s.contains("wm_text_color"))
			user->style.wm_text_color = ReadImVec4(s["wm_text_color"], user->style.wm_text_color);


		if (s.contains("wm_opacity"))user->style.wm_opacity = s["wm_opacity"].get<float>();

		if (s.contains("main_bg_color"))
			user->style.main_bg_color = ReadImVec4(s["main_bg_color"], user->style.main_bg_color);

		if (s.contains("main_text_color"))
			user->style.main_text_color = ReadImVec4(s["main_text_color"], user->style.main_text_color);

		if (s.contains("main_border_color"))
			user->style.main_border_color = ReadImVec4(s["main_border_color"], user->style.main_border_color);

		if (s.contains("main_opacity")) user->style.main_opacity = s["main_opacity"].get<float>();

		if (s.contains("loading_theme"))
			user->style.loading_theme = (LoadingTheme)s["loading_theme"].get<int>();

		if (s.contains("bg_theme"))
			user->style.bg_theme = (BgTheme)s["bg_theme"].get<int>();

		if (s.contains("main_secondary_color"))
			user->style.main_secondary_color = ReadImVec4(s["main_secondary_color"], user->style.main_secondary_color);
	}

	OutputDebugStringA("[HVK] ImportFromHvk(user) AFTER import:\n");
	{
		char buf[512];
		sprintf_s(buf, "main_bg_color=%.3f %.3f %.3f %.3f  main_opacity=%.3f\n",
			user->style.main_bg_color.x, user->style.main_bg_color.y,
			user->style.main_bg_color.z, user->style.main_bg_color.w,
			user->style.main_opacity);
		OutputDebugStringA(buf);
	}

	return true;
}

bool c_settings::ImportFromHvk(const std::wstring& path)
{
	std::string text;
	if (!ReadFileToString(path, text))
		return false;

	json j;
	try {
		j = json::parse(text);
	}
	catch (...) {
		return false;
	}

	if (j.contains("is_first_run")) settings->is_first_run = j["is_first_run"];
	if (j.contains("g_MainTab"))    settings->g_MainTab = j["g_MainTab"];
	if (j.contains("vsync"))        settings->vsync = j["vsync"];
	if (j.contains("isLoading"))    settings->isLoading = j["isLoading"];

	if (j.contains("visibility"))
	{
		auto& v = j["visibility"];
		if (v.contains("win_main")) settings->visibility.win_main = v["win_main"];
		if (v.contains("win_dev")) settings->visibility.win_dev = v["win_dev"];
		if (v.contains("win_selector")) settings->visibility.win_selector = v["win_selector"];
		if (v.contains("disk_info")) settings->visibility.disk_info = v["disk_info"];
		if (v.contains("part_info")) settings->visibility.part_info = v["part_info"];
		if (v.contains("disk_and_part_info"))
			settings->visibility.disk_and_part_info = v["disk_and_part_info"];
	}

	if (j.contains("format_ui"))
	{
		auto& f = j["format_ui"];
		auto& ui = settings->fmtui.g_FormatUI;

		if (f.contains("SelectedDisk")) ui.SelectedDisk = f["SelectedDisk"];
		if (f.contains("SelectedPartition")) ui.SelectedPartition = f["SelectedPartition"];
		if (f.contains("VolumeLabel"))
			strncpy_s(ui.VolumeLabel, f["VolumeLabel"].get<std::string>().c_str(), sizeof(ui.VolumeLabel));
		if (f.contains("FileSystem")) ui.FileSystem = f["FileSystem"];
		if (f.contains("QuickFormat")) ui.QuickFormat = f["QuickFormat"];
		if (f.contains("ConfirmPopup")) ui.ConfirmPopup = f["ConfirmPopup"];
		if (f.contains("ConfirmRecreate")) ui.ConfirmRecreate = f["ConfirmRecreate"];
		if (f.contains("ConfirmCreatePartition")) ui.ConfirmCreatePartition = f["ConfirmCreatePartition"];
		if (f.contains("ConfirmDeletePartition")) ui.ConfirmDeletePartition = f["ConfirmDeletePartition"];
		if (f.contains("RenameLabel"))
			strncpy_s(ui.RenameLabel, f["RenameLabel"].get<std::string>().c_str(), sizeof(ui.RenameLabel));
	}

	if (j.contains("themecombos"))
	{
		auto& t = j["themecombos"];
		if (t.contains("LoadingThemeIdx")) settings->themecombos.LoadingThemeIdx = t["LoadingThemeIdx"];
		if (t.contains("BgThemeIdx")) settings->themecombos.BgThemeIdx = t["BgThemeIdx"];
	}

	return true;
}


#pragma pack(push, 1)
struct HVKSignature
{
	char     magic[6];   // "HVKSIG"
	uint16_t version;    // for future changes
	uint8_t  hash[32];   // SHA-256
	// uint8_t uuidhash[32]; // SHA-256 (HVKUUID)
};
#pragma pack(pop)


#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

static bool Sha256(const void* data, size_t size, uint8_t out[32])
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_HASH_HANDLE hHash = nullptr;
	DWORD hashLen = 32;
	DWORD cb = 0;

	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
		return false;

	if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))
	{
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return false;
	}

	BCryptHashData(hHash, (PUCHAR)data, (ULONG)size, 0);
	BCryptFinishHash(hHash, out, hashLen, 0);

	BCryptDestroyHash(hHash);
	BCryptCloseAlgorithmProvider(hAlg, 0);
	return true;
}


bool HVKIO::CreateInstanceFile()
{
	std::wstring base = GetLocalAppDataW();
	std::wstring dir = base + L"\\PSHVK";
	std::wstring file = dir + L"\\instance.hvk";

	EnsureDirectory(dir);

	settings->metadata.uuid = GenerateHvkUUID();

	// --- build JSON ---
	HvkJsonWriter w;
	w.BeginObject();

	w.Key("app");
	w.String("PSHVK");

	w.Key("uuid");
	w.String(settings->metadata.uuid);

	w.Key("created_unix");
	w.Number((uint64_t)time(nullptr), false);

	w.EndObject();

	std::string jsonText = w.ss.str();

	// --- build signature ---
	HVKSignature sig{};
	memcpy(sig.magic, "HVKSIG", 6);
	sig.version = 1;

	if (!Sha256(jsonText.data(), jsonText.size(), sig.hash))
		return false;

	/*if (!Sha256(settings->metadata.uuid, sizeof(settings->metadata.uuid), sig.uuidhash))
		return false;*/

	// --- write file ---
	HANDLE hFile = CreateFileW(
		file.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	DWORD written = 0;

	// write JSON
	WriteFile(hFile, jsonText.data(), (DWORD)jsonText.size(), &written, nullptr);

	// separator
	char zero = 0;
	WriteFile(hFile, &zero, 1, &written, nullptr);

	// write signature block
	WriteFile(hFile, &sig, sizeof(sig), &written, nullptr);

	CloseHandle(hFile);
	return true;
}

bool HVKIO::ValidateInstanceFile()
{
	std::wstring base = GetLocalAppDataW();
	std::wstring file = base + L"\\PSHVK\\instance.hvk";

	HANDLE hFile = CreateFileW(
		file.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER fileSize{};
	if (!GetFileSizeEx(hFile, &fileSize))
	{
		CloseHandle(hFile);
		return false;
	}

	if (fileSize.QuadPart <= (LONGLONG)sizeof(HVKSignature) + 1)
	{
		CloseHandle(hFile);
		return false;
	}

	std::vector<uint8_t> data((size_t)fileSize.QuadPart);
	DWORD read = 0;

	if (!ReadFile(hFile, data.data(), (DWORD)data.size(), &read, nullptr) ||
		read != data.size())
	{
		CloseHandle(hFile);
		return false;
	}

	CloseHandle(hFile);

	// signature is at the end
	const size_t sigOffset = data.size() - sizeof(HVKSignature);
	const HVKSignature* sig =
		reinterpret_cast<const HVKSignature*>(data.data() + sigOffset);

	// validate magic
	if (memcmp(sig->magic, "HVKSIG", 6) != 0)
		return false;

	// validate version
	if (sig->version != 1)
		return false;

	// JSON must end before the zero separator
	size_t jsonEnd = sigOffset;
	if (jsonEnd == 0 || data[jsonEnd - 1] != 0)
		return false;

	jsonEnd--; // exclude separator

	// recompute hash
	uint8_t hash[32]{};
	if (!Sha256(data.data(), jsonEnd, hash))
		return false;

	/*uint8_t uuidhash[32]{};
	if (!Sha256(settings->metadata.uuid, sizeof(settings->metadata.uuid), uuidhash))
		return false;*/

	// compare hashes
	if (memcmp(hash, sig->hash, 32) != 0)
		return false;

	return true;
}

