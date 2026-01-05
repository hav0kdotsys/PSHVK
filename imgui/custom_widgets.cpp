#define IMGUI_DEFINE_MATH_OPERATORS

#include "custom_widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static std::string WStringToUtf8(const std::wstring& w)
{
	if (w.empty())
		return {};

	int size = WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		(int)w.size(),
		nullptr,
		0,
		nullptr,
		nullptr);

	std::string result(size, 0);

	WideCharToMultiByte(
		CP_UTF8,
		0,
		w.data(),
		(int)w.size(),
		result.data(),
		size,
		nullptr,
		nullptr);

	return result;
}

namespace ImGui {

	bool SnapSlider(const std::vector<int>& snapValues, int* v, const char* label)
	{
		if (snapValues.empty() || !v)
			return false;

		int current_index = 0;
		int closest_index = 0;
		int min_distance = INT_MAX;

		for (size_t i = 0; i < snapValues.size(); ++i)
		{
			int distance = abs(snapValues[i] - *v);
			if (distance < min_distance)
			{
				min_distance = distance;
				closest_index = (int)i;
			}
			if (snapValues[i] == *v)
			{
				current_index = (int)i;
				break;
			}
		}

		if (min_distance > 0)
			current_index = closest_index;

		int prev_index = current_index;

		// Display label on the left
		ImGui::Text("%s", label);
		ImGui::SameLine();

		// Create format string to display actual snap value
		char fmt[64];
		snprintf(fmt, sizeof(fmt), "%d", snapValues[current_index]);

		// Use a unique ID for the slider
		char slider_id[128];
		snprintf(slider_id, sizeof(slider_id), "##SnapSlider_%p", (void*)v);

		ImGui::SliderInt(slider_id, &current_index, 0, (int)snapValues.size() - 1, fmt);

		if (current_index != prev_index && current_index >= 0 && current_index < (int)snapValues.size())
		{
			*v = snapValues[current_index];
			return true;
		}

		return false;
	}

	bool SnapSliderFloat(const std::vector<float>& snapValues, float* v, const char* label)
	{
		if (snapValues.empty() || !v)
			return false;

		int current_index = 0;
		int closest_index = 0;
		float min_distance = FLT_MAX;

		for (size_t i = 0; i < snapValues.size(); ++i)
		{
			float distance = fabs(snapValues[i] - *v);
			if (distance < min_distance)
			{
				min_distance = distance;
				closest_index = (int)i;
			}
			if (fabs(snapValues[i] - *v) < 0.0001f)
			{
				current_index = (int)i;
				break;
			}
		}

		if (min_distance > 0.0001f)
			current_index = closest_index;

		int prev_index = current_index;

		// Display label on the left
		ImGui::Text("%s", label);
		ImGui::SameLine();

		// Create format string to display actual snap value
		char fmt[64];
		snprintf(fmt, sizeof(fmt), "%.2f", snapValues[current_index]);

		// Use a unique ID for the slider
		char slider_id[128];
		snprintf(slider_id, sizeof(slider_id), "##SnapSliderFloat_%p", (void*)v);

		ImGui::SliderInt(slider_id, &current_index, 0, (int)snapValues.size() - 1, fmt);

		if (current_index != prev_index && current_index >= 0 && current_index < (int)snapValues.size())
		{
			*v = snapValues[current_index];
			return true;
		}

		return false;
	}

	void Watermark(
		float* fps,
		float* cpuUsage,        // %
		uint64_t* gpuUsedMB,    // MB
		uint64_t* gpuTotalMB,   // MB
		ImVec4 bgColor,
		ImVec4 textColor,
		ImFont* font,
		float baseOpacity,
		float rounding
	)
	{
		if (!fps)
			return;

		ImGuiIO& io = ImGui::GetIO();
		ImDrawList* draw = ImGui::GetBackgroundDrawList();

		// -------------------------------------------------
		// Animation (soft pulse)
		// -------------------------------------------------
		//static float anim = 0.0f;
		//anim += io.DeltaTime * 2.5f; // speed
		//float fade = 0.75f + 0.25f * sinf(anim); // 0.75 → 1.0

		float opacity = baseOpacity;

		// -------------------------------------------------
		// FPS-based color shift
		// -------------------------------------------------
		ImVec4 dynTextColor = textColor;

		if (*fps < 30.0f)
		{
			dynTextColor = ImVec4(1.0f, 0.25f, 0.25f, 1.0f); // red
		}
		else if (*fps < 59.0f)
		{
			dynTextColor = ImVec4(1.0f, 0.6f, 0.2f, 1.0f); // orange
		}

		// -------------------------------------------------
		// Build text
		// -------------------------------------------------
		char text[128];

		if (cpuUsage && gpuUsedMB && gpuTotalMB)
		{
			snprintf(
				text, sizeof(text),
				"FPS: %.0f | CPU: %.1f%% | GPU: %llu / %llu MB",
				*fps,
				*cpuUsage,
				*gpuUsedMB,
				*gpuTotalMB
			);
		}
		else
		{
			snprintf(text, sizeof(text), "FPS: %.0f", *fps);
		}

		// Calculate text size using the specified font or current font
		ImVec2 textSize;
		float fontSize = 0.0f;
		if (font)
		{
			fontSize = font->LegacySize > 0.0f ? font->LegacySize : ImGui::GetFontSize();
			textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, NULL, NULL);
		}
		else
		{
			textSize = ImGui::CalcTextSize(text);
			fontSize = ImGui::GetFontSize();
		}

		// -------------------------------------------------
		// Layout
		// -------------------------------------------------
		const float padX = 14.0f;
		const float padY = 6.0f;

		ImVec2 boxSize(
			textSize.x + padX * 2.0f,
			textSize.y + padY * 2.0f
		);

		ImVec2 pos(
			io.DisplaySize.x * 0.5f - boxSize.x * 0.5f,
			0.0f
		);

		ImVec2 rectMin = pos;
		ImVec2 rectMax = pos + boxSize;

		// -------------------------------------------------
		// Colors + opacity
		// -------------------------------------------------
		bgColor.w *= opacity;
		dynTextColor.w *= opacity;

		ImU32 bgCol = ImGui::GetColorU32(bgColor);
		ImU32 textCol = ImGui::GetColorU32(dynTextColor);

		// -------------------------------------------------
		// Background (bottom corners only)
		// -------------------------------------------------
		draw->PathClear();
		draw->PathRect(
			rectMin,
			rectMax,
			rounding,
			ImDrawFlags_RoundCornersBottomLeft |
			ImDrawFlags_RoundCornersBottomRight
		);
		draw->PathFillConvex(bgCol);

		// -------------------------------------------------
		// Text
		// -------------------------------------------------
		ImVec2 textPos(
			rectMin.x + padX,
			rectMin.y + padY
		);

		if (font)
		{
			draw->AddText(font, fontSize, textPos, textCol, text, NULL);
		}
		else
		{
			draw->AddText(textPos, textCol, text);
		}
	}

	bool DrawPartitionList(
		const std::vector<PartitionInfo>& parts,
		int* selectedIndex)
	{
		if (ImGui::BeginTable("Partitions", 4,
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Borders |
			ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Offset");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Boot");

			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)parts.size(); i++)
			{
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s", BytesToStr(parts[i].Offset));

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", BytesToStr(parts[i].Size));

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s",
					parts[i].Type == PARTITION_STYLE_GPT ? "GPT" :
					parts[i].Type == PARTITION_STYLE_MBR ? "MBR" : "RAW");

				ImGui::TableSetColumnIndex(3);
				ImGui::Text(parts[i].Bootable ? "Yes" : "No");

				if (selectedIndex)
				{
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(i);
					if (ImGui::Selectable("##sel", *selectedIndex == i,
						ImGuiSelectableFlags_SpanAllColumns))
					{
						*selectedIndex = i;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
		return true;
	}

	bool DrawDiskWithPartitions(
		const DiskInfo& disk,
		const std::vector<PartitionInfo>& parts,
		int* selectedPart)
	{
		DrawDiskInfo(disk);
		ImGui::Spacing();
		ImGui::Text("Partitions:");
		return DrawPartitionList(parts, selectedPart);
	}



	bool DrawVolumeList(
		const std::vector<VolumeInfo>& vols,
		int* selectedIndex)
	{
		if (ImGui::BeginTable("Volumes", 5,
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Borders |
			ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Root");
			ImGui::TableSetupColumn("Label");
			ImGui::TableSetupColumn("FS");
			ImGui::TableSetupColumn("Free");
			ImGui::TableSetupColumn("Total");

			ImGui::TableHeadersRow();

			for (int i = 0; i < (int)vols.size(); i++)
			{
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%ls", vols[i].RootPath.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%ls", vols[i].Label.c_str());

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%ls", vols[i].FileSystem.c_str());

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%s", BytesToStr(vols[i].FreeBytes));

				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%s", BytesToStr(vols[i].TotalBytes));

				if (selectedIndex)
				{
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(i);
					if (ImGui::Selectable("##sel", *selectedIndex == i,
						ImGuiSelectableFlags_SpanAllColumns))
					{
						*selectedIndex = i;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndTable();
		}
		return true;
	}

	void DrawDiskSelector(AppState& appstate)
	{
		ImGui::Begin("Disk Selector");

		if (ImGui::BeginTabBar("DiskTabs"))
		{
			// -------------------------------------------------
			// Physical drives
			// -------------------------------------------------
			if (ImGui::BeginTabItem("Physical"))
			{
				for (int i = 0; i < (int)appstate.PhysicalDisks.size(); i++)
				{
					const auto& d = appstate.PhysicalDisks[i];
					bool selected = (appstate.Selection.PhysicalIndex == i);

					ImGui::PushID(i);
					if (ImGui::Selectable("##phys", selected))
					{
						appstate.Selection.PhysicalIndex = i;
						appstate.Selection.VolumeIndex = -1;
						appstate.Selection.PartitionIndex = -1;
					}
					ImGui::SameLine();
					ImGui::Text(
						"PhysicalDrive%d | %ls | %s",
						i,
						d.Model.c_str(),
						BytesToStr(d.SizeBytes)
					);
					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}

			// -------------------------------------------------
			// Volumes
			// -------------------------------------------------
			if (ImGui::BeginTabItem("Disks"))
			{
				for (int i = 0; i < (int)appstate.Volumes.size(); i++)
				{
					const auto& v = appstate.Volumes[i];
					bool selected = (appstate.Selection.VolumeIndex == i);

					ImGui::PushID(i);
					if (ImGui::Selectable("##vol", selected))
					{
						appstate.Selection.VolumeIndex = i;
						appstate.Selection.PhysicalIndex = -1;
						appstate.Selection.PartitionIndex = -1;
					}
					ImGui::SameLine();
					ImGui::Text(
						"%ls [%ls] %s / %s",
						v.RootPath.c_str(),
						v.FileSystem.c_str(),
						BytesToStr(v.FreeBytes),
						BytesToStr(v.TotalBytes)
					);
					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}

			// -------------------------------------------------
			// Partitions
			// -------------------------------------------------
			if (ImGui::BeginTabItem("Partitions"))
			{
				for (int i = 0; i < (int)appstate.Partitions.size(); i++)
				{
					const auto& p = appstate.Partitions[i];
					bool selected = (appstate.Selection.PartitionIndex == i);

					ImGui::PushID(i);
					if (ImGui::Selectable("##part", selected))
					{
						appstate.Selection.PartitionIndex = i;
						appstate.Selection.VolumeIndex = -1;
					}
					ImGui::SameLine();
					ImGui::Text(
						"Partition %d | %s | %s",
						i,
						p.Type == PARTITION_STYLE_GPT ? "GPT" :
						p.Type == PARTITION_STYLE_MBR ? "MBR" : "RAW",
						BytesToStr(p.Size)
					);
					ImGui::PopID();
				}
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::Spacing(12.0f);

		ImGui::Separator();

		ImGui::Spacing(70.5f);

		ImGui::Text(
			"Phys=%d Vol=%d Part=%d Parts=%d",
			appstate.Selection.PhysicalIndex,
			appstate.Selection.VolumeIndex,
			appstate.Selection.PartitionIndex,
			(int)appstate.Partitions.size()
		);

		ImGui::End();
	}




	// Usage:
	// static int delayMs = 250;
	// IntSliderWithEdit("Delay", &delayMs, 0, 5000, "%d ms");

	bool IntSliderWithEdit(const char* label, int* value, int min, int max, const char* format)
	{
		ImGui::PushID(label);

		static bool editing = false;
		static char buffer[64] = {};

		bool changed = false;

		ImGui::BeginGroup();

		float fullWidth = ImGui::CalcItemWidth();
		float buttonWidth = ImGui::GetFrameHeight();
		float sliderWidth = fullWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x;

		ImGui::PushItemWidth(sliderWidth);

		if (!editing)
		{
			if (ImGui::SliderInt("##slider", value, min, max, format))
				changed = true;

			ImGui::SameLine();

			if (ImGui::Button("E", ImVec2(buttonWidth, 0)))
			{
				editing = true;
				snprintf(buffer, sizeof(buffer), "%d", *value);
			}
		}
		else
		{
			if (ImGui::InputText("##edit", buffer, sizeof(buffer),
				ImGuiInputTextFlags_EnterReturnsTrue))
			{
				*value = atoi(buffer);
				*value = ImClamp(*value, min, max);
				editing = false;
				changed = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
			{
				*value = atoi(buffer);
				*value = ImClamp(*value, min, max);
				editing = false;
				changed = true;
			}
		}

		ImGui::PopItemWidth();
		ImGui::EndGroup();
		ImGui::PopID();

		return changed;
	}

	bool DrawDiskInfo(const DiskInfo& d)
	{
		ImGui::BeginChild("DiskInfo", ImVec2(0, 90), true);

		ImGui::Text("Model: %ls", d.Model.c_str());
		ImGui::Text("Serial: %ls", d.Serial.c_str());
		ImGui::Text("Size: %s", BytesToStr(d.SizeBytes));

		ImGui::EndChild();
		return false;
	}

	void DrawCenteredTabs(
		const char* const* labels,
		int count,
		int& activeTab,
		ImVec4 tabbar_text_color,
		ImVec4 tabbar_selected_color,
		float tabbar_inactive_opacity,
		ImFont* satoshi_medium_font,
		ImFont* satoshi_regular_font,
		float horizontalSpacing,
		float verticalPadding)
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Add vertical padding before tabs
		if (verticalPadding > 0.0f)
			ImGui::Spacing(verticalPadding);

		// Calculate total width for centering
		float selectedFontSize = style.FontSizeBase * 1.8f;
		float unselectedFontSize = style.FontSizeBase * 1.7f; // Slightly smaller but still readable
		float totalWidth = 0.0f;
		for (int i = 0; i < count; i++)
		{
			// Use appropriate font and size for width calculation
			ImFont* calcFont = (activeTab == i && satoshi_regular_font) ? satoshi_regular_font : satoshi_medium_font;
			float calcFontSize = (activeTab == i) ? selectedFontSize : unselectedFontSize;
			if (calcFont)
			{
				ImVec2 textSize = calcFont->CalcTextSizeA(calcFontSize, FLT_MAX, 0.0f, labels[i], NULL, NULL);
				totalWidth += textSize.x;
			}
			else
			{
				totalWidth += ImGui::CalcTextSize(labels[i]).x;
			}
			totalWidth += style.FramePadding.x * 2.0f;
			if (i > 0)
				totalWidth += horizontalSpacing;
		}

		float avail = ImGui::GetContentRegionAvail().x;
		float startX = (avail - totalWidth) * 0.5f;
		if (startX > 0.0f)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

		// Store button positions for text rendering
		std::vector<ImVec2> buttonPositions;
		std::vector<ImVec2> buttonSizes;

		// Draw invisible buttons to capture clicks
		for (int i = 0; i < count; i++)
		{
			if (i > 0)
				ImGui::SameLine(0.0f, horizontalSpacing);

			bool selected = (activeTab == i);

			// Use transparent button
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));

			ImVec2 buttonPos = ImGui::GetCursorScreenPos();
			if (ImGui::Button(labels[i]))
				activeTab = i;
			
			ImVec2 buttonSize = ImGui::GetItemRectSize();
			buttonPositions.push_back(buttonPos);
			buttonSizes.push_back(buttonSize);

			ImGui::PopStyleColor(3);
		}

		// Separate selected and unselected tabs - we'll render selected first, then unselected on top
		// This ensures unselected text is never behind selected glow
		struct TabRenderInfo {
			int index;
			ImVec2 buttonPos;
			ImVec2 buttonSize;
		};
		
		std::vector<TabRenderInfo> unselectedTabs;
		TabRenderInfo selectedTab;
		bool hasSelectedTab = false;
		
		// Build arrays: unselected tabs go into array, selected tab is separate
		for (int i = 0; i < count; i++)
		{
			bool selected = (activeTab == i);
			TabRenderInfo info;
			info.index = i;
			info.buttonPos = buttonPositions[i];
			info.buttonSize = buttonSizes[i];
			
			if (selected)
			{
				selectedTab = info;
				hasSelectedTab = true;
			}
			else
			{
				unselectedTabs.push_back(info);
			}
		}
		
		// FIRST: Render the selected tab (with glow) - this goes behind everything
		if (hasSelectedTab)
		{
			int i = selectedTab.index;
			ImFont* fontToUse = satoshi_regular_font;
			if (!fontToUse)
				fontToUse = ImGui::GetFont();

			ImVec4 textColor = tabbar_selected_color;
			textColor.w = 1.0f; // Selected tab always 100% opacity

			ImVec2 buttonPos = selectedTab.buttonPos;
			ImVec2 buttonSize = selectedTab.buttonSize;
			
			// Calculate text position (centered in button)
			ImVec2 textSize;
			if (fontToUse)
			{
				textSize = fontToUse->CalcTextSizeA(selectedFontSize, FLT_MAX, 0.0f, labels[i], NULL, NULL);
			}
			else
			{
				textSize = ImGui::CalcTextSize(labels[i]);
			}
			
			ImVec2 textPos(
				buttonPos.x + (buttonSize.x - textSize.x) * 0.5f,
				buttonPos.y + (buttonSize.y - textSize.y) * 0.5f
			);

			ImU32 textColorU32 = ImGui::GetColorU32(textColor);

			// Render glow effect for selected tab (draw text multiple times with offsets)
			const int glowSteps = 6;
			const float glowSpread = 1.2f;
			for (int step = glowSteps; step >= 1; step--)
			{
				float offset = (float)step * glowSpread / (float)glowSteps;
				float alpha = (1.0f - (float)step / (float)glowSteps) * 0.25f;
				
				ImVec4 glowColor = tabbar_selected_color;
				glowColor.w *= alpha;
				ImU32 glowColorU32 = ImGui::GetColorU32(glowColor);
				
				// Draw glow in multiple directions
				if (fontToUse)
				{
					// drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x - offset, textPos.y), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x + offset, textPos.y), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x, textPos.y - offset), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x, textPos.y + offset), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x - offset * 0.7f, textPos.y - offset * 0.7f), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x + offset * 0.7f, textPos.y - offset * 0.7f), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x - offset * 0.7f, textPos.y + offset * 0.7f), glowColorU32, labels[i], NULL);
					drawList->AddText(fontToUse, selectedFontSize, ImVec2(textPos.x + offset * 0.7f, textPos.y + offset * 0.7f), glowColorU32, labels[i], NULL);
				}
			}
			
			// Draw main selected text on top of glow
			if (fontToUse)
				drawList->AddText(fontToUse, selectedFontSize, textPos, textColorU32, labels[i], NULL);
			else
				drawList->AddText(textPos, textColorU32, labels[i], NULL);
		}
		
		// SECOND: Render unselected tabs ON TOP (so they're never behind selected glow)
		for (const auto& tabInfo : unselectedTabs)
		{
			int i = tabInfo.index;
			ImFont* fontToUse = satoshi_medium_font;
			if (!fontToUse)
				fontToUse = ImGui::GetFont();

			ImVec4 textColor = tabbar_text_color;
			textColor.w *= tabbar_inactive_opacity;

			ImVec2 buttonPos = tabInfo.buttonPos;
			ImVec2 buttonSize = tabInfo.buttonSize;
			
			// Calculate text position (centered in button)
			ImVec2 textSize;
			if (fontToUse)
			{
				textSize = fontToUse->CalcTextSizeA(unselectedFontSize, FLT_MAX, 0.0f, labels[i], NULL, NULL);
			}
			else
			{
				textSize = ImGui::CalcTextSize(labels[i]);
			}
			
			ImVec2 textPos(
				buttonPos.x + (buttonSize.x - textSize.x) * 0.5f,
				buttonPos.y + (buttonSize.y - textSize.y) * 0.5f
			);

			ImU32 textColorU32 = ImGui::GetColorU32(textColor);
			
			// Draw unselected tab text (rendered AFTER selected, so it's on top)
			if (fontToUse)
				drawList->AddText(fontToUse, unselectedFontSize, textPos, textColorU32, labels[i], NULL);
			else
				drawList->AddText(textPos, textColorU32, labels[i], NULL);
		}

		// Add vertical padding after tabs
		if (verticalPadding > 0.0f)
			ImGui::Spacing(verticalPadding);
	}

	// helper to convert char* to wstring
	static std::wstring CharToWString(const char* src)
	{
		if (!src || !*src)
			return L"";

		int len = MultiByteToWideChar(
			CP_UTF8,
			0,
			src,
			-1,
			nullptr,
			0);

		if (len <= 1)
			return L"";

		std::wstring out(len - 1, L'\0');

		MultiByteToWideChar(
			CP_UTF8,
			0,
			src,
			-1,
			out.data(),
			len);

		return out;
	}


	void DrawFormatWidget(AppState& appstate)
	{
		auto& ui = settings->fmtui.g_FormatUI;

		ImGui::BeginChild("FormatRoot", ImVec2(0, 520), true);

		// =========================================================
		// LEFT: Disks + Partitions
		// =========================================================
		ImGui::BeginChild(
			"FormatLeft",
			ImVec2(ImGui::GetContentRegionAvail().x * 0.62f, 0),
			true
		);

		if (ImGui::BeginTable(
			"FormatTable",
			4,
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Borders |
			ImGuiTableFlags_ScrollY |
			ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Index");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Info");
			ImGui::TableHeadersRow();

			// -------------------------
			// Physical disks
			// -------------------------
			for (int i = 0; i < (int)appstate.PhysicalDisks.size(); i++)
			{
				const auto& d = appstate.PhysicalDisks[i];
				bool selected = (ui.SelectedDisk == i);

				ImGui::TableNextRow();
				ImGui::PushID(i);

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Disk");

				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable("##disk", selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					ui.SelectedDisk = i;
					ui.SelectedPartition = -1;

					appstate.Selection.PhysicalIndex = i;
					appstate.Partitions = Disk::ListPartitions(i);
				}
				ImGui::SameLine();
				ImGui::Text("PhysicalDrive%d", i);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", BytesToStr(d.SizeBytes));

				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%ls", d.Model.c_str());

				ImGui::PopID();
			}

			// -------------------------
			// Partitions
			// -------------------------
			for (int i = 0; i < (int)appstate.Partitions.size(); i++)
			{
				const auto& p = appstate.Partitions[i];
				bool selected = (ui.SelectedPartition == i);

				ImGui::TableNextRow();
				ImGui::PushID(1000 + i);

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("Part");

				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable("##part", selected, ImGuiSelectableFlags_SpanAllColumns))
				{
					ui.SelectedPartition = i;
					appstate.Selection.PartitionIndex = i;
				}
				ImGui::SameLine();
				ImGui::Text("%d", i);

				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%s", BytesToStr(p.Size));

				ImGui::TableSetColumnIndex(3);
				ImGui::Text(
					"%s",
					p.Type == PARTITION_STYLE_GPT ? "GPT" :
					p.Type == PARTITION_STYLE_MBR ? "MBR" : "RAW"
				);

				ImGui::PopID();
			}

			ImGui::EndTable();
		}

		ImGui::EndChild();
		ImGui::SameLine();

		// =========================================================
		// RIGHT: Actions / Options
		// =========================================================
		ImGui::BeginChild("FormatRight", ImVec2(0, 0), true);

		bool validDisk =
			ui.SelectedDisk >= 0 &&
			ui.SelectedDisk < (int)appstate.PhysicalDisks.size();

		bool validPart =
			ui.SelectedPartition >= 0 &&
			ui.SelectedPartition < (int)appstate.Partitions.size();

		// -------------------------
		// Unallocated space
		// -------------------------
		uint64_t unallocated = 0;

		if (validDisk)
		{
			uint64_t used = 0;

			for (const auto& p : appstate.Partitions)
			{
				if (p.Size > (1ull << 20)) // ignore EFI / MSR
					used += p.Size;
			}

			uint64_t total = appstate.PhysicalDisks[ui.SelectedDisk].SizeBytes;

			if (used < total)
				unallocated = total - used;
		}
		bool hasUnallocated = unallocated >= (1ull << 20); // >= 1MB


		ImGui::Text("Disk Actions");
		ImGui::Separator();

		// -------------------------
		// Rename volume
		// -------------------------
		bool canRename = validDisk || (appstate.Selection.VolumeIndex >= 0);

		if (canRename)
		{
			ImGui::InputText("Rename Drive", ui.RenameLabel, sizeof(ui.RenameLabel));

			if (ImGui::Button("Rename Volume", ImVec2(-1, 0)))
			{
				wchar_t letter = 0;

				if (ui.SelectedPartition >= 0)
				{
					std::wstring root = Disk::GetPartitionRootPath(
						ui.SelectedDisk,
						appstate.Partitions[ui.SelectedPartition]);
					letter = Disk::ExtractDriveLetter(root);
				}
				else if (appstate.Selection.VolumeIndex >= 0)
				{
					letter = Disk::ExtractDriveLetter(
						appstate.Volumes[appstate.Selection.VolumeIndex].RootPath);
				}
				else if (validDisk)
				{
					letter = Disk::FindAnyDriveLetterForDisk(ui.SelectedDisk);
				}

				if (letter)
				{
					Disk::RenameVolume(
						letter,
						CharToWString(ui.RenameLabel),
						nullptr
					);

					appstate.NeedsRefresh = true;
				}
				else
				{
					ImGui::TextDisabled("No mounted drive letter found.");
				}
			}

			ImGui::Spacing(8.f);
		}


		// -------------------------
		// Wipe / recreate disk
		// -------------------------
		if (!validDisk)
			ImGui::BeginDisabled();

		if (ImGui::Button("Wipe & Recreate Disk", ImVec2(-1, 0)))
			ui.ConfirmRecreate = true;

		if (!validDisk)
			ImGui::EndDisabled();

		ImGui::Spacing(10.f);

		// -------------------------
		// Partition creation
		// -------------------------
		ImGui::Text("Create Partition");
		ImGui::Separator();

		ImGui::Combo("File System", &ui.FileSystem, "NTFS\0exFAT\0FAT32\0");
		ImGui::Checkbox("Quick Format", &ui.QuickFormat);
		ImGui::InputText("Label", ui.VolumeLabel, sizeof(ui.VolumeLabel));

		// display unallocated
		ImGui::Text("Unallocated: %s", BytesToStr(unallocated));

		// convert to GiB for UI
		double maxGiB = (double)unallocated / (1024.0 * 1024.0 * 1024.0);
		static double allocGiB = 0.0;

		if (allocGiB <= 0.0 || allocGiB > maxGiB)
			allocGiB = maxGiB;

		double minGiB = 0.01; // ~10MB

		if (maxGiB < minGiB)
			minGiB = maxGiB;

		ImGui::SliderScalar(
			"Partition Size (GiB)",
			ImGuiDataType_Double,
			&allocGiB,
			&minGiB,
			&maxGiB,
			"%.2f"
		);

		// convert back to MB for DiskPart
		uint64_t allocMB = (uint64_t)(allocGiB * 1024.0);

		if (!validDisk || !hasUnallocated)
			ImGui::BeginDisabled();

		if (ImGui::Button("Create Partition", ImVec2(-1, 0)))
			ui.ConfirmCreatePartition = true;

		if (!validDisk || !hasUnallocated)
			ImGui::EndDisabled();

		ImGui::Spacing(6.f);

		// -------------------------
		// Delete partition
		// -------------------------
		if (!validPart)
			ImGui::BeginDisabled();

		if (ImGui::Button("Delete Partition", ImVec2(-1, 0)))
			ui.ConfirmDeletePartition = true;

		if (!validPart)
			ImGui::EndDisabled();

		ImGui::EndChild();
		ImGui::EndChild();

		// =========================================================
		// CONFIRM: CREATE PARTITION
		// =========================================================
		if (ui.ConfirmCreatePartition)
			ImGui::OpenPopup("Create Partition");

		if (ImGui::BeginPopupModal("Create Partition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped(
				"A new partition will be created using the selected size.\n"
				"All existing data in unallocated space will be consumed."
			);

			ImGui::Separator();

			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ui.ConfirmCreatePartition = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Create", ImVec2(120, 0)))
			{

				Disk::CreatePartition(
					ui.SelectedDisk,
					allocMB,
					FsToString((FileSystem)ui.FileSystem),
					CharToWString(ui.VolumeLabel),
					ui.QuickFormat,
					0,
					nullptr
				);

				appstate.NeedsRefresh = true;
				ui.ConfirmCreatePartition = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		// =========================================================
		// CONFIRM: DELETE PARTITION
		// =========================================================
		if (ui.ConfirmDeletePartition)
			ImGui::OpenPopup("Delete Partition");

		if (ImGui::BeginPopupModal("Delete Partition", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped(
				"This will permanently delete the selected partition.\n"
				"All data will be lost."
			);

			ImGui::Separator();

			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ui.ConfirmDeletePartition = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Delete", ImVec2(120, 0)))
			{
				Disk::DeletePartition(
					ui.SelectedDisk,
					ui.SelectedPartition,
					nullptr
				);

				appstate.NeedsRefresh = true;
				ui.ConfirmDeletePartition = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		// =========================================================
		// CONFIRM: WIPE & RECREATE DISK
		// =========================================================
		if (ui.ConfirmRecreate)
			ImGui::OpenPopup("Recreate Disk");

		if (ImGui::BeginPopupModal("Recreate Disk", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped(
				"THIS WILL COMPLETELY ERASE THE DISK.\n\n"
				"• All partitions deleted\n"
				"• Partition table recreated\n"
				"• Disk reformatted\n\n"
				"THIS CANNOT BE UNDONE."
			);

			static int scheme = 0;
			ImGui::Combo("Partition Scheme", &scheme, "MBR\0GPT\0");

			ImGui::Separator();

			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				ui.ConfirmRecreate = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Proceed", ImVec2(120, 0)))
			{
				Disk::RecreateDiskAndFormat(
					ui.SelectedDisk,
					scheme == 1,
					FsToString((FileSystem)ui.FileSystem),
					CharToWString(ui.VolumeLabel),
					ui.QuickFormat,
					0,
					nullptr
				);

				appstate.NeedsRefresh = true;
				ui.ConfirmRecreate = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		// Refresh Btn
		ImGui::Spacing(12.0f);

		ImGui::Separator();

		ImGui::Spacing(12.0f);

		if (ImGui::Button("Refresh Disks", ImVec2(-1, 0)))
		{
			appstate.NeedsRefresh = true;
			ui.SelectedDisk = -1;
			ui.SelectedPartition = -1;
		}
	}




	bool UpdateStyle(c_usersettings& user, ImGuiStyle& style) {

		style.Colors[ImGuiCol_WindowBg].x = (float)user.style.main_bg_color.x;
		style.Colors[ImGuiCol_WindowBg].y = (float)user.style.main_bg_color.y;
		style.Colors[ImGuiCol_WindowBg].z = (float)user.style.main_bg_color.z;
		style.Colors[ImGuiCol_WindowBg].w = user.style.main_opacity;

		style.Colors[ImGuiCol_Text].x = (float)user.style.main_text_color.x;
		style.Colors[ImGuiCol_Text].y = (float)user.style.main_text_color.y;
		style.Colors[ImGuiCol_Text].z = (float)user.style.main_text_color.z;
		style.Colors[ImGuiCol_Text].w = (float)user.style.main_text_color.w;

		// Apply modern widget styling defaults
		style.FrameRounding = 6.0f;
		style.GrabRounding = 8.0f;
		style.ScrollbarRounding = 9.0f;
		style.TabRounding = 6.0f;
		style.WindowRounding = 8.0f;
		style.ChildRounding = 6.0f;
		style.PopupRounding = 8.0f;
		
		// Better spacing and padding
		style.FramePadding = ImVec2(10.0f, 6.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
		style.WindowPadding = ImVec2(12.0f, 12.0f);
		style.CellPadding = ImVec2(6.0f, 4.0f);
		
		// Enhanced button colors from user settings
		style.Colors[ImGuiCol_Button] = user.style.button_color;
		style.Colors[ImGuiCol_ButtonHovered] = user.style.button_hover_color;
		style.Colors[ImGuiCol_ButtonActive] = user.style.button_active_color;
		
		// Enhanced frame colors
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.75f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.85f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.25f, 0.25f, 0.9f);
		
		// Enhanced slider colors
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

		static float last_applied = 1.0f;
		float desired = user.style.dpi_scale * user.style.ui_scale;

		ImGuiIO& io = ImGui::GetIO();

                const std::string base = HVKIO::GetLocalAppData() + "\\PSHVK\\assets\\fonts\\";
                std::string satoshiRegularPath = base + "satoshi\\Satoshi-Regular.otf";
                std::string satoshiMediumPath = base + "satoshi\\Satoshi-Medium.otf";
                std::string satoshiBoldPath = base + "satoshi\\Satoshi-Bold.otf";
                std::string proggyPath = base + "proggy_clean\\ProggyClean.ttf";

		if (fabsf(desired - last_applied) > 0.001f)
		{
			style.ScaleAllSizes(desired / last_applied);
			last_applied = desired;

			// Use 13px as base to match ImGui's default font size for consistent spacing
			float baseFontSize = 13.0f;
			// Handle ui_scale being 0.0 (default) by using 1.0 as fallback
			float fontSize = baseFontSize * (user.style.ui_scale > 0.0f ? user.style.ui_scale : 1.0f);

                        io.Fonts->Clear();

                        // Load Satoshi Regular font with error handling
                        ImFontConfig satoshiRegularConfig;
                        satoshiRegularConfig.Flags |= ImFontFlags_NoLoadError;
                        user.style.satoshi_regular = io.Fonts->AddFontFromFileTTF(satoshiRegularPath.c_str(), fontSize, &satoshiRegularConfig);
                        if (!user.style.satoshi_regular)
                        {
                                printf("[FONT] UpdateStyle: Failed to load Satoshi Regular font from: %s\n", satoshiRegularPath.c_str());
                                // Fallback to default font if Satoshi Regular fails to load
                                ImFontConfig defaultConfig;
                                defaultConfig.SizePixels = fontSize;
                                user.style.satoshi_regular = io.Fonts->AddFontDefault(&defaultConfig);
                                printf("[FONT] UpdateStyle: Using default font as fallback for Satoshi Regular\n");
                        }
                        else
                        {
                                printf("[FONT] UpdateStyle: Successfully loaded Satoshi Regular font from: %s\n", satoshiRegularPath.c_str());
                        }

                        // Load Satoshi Medium font with error handling
                        ImFontConfig satoshiMediumConfig;
                        satoshiMediumConfig.Flags |= ImFontFlags_NoLoadError;
                        user.style.satoshi_medium = io.Fonts->AddFontFromFileTTF(satoshiMediumPath.c_str(), fontSize, &satoshiMediumConfig);
                        if (!user.style.satoshi_medium)
                        {
                                printf("[FONT] UpdateStyle: Failed to load Satoshi Medium font from: %s\n", satoshiMediumPath.c_str());
                                // Fallback to default font if Satoshi fails to load
                                user.style.satoshi_medium = user.style.satoshi_regular;
                                printf("[FONT] UpdateStyle: Using Satoshi Regular as fallback for Satoshi Medium\n");
                        }
                        else
                        {
                                printf("[FONT] UpdateStyle: Successfully loaded Satoshi Medium font from: %s\n", satoshiMediumPath.c_str());
                        }

                        // Load Satoshi Bold font with error handling
                        ImFontConfig satoshiBoldConfig;
                        satoshiBoldConfig.Flags |= ImFontFlags_NoLoadError;
                        user.style.satoshi_bold = io.Fonts->AddFontFromFileTTF(satoshiBoldPath.c_str(), fontSize, &satoshiBoldConfig);
                        if (!user.style.satoshi_bold)
                        {
                                printf("[FONT] UpdateStyle: Failed to load Satoshi Bold font from: %s\n", satoshiBoldPath.c_str());
                                // Fallback to Satoshi Medium if Bold fails to load
                                user.style.satoshi_bold = user.style.satoshi_medium;
                                printf("[FONT] UpdateStyle: Using Satoshi Medium as fallback for Satoshi Bold\n");
                        }
                        else
                        {
                                printf("[FONT] UpdateStyle: Successfully loaded Satoshi Bold font from: %s\n", satoshiBoldPath.c_str());
                        }

                        // Load Proggy font with error handling
                        ImFontConfig proggyConfig;
                        proggyConfig.Flags |= ImFontFlags_NoLoadError;
                        user.style.proggy_clean = io.Fonts->AddFontFromFileTTF(proggyPath.c_str(), fontSize, &proggyConfig);
                        if (!user.style.proggy_clean)
                        {
                                printf("[FONT] UpdateStyle: Failed to load Proggy font from: %s\n", proggyPath.c_str());
                                // Fallback to default font if Proggy fails to load
                                ImFontConfig defaultConfig;
                                defaultConfig.SizePixels = fontSize;
                                user.style.proggy_clean = io.Fonts->AddFontDefault(&defaultConfig);
                                printf("[FONT] UpdateStyle: Using default font as fallback for Proggy\n");
                        }
                        else
                        {
                                printf("[FONT] UpdateStyle: Successfully loaded Proggy font from: %s\n", proggyPath.c_str());
                        }

                        io.Fonts->Build();

                        // Set satoshi regular as the default font for the menu
                        io.FontDefault = user.style.satoshi_regular;
		}



		return true;
	}

	void Spacing(float height)
	{
		ImGui::Dummy(ImVec2(0.0f, height));
	}

	void HSpacing(float width)
	{
		ImGui::Dummy(ImVec2(width, 0.0f));
		ImGui::SameLine();
	}

	void DrawResolutionWidget(ResolutionUI& g_ResUI)
	{
		ImGui::Text("Display Settings");
		ImGui::Separator();

		// -------------------------
		// Aspect ratio
		// -------------------------
		if (ImGui::Combo(
			"Aspect Ratio",
			&g_ResUI.AspectIndex,
			AspectPresets,
			IM_ARRAYSIZE(AspectPresets)))
		{
			g_ResUI.Filtered =
				Display::UniqueResolutions(
					Display::FilterByAspect(
						g_ResUI.All,
						g_ResUI.AspectIndex));

			g_ResUI.ResolutionIndex = 0;
			Display::UpdateRefreshRates(g_ResUI);
		}

		// -------------------------
		// Resolution list
		// -------------------------
		if (!g_ResUI.Filtered.empty())
		{
			std::vector<std::string> labels;
			labels.reserve(g_ResUI.Filtered.size());

			for (auto& r : g_ResUI.Filtered)
			{
				char buf[64];
				snprintf(buf, sizeof(buf), "%dx%d", r.Width, r.Height);
				labels.emplace_back(buf);
			}

			if (ImGui::ListBox(
				"Resolution",
				&g_ResUI.ResolutionIndex,
				[](void* data, int idx, const char** out)
				{
					auto& v = *(std::vector<std::string>*)data;
					*out = v[idx].c_str();
					return true;
				},
				&labels,
				(int)labels.size(),
				6))
			{
				Display::UpdateRefreshRates(g_ResUI);
			}
		}

		// -------------------------
		// Refresh rate (SnapSlider)
		// -------------------------
		if (!g_ResUI.RefreshRates.empty())
		{
			ImGui::Separator();
			ImGui::SnapSlider(
				g_ResUI.RefreshRates,
				&g_ResUI.SelectedRefresh,
				"Refresh Rate"
			);
			ImGui::SameLine();
			ImGui::Text("%d Hz", g_ResUI.SelectedRefresh);
		}

		ImGui::Separator();

		// -------------------------
		// Apply
		// -------------------------
		if (ImGui::Button("Apply"))
			g_ResUI.ApplyConfirm = true;

		if (g_ResUI.ApplyConfirm)
			ImGui::OpenPopup("Confirm Resolution");

		if (ImGui::BeginPopupModal(
			"Confirm Resolution",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			const auto& base = g_ResUI.Filtered[g_ResUI.ResolutionIndex];

			ImGui::Text(
				"Apply %dx%d @ %d Hz ?",
				base.Width,
				base.Height,
				g_ResUI.SelectedRefresh
			);

			ImGui::Separator();

			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				g_ResUI.ApplyConfirm = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();

			if (ImGui::Button("Apply", ImVec2(120, 0)))
			{
				Resolution r = base;
				r.Refresh = g_ResUI.SelectedRefresh;

				Display::ApplyResolution(r);

				g_ResUI.ApplyConfirm = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool ImGui_FilePicker(
		const char* label,
		std::wstring& inOutPath,
		const wchar_t* autoOpenPath,
		const wchar_t* fileTypes)
	{
		bool changed = false;

		ImGui::Text("%s", label);

		ImGui::SameLine();
		ImGui::BeginGroup();

		// Display current path (trimmed)
		std::string display;
		if (!inOutPath.empty())
			display = WStringToUtf8(inOutPath);
		else
			display = "<none>";

		ImGui::PushItemWidth(-1);
		ImGui::InputText(
			"##path",
			display.data(),
			display.size() + 1,
			ImGuiInputTextFlags_ReadOnly);
		ImGui::PopItemWidth();

		if (ImGui::Button("Browse...", ImVec2(90, 0)))
		{
			wchar_t buffer[MAX_PATH] = {};
			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.lpstrFile = buffer;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrFilter = fileTypes;
			ofn.lpstrInitialDir = autoOpenPath;
			ofn.Flags =
				OFN_EXPLORER |
				OFN_FILEMUSTEXIST |
				OFN_PATHMUSTEXIST;

			if (GetOpenFileNameW(&ofn))
			{
				inOutPath = buffer;
				changed = true;
			}
		}

		ImGui::EndGroup();
		return changed;
	}


	// Modern Widget Styling System Implementation
	namespace ModernStyle {
		
		void PushModernButtonStyle(float rounding, float alpha)
		{
			ImVec4 buttonCol = ::user->style.button_color;
			ImVec4 buttonHoveredCol = ::user->style.button_hover_color;
			ImVec4 buttonActiveCol = ::user->style.button_active_color;
			ImVec4 buttonTextCol = ::user->style.button_text_color;
			
			buttonCol.w *= alpha;
			buttonHoveredCol.w *= alpha;
			buttonActiveCol.w *= alpha;
			
			ImGui::PushStyleColor(ImGuiCol_Button, buttonCol);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHoveredCol);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActiveCol);
			ImGui::PushStyleColor(ImGuiCol_Text, buttonTextCol);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));
		}
		
		void PopModernButtonStyle()
		{
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4); // Button, ButtonHovered, ButtonActive, Text
		}
		
		void PushModernFrameStyle(float rounding, float alpha)
		{
			ImGuiStyle& style = ImGui::GetStyle();
			ImVec4 frameCol = style.Colors[ImGuiCol_FrameBg];
			ImVec4 frameHoveredCol = style.Colors[ImGuiCol_FrameBgHovered];
			ImVec4 frameActiveCol = style.Colors[ImGuiCol_FrameBgActive];
			
			frameCol.w *= alpha;
			frameHoveredCol.w *= alpha;
			frameActiveCol.w *= alpha;
			
			ImGui::PushStyleColor(ImGuiCol_FrameBg, frameCol);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameHoveredCol);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frameActiveCol);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
		}
		
		void PopModernFrameStyle()
		{
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(3);
		}
		
		void PushModernSliderStyle(float rounding)
		{
			ImGuiStyle& style = ImGui::GetStyle();
			ImVec4 grabCol = style.Colors[ImGuiCol_SliderGrab];
			ImVec4 grabActiveCol = style.Colors[ImGuiCol_SliderGrabActive];
			
			ImGui::PushStyleColor(ImGuiCol_SliderGrab, grabCol);
			ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, grabActiveCol);
			ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, rounding);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
		}
		
		void PopModernSliderStyle()
		{
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
		}
		
		void PushModernComboStyle(float rounding, float alpha)
		{
			PushModernFrameStyle(rounding, alpha);
		}
		
		void PopModernComboStyle()
		{
			PopModernFrameStyle();
		}
		
		bool ModernButton(const char* label, const ImVec2& size, float rounding)
		{
			PushModernButtonStyle(rounding, 0.85f);
			bool result = ImGui::Button(label, size);
			PopModernButtonStyle();
			return result;
		}
		
		bool ModernCheckbox(const char* label, bool* v)
		{
			PushModernFrameStyle(4.0f, 0.8f);
			bool result = ImGui::Checkbox(label, v);
			PopModernFrameStyle();
			return result;
		}
		
		bool ModernSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, float rounding)
		{
			PushModernSliderStyle(rounding);
			bool result = ImGui::SliderFloat(label, v, v_min, v_max, format);
			PopModernSliderStyle();
			return result;
		}
		
		bool ModernSliderInt(const char* label, int* v, int v_min, int v_max, const char* format, float rounding)
		{
			PushModernSliderStyle(rounding);
			bool result = ImGui::SliderInt(label, v, v_min, v_max, format);
			PopModernSliderStyle();
			return result;
		}
		
		bool ModernCombo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items, float rounding)
		{
			PushModernComboStyle(rounding, 0.85f);
			bool result = ImGui::Combo(label, current_item, items, items_count, popup_max_height_in_items);
			PopModernComboStyle();
			return result;
		}
		
		bool ModernCombo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items, float rounding)
		{
			PushModernComboStyle(rounding, 0.85f);
			bool result = ImGui::Combo(label, current_item, items_separated_by_zeros, popup_max_height_in_items);
			PopModernComboStyle();
			return result;
		}
		
		bool ModernColorEdit3(const char* label, float col[3], float rounding)
		{
			PushModernFrameStyle(rounding, 0.9f);
			bool result = ImGui::ColorEdit3(label, col);
			PopModernFrameStyle();
			return result;
		}
		
		bool ModernColorEdit4(const char* label, float col[4], float rounding)
		{
			PushModernFrameStyle(rounding, 0.9f);
			bool result = ImGui::ColorEdit4(label, col);
			PopModernFrameStyle();
			return result;
		}
		
		void AddSpacing(float spacing)
		{
			ImGui::Spacing(spacing);
		}
		
	} // namespace ModernStyle

} // namespace ImGui


