#pragma once
#include "imgui_internal.h"
#include "imgui.h"
#include <vector>
#include "../example_win32_directx12/util/disk.h"
#include "../example_win32_directx12/settings.h"
#include "../example_win32_directx12/util/system.h"

static const char* kFileSystems[] = {
	"NTFS",
	"FAT32",
	"exFAT"
};



namespace ImGui {

	/// <summary>
	/// A slider that snaps to preset values instead of allowing smooth dragging.
	/// </summary>
	/// <param name="snapValues">Vector of preset values to snap to (should be sorted)</param>
	/// <param name="v">Pointer to the current value</param>
	/// <param name="label">Label for the slider</param>
	/// <returns>True if the value changed, false otherwise</returns>
	bool SnapSlider(const std::vector<int>& snapValues, int* v, const char* label);

	bool DrawDiskInfo(const DiskInfo& d);
	bool DrawPartitionList(const std::vector<PartitionInfo>& parts, int* selectedIndex = nullptr);
	bool DrawVolumeList(const std::vector<VolumeInfo>& vols, int* selectedIndex = nullptr);
	bool DrawDiskWithPartitions(const DiskInfo& disk, const std::vector<PartitionInfo>& parts, int* selectedPart = nullptr);

	void DrawDiskSelector(AppState& appstate);

	/// <summary>
	/// Float variant of SnapSlider.
	/// </summary>
	bool SnapSliderFloat(const std::vector<float>& snapValues, float* v, const char* label);

	/// <summary>
	/// Draws an FPS watermark at the top center of the screen with rounded bottom corners.
	/// Should be called after ImGui::NewFrame() but before other window rendering.
	/// </summary>
	/// <param name="fps">Pointer to the frames per second value to display</param>
	/// <param name="bg_color">Background color (default: semi-transparent dark)</param>
	/// <param name="text_color">Text color (default: white)</param>
	void Watermark(
		float* fps,
		float* cpuUsage,        // %
		uint64_t* gpuUsedMB,    // MB
		uint64_t* gpuTotalMB,   // MB
		ImVec4 bgColor,
		ImVec4 textColor,
		ImFont* font = nullptr,
		float baseOpacity = 0.85f,
		float rounding = 10.0f
	);

	bool IntSliderWithEdit(const char* label, int* value, int min, int max, const char* format);
	void DrawCenteredTabs(
		const char* const* labels,
		int count,
		int& activeTab,
		ImVec4 tabbar_text_color,
		ImVec4 tabbar_selected_color,
		float tabbar_inactive_opacity,
		ImFont* satoshi_medium_font,
		ImFont* satoshi_regular_font,
		float horizontalSpacing = 8.0f,
		float verticalPadding = 0.0f
	);
	void DrawFormatWidget(AppState& appstate);
	void DrawResolutionWidget(ResolutionUI& g_ResUI);


	bool UpdateStyle(c_usersettings& user, ImGuiStyle& style);
	void Spacing(float height);
	void HSpacing(float width);

	bool ImGui_FilePicker(
		const char* label,
		std::wstring& inOutPath,
		const wchar_t* autoOpenPath,
		const wchar_t* fileTypes   // e.g. L"Images (*.png;*.jpg)\0*.png;*.jpg\0"
	);

	// Modern Widget Styling System
	namespace ModernStyle {
		// Push modern style for widgets (rounded corners, better spacing, transparency)
		void PushModernButtonStyle(float rounding = 6.0f, float alpha = 0.85f);
		void PopModernButtonStyle();
		
		void PushModernFrameStyle(float rounding = 6.0f, float alpha = 0.75f);
		void PopModernFrameStyle();
		
		void PushModernSliderStyle(float rounding = 8.0f);
		void PopModernSliderStyle();
		
		void PushModernComboStyle(float rounding = 6.0f, float alpha = 0.85f);
		void PopModernComboStyle();

		// Modern widget wrappers
		bool ModernButton(const char* label, const ImVec2& size = ImVec2(0, 0), float rounding = 6.0f);
		bool ModernCheckbox(const char* label, bool* v);
		bool ModernSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", float rounding = 8.0f);
		bool ModernSliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d", float rounding = 8.0f);
		bool ModernCombo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1, float rounding = 6.0f);
		bool ModernCombo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1, float rounding = 6.0f);
		bool ModernColorEdit3(const char* label, float col[3], float rounding = 6.0f);
		bool ModernColorEdit4(const char* label, float col[4], float rounding = 6.0f);
		
		// Apply modern spacing between widgets
		void AddSpacing(float spacing = 8.0f);
	}

} // namespace ImGui
