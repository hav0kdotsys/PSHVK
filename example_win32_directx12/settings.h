#pragma once
#include "util/web_helper.h"
#include "util/disk.h"
#include "thread"

#ifdef _DEV
#define DEV_BUILD  1
#else
#define DEV_BUILD 0
#endif


struct WatermarkStats
{
	float fps = 0.0f;
	float cpu = 0.0f;
	uint64_t gpuUsedMB = 0;
	uint64_t gpuTotalMB = 0;
};

struct FormatUIState
{
	int SelectedDisk = -1;
	int SelectedPartition = -1;

	char VolumeLabel[32] = "";
	int FileSystem = 0; // 0=NTFS 1=FAT32 2=exFAT
	bool QuickFormat = true;

	bool ConfirmPopup = false;
	bool ConfirmRecreate = false;
	bool ConfirmCreatePartition = false;
	bool ConfirmDeletePartition = false;

	char RenameLabel[32] = "";
};

struct LoadingCache {
	bool vsync = false;
	int target_fps = 60;
};

enum class RenderBackend
{
	DX12,
	DX11
};

struct AppState
{
	std::vector<DiskInfo>      PhysicalDisks;
	std::vector<VolumeInfo>    Volumes;
	std::vector<PartitionInfo> Partitions;

	DiskSelection Selection;
	LoadingCache Lcache;
	RenderBackend g_RenderBackend = RenderBackend::DX11;

	bool NeedsRefresh = true;
};

enum class LoadingTheme 
{
	DARKMODE,
	LIGHTMODE
};

enum class BgTheme 
{
	BLACK,
	PURPLE,
	YELLOW, 
	BLUE,
	GREEN,
	RED
};

class c_usersettings {
public:

	static void ExportToHvk(const std::wstring path);
	static bool ImportFromHvk(const std::wstring& path);

	
	struct {
		int wm_render_interval = 1000; // milliseconds
		int target_fps = 60;
		std::wstring bg_image_path = HVKIO::GetLocalAppDataW() + L"\\PSHVK\\assets\\Galaxy_Purple.png";
	} render;

	struct {

		int toggle_main = VK_INSERT;
		int toggle_dev = VK_F2;
		int shutdown = VK_END;

	} binds;
		
	struct {

		// Watermark //
		ImVec4 wm_bg_color = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
		ImVec4 wm_text_color = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
		float wm_opacity = 0.85f;

		// Main Window //
		ImVec4 main_bg_color = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
		ImVec4 main_text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImVec4 main_border_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		float main_opacity = 1.0f;

		// Main Secondary Color (tied to bg_theme) //
		ImVec4 main_secondary_color = ImVec4(0.5f, 0.2f, 0.8f, 1.0f); // Purple (default for PURPLE theme)

		// Tab Bar //
		ImVec4 tabbar_text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImVec4 tabbar_selected_color = main_secondary_color;
		float tabbar_inactive_opacity = 1.0f;

		// Widget Colors //
		ImVec4 button_color = ImVec4(0.2f, 0.2f, 0.2f, 0.8f);
		ImVec4 button_text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImVec4 button_hover_color = ImVec4(0.3f, 0.3f, 0.3f, 0.9f);
		ImVec4 button_hover_text_color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		ImVec4 button_active_color = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);

		// BG & Loading Textures //
		LoadingTheme loading_theme = LoadingTheme::DARKMODE;
		BgTheme bg_theme = BgTheme::PURPLE;

		// Scale // 
		float dpi_scale = 0.0f;
		float ui_scale = 0.0f;

		// Fonts //
		ImFont* proggy_clean = (ImFont*)nullptr;
		ImFont* satoshi_regular = (ImFont*)nullptr;
		ImFont* satoshi_medium = (ImFont*)nullptr;
		ImFont* satoshi_bold = (ImFont*)nullptr;


	} style;
};

class c_settings {
public:

	static void ExportToHvk(const std::wstring path);
	static bool ImportFromHvk(const std::wstring& path);


	bool is_first_run = false;

	int g_MainTab = 0; // 0=Home, 1=Format, 2=Settings, 3=Colors

	bool vsync = true;
	float* fps = nullptr;
	bool isLoading = false;

	struct {
		bool win_main = true;
		bool win_dev = DEV_BUILD;
		bool win_selector = false;

		bool disk_info = false;
		bool part_info = false;
		bool disk_and_part_info = false;

	} visibility;

	struct {

		FormatUIState g_FormatUI;

	} fmtui;

	struct {

		int LoadingThemeIdx = 0;
		int BgThemeIdx = 1;

	} themecombos;

	struct {
		
		const char* uuid;
		bool dev_build = DEV_BUILD ? true : false;

	} metadata;

};


inline c_usersettings* user = new c_usersettings();
inline c_settings* settings = new c_settings();