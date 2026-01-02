#define IMGUI_DEFINE_MATH_OPERATORS

#include "hvk_gui.h"
#include "imgui_internal.h"
#include "../example_win32_directx12/settings.h"
#include "hvk_emissive.h"

// Forward declarations from main.cpp
extern AppState g_App;

namespace HvkGui
{
        namespace
        {
                static ImVector<HvkEmissiveBinding> g_EmissiveBindings;
                static int g_LastFrame = -1;

                const HvkEmissiveBinding* AllocateEmissiveBinding(ImTextureID base, const EmissiveImageOptions* opts)
                {
                        if (!opts || opts->emissiveTexture == nullptr || opts->emissiveStrength <= 0.0f)
                                return nullptr;

                        ImGuiContext* ctx = ImGui::GetCurrentContext();
                        const int frame = ctx ? ctx->FrameCount : 0;
                        if (frame != g_LastFrame)
                        {
                                g_EmissiveBindings.clear();
                                g_LastFrame = frame;
                        }

                        HvkEmissiveBinding binding;
                        binding.BaseTexture = base;
                        binding.EmissiveTexture = opts->emissiveTexture;
                        binding.EmissiveStrength = opts->emissiveStrength;
                        binding.Additive = opts->additiveBlend;

                        g_EmissiveBindings.push_back(binding);
                        return &g_EmissiveBindings.back();
                }
        }

        // ====================================================================
        // TEXT RENDERING - GLOW EFFECTS
        // ====================================================================

	void GlowText(
		ImFont* font,
		float fontSize,
		ImU32 color,
		const char* text,
		ImU32 glowColor,
		float glowSize,
		float glowIntensity
	)
	{
		if (!text)
			return;

		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;

		ImGuiContext& g = *GImGui;
		
		// Get current cursor position (like ImGui::Text does, accounting for CurrLineTextBaseOffset)
		ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
		
		// Calculate text size
		ImVec2 textSize;
		if (font)
			textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, NULL, NULL);
		else
			textSize = ImGui::CalcTextSize(text);
		
		// ItemSize to reserve space (like ImGui::Text does)
		ImGui::ItemSize(textSize, 0.0f);
		
		// Get draw list
		ImDrawList* drawList = window->DrawList;
		if (!drawList)
			return;

		// Clamp glow intensity
		glowIntensity = ImClamp(glowIntensity, 0.0f, 1.0f);

		// Number of glow layers (more layers = smoother glow, but more expensive)
		const int glowLayers = 12;
		const float layerStep = glowSize / (float)glowLayers;

		// Render glow layers first (behind the text)
		// Use a Gaussian-like falloff for smoother glow
		for (int layer = glowLayers; layer >= 1; layer--)
		{
			float currentSize = layerStep * (float)layer;
			float alpha = (1.0f - ((float)layer / (float)glowLayers)) * glowIntensity;
			
			// Extract alpha from glowColor and apply layer alpha
			ImU32 layerColor = glowColor;
			int alphaChannel = (int)(alpha * 255.0f);
			layerColor = (layerColor & 0x00FFFFFF) | (alphaChannel << 24);

			// Render text at multiple offset positions to create circular glow
			const int directions = 8; // 8 directions for smooth circular glow
			for (int dir = 0; dir < directions; dir++)
			{
				float angle = (float)dir * (IM_PI * 2.0f / (float)directions);
				float offsetX = cosf(angle) * currentSize;
				float offsetY = sinf(angle) * currentSize;
				
				ImVec2 offsetPos(pos.x + offsetX, pos.y + offsetY);
				
				if (font)
					drawList->AddText(font, fontSize, offsetPos, layerColor, text, NULL);
				else
					drawList->AddText(offsetPos, layerColor, text);
			}

			// Also add diagonal offsets for even smoother glow
			for (int dir = 0; dir < 4; dir++)
			{
				float angle = (float)dir * (IM_PI * 2.0f / 4.0f) + (IM_PI / 4.0f);
				float offsetX = cosf(angle) * currentSize * 0.7f;
				float offsetY = sinf(angle) * currentSize * 0.7f;
				
				ImVec2 offsetPos(pos.x + offsetX, pos.y + offsetY);
				
				if (font)
					drawList->AddText(font, fontSize, offsetPos, layerColor, text, NULL);
				else
					drawList->AddText(offsetPos, layerColor, text);
			}
		}

		// Render the main text on top (without glow)
		if (font)
			drawList->AddText(font, fontSize, pos, color, text, NULL);
		else
			drawList->AddText(pos, color, text);
		
		// Cursor is already advanced by ItemSize() above, just like ImGui::Text()
	}

	void GlowText(
		ImFont* font,
		float fontSize,
		const ImVec4& color,
		const char* text,
		const ImVec4& glowColor,
		float glowSize,
		float glowIntensity
	)
	{
		GlowText(font, fontSize, ImGui::ColorConvertFloat4ToU32(color), text,
			ImGui::ColorConvertFloat4ToU32(glowColor), glowSize, glowIntensity);
	}

	void GlowText(
		ImFont* font,
		float fontSize,
		ImU32 color,
		const std::string& text,
		ImU32 glowColor,
		float glowSize,
		float glowIntensity
	)
	{
		GlowText(font, fontSize, color, text.c_str(), glowColor, glowSize, glowIntensity);
	}

	// ====================================================================
	// TEXTURE WIDGETS
	// ====================================================================

        bool ImageRounded(
                ImTextureID textureId,
                const ImVec2& size,
                const ImVec2& uv0,
                const ImVec2& uv1,
                ImU32 tintColor,
                ImU32 borderColor,
                float rounding,
                const EmissiveImageOptions* emissive
        )
        {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                if (window->SkipItems)
                        return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;

		ImVec2 min = window->DC.CursorPos;
		ImVec2 max = ImVec2(min.x + size.x, min.y + size.y);
		ImRect bb(min, max);
		ImGui::ItemSize(bb);
		if (!ImGui::ItemAdd(bb, 0))
			return false;

		ImDrawList* drawList = ImGui::GetWindowDrawList();

                const HvkEmissiveBinding* binding = AllocateEmissiveBinding(textureId, emissive);
                ImTextureID resolvedTex = binding ? (ImTextureID)binding : textureId;

                // For rounded images, we use path clipping
                if (rounding > 0.0f)
                {
                        drawList->AddImageRounded(resolvedTex, bb.Min, bb.Max, uv0, uv1, tintColor, rounding);
                }
                else
                {
                        drawList->AddImage(resolvedTex, bb.Min, bb.Max, uv0, uv1, tintColor);
                }

		if ((borderColor & IM_COL32_A_MASK) != 0 && borderColor != IM_COL32_BLACK_TRANS)
		{
			drawList->AddRect(bb.Min, bb.Max, borderColor, rounding, 0, 1.0f);
		}

		return true;
	}

        bool ImageWithCustomBorder(
                ImTextureID textureId,
                ImTextureID borderTextureId,
                const ImVec2& size,
                float borderSize,
                ImU32 tintColor,
                ImU32 borderTintColor,
                const ImVec2& uv0,
                const ImVec2& uv1,
                const EmissiveImageOptions* emissive
        )
        {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                if (window->SkipItems)
                        return false;

		ImVec2 min = window->DC.CursorPos;
		ImVec2 max = ImVec2(min.x + size.x, min.y + size.y);
		ImRect bb(min, max);
		ImGui::ItemSize(bb);
		if (!ImGui::ItemAdd(bb, 0))
			return false;

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Render border using 9-slice approach
		// Border texture should be designed for 9-slice (corners, edges, center)
		// For simplicity, we'll render border as a simple frame
		if (borderTextureId && borderSize > 0.0f)
		{
			// Top border
			drawList->AddImage(borderTextureId,
				ImVec2(bb.Min.x, bb.Min.y),
				ImVec2(bb.Max.x, bb.Min.y + borderSize),
				ImVec2(0, 0), ImVec2(1, 0.33f), borderTintColor);
			
			// Bottom border
			drawList->AddImage(borderTextureId,
				ImVec2(bb.Min.x, bb.Max.y - borderSize),
				ImVec2(bb.Max.x, bb.Max.y),
				ImVec2(0, 0.67f), ImVec2(1, 1), borderTintColor);
			
			// Left border
			drawList->AddImage(borderTextureId,
				ImVec2(bb.Min.x, bb.Min.y + borderSize),
				ImVec2(bb.Min.x + borderSize, bb.Max.y - borderSize),
				ImVec2(0, 0.33f), ImVec2(0.33f, 0.67f), borderTintColor);
			
			// Right border
			drawList->AddImage(borderTextureId,
				ImVec2(bb.Max.x - borderSize, bb.Min.y + borderSize),
				ImVec2(bb.Max.x, bb.Max.y - borderSize),
				ImVec2(0.67f, 0.33f), ImVec2(1, 0.67f), borderTintColor);
		}

                const HvkEmissiveBinding* binding = AllocateEmissiveBinding(textureId, emissive);
                ImTextureID resolvedTex = binding ? (ImTextureID)binding : textureId;

                // Render main texture (inside border)
                ImVec2 innerMin(bb.Min.x + borderSize, bb.Min.y + borderSize);
                ImVec2 innerMax(bb.Max.x - borderSize, bb.Max.y - borderSize);
                if (innerMin.x < innerMax.x && innerMin.y < innerMax.y)
                {
                        drawList->AddImage(resolvedTex, innerMin, innerMax, uv0, uv1, tintColor);
                }

		return true;
	}

	bool TextureButton(
		const char* label,
		ImTextureID textureId,
		const ImVec2& size,
		float rounding
	)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(label);
		const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

		ImVec2 button_size = size;
		if (button_size.x == 0.0f)
			button_size.x = label_size.x + style.FramePadding.x * 2.0f;
		if (button_size.y == 0.0f)
			button_size.y = label_size.y + style.FramePadding.y * 2.0f;

		ImVec2 min = window->DC.CursorPos;
		ImVec2 max = ImVec2(min.x + button_size.x, min.y + button_size.y);
		const ImRect bb(min, max);
		ImGui::ItemSize(bb, style.FramePadding.y);
		if (!ImGui::ItemAdd(bb, id))
			return false;

		bool hovered, held;
		bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, 0);

		// Render texture background
		ImU32 tintColor = IM_COL32_WHITE;
		if (held)
			tintColor = IM_COL32(200, 200, 200, 255); // Darker when pressed
		else if (hovered)
			tintColor = IM_COL32(220, 220, 220, 255); // Slightly darker when hovered

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		if (rounding > 0.0f)
			drawList->AddImageRounded(textureId, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), tintColor, rounding);
		else
			drawList->AddImage(textureId, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), tintColor);

		// Render label on top
		ImVec2 label_pos = ImVec2(
			bb.Min.x + (button_size.x - label_size.x) * 0.5f,
			bb.Min.y + (button_size.y - label_size.y) * 0.5f
		);
		drawList->AddText(label_pos, IM_COL32_WHITE, label);

		return pressed;
	}

	// ====================================================================
	// CUSTOM TAB BAR
	// ====================================================================

	bool CustomTabBar(
		const char* const* labels,
		int count,
		int& activeTab,
		ImFont* selectedFont,
		ImFont* unselectedFont,
		float selectedFontSize,
		float unselectedFontSize,
		ImU32 selectedColor,
		ImU32 unselectedColor,
		ImU32 glowColor,
		float glowSize,
		float glowIntensity,
		float horizontalSpacing,
		float verticalPadding
	)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems || count == 0)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;

		bool tabChanged = false;
		const ImGuiID tabBarId = window->GetID("##CustomTabBar");

		// Calculate total width needed
		float totalWidth = 0.0f;
		std::vector<float> tabWidths(count);
		
		ImFont* defaultFont = ImGui::GetFont();
		float defaultFontSize = ImGui::GetFontSize();
		
		float selSize = selectedFontSize > 0.0f ? selectedFontSize : defaultFontSize * 1.2f;
		float unselSize = unselectedFontSize > 0.0f ? unselectedFontSize : defaultFontSize;

		for (int i = 0; i < count; i++)
		{
			ImFont* fontToUse = (i == activeTab) ? (selectedFont ? selectedFont : defaultFont) : (unselectedFont ? unselectedFont : defaultFont);
			float fontSize = (i == activeTab) ? selSize : unselSize;
			
			ImVec2 textSize;
			if (fontToUse)
				textSize = fontToUse->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, labels[i], NULL, NULL);
			else
				textSize = ImGui::CalcTextSize(labels[i]);
			
			tabWidths[i] = textSize.x + horizontalSpacing * 2.0f;
			totalWidth += tabWidths[i];
		}

		// Calculate starting position (center the tabs)
		ImVec2 cursorPos = window->DC.CursorPos;
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		float startX = cursorPos.x + (availableSize.x - totalWidth) * 0.5f;
		
		// Add vertical padding
		if (verticalPadding > 0.0f)
			ImGui::SetCursorPosY(cursorPos.y + verticalPadding);

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// First pass: Render all unselected tabs
		for (int i = 0; i < count; i++)
		{
			if (i == activeTab)
				continue;

			float x = startX;
			for (int j = 0; j < i; j++)
				x += tabWidths[j];

			ImFont* fontToUse = unselectedFont ? unselectedFont : defaultFont;
			float fontSize = unselSize;

			ImVec2 textSize;
			if (fontToUse)
				textSize = fontToUse->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, labels[i], NULL, NULL);
			else
				textSize = ImGui::CalcTextSize(labels[i]);

			ImVec2 textPos(
				x + (tabWidths[i] - textSize.x) * 0.5f,
				cursorPos.y + verticalPadding + (textSize.y * 0.5f)
			);

			// Check for click
			ImRect tabRect(ImVec2(x, cursorPos.y + verticalPadding), ImVec2(x + tabWidths[i], cursorPos.y + verticalPadding + textSize.y * 1.5f));
			ImGuiID tabId = window->GetID((void*)(intptr_t)(i + 1000));
			
			bool hovered = ImGui::IsMouseHoveringRect(tabRect.Min, tabRect.Max);
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				activeTab = i;
				tabChanged = true;
			}

			// Render unselected text
			if (fontToUse)
				drawList->AddText(fontToUse, fontSize, textPos, unselectedColor, labels[i], NULL);
			else
				drawList->AddText(textPos, unselectedColor, labels[i]);
		}

		// Second pass: Render selected tab with glow
		if (activeTab >= 0 && activeTab < count)
		{
			float x = startX;
			for (int j = 0; j < activeTab; j++)
				x += tabWidths[j];

			ImFont* fontToUse = selectedFont ? selectedFont : defaultFont;
			float fontSize = selSize;

			ImVec2 textSize;
			if (fontToUse)
				textSize = fontToUse->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, labels[activeTab], NULL, NULL);
			else
				textSize = ImGui::CalcTextSize(labels[activeTab]);

			ImVec2 textPos(
				x + (tabWidths[activeTab] - textSize.x) * 0.5f,
				cursorPos.y + verticalPadding + (textSize.y * 0.5f)
			);

			// Render glow effect - we need to use drawList directly since CustomTabBar has custom positioning
			// Save and restore cursor position to use GlowText's internal logic
			ImVec2 savedCursorPos = window->DC.CursorPos;
			window->DC.CursorPos = textPos;
			
			// Temporarily use GlowText, then restore cursor
			GlowText(fontToUse, fontSize, selectedColor, labels[activeTab],
				glowColor, glowSize, glowIntensity);
			
			// Restore original cursor position (we don't want cursor advancement in CustomTabBar)
			window->DC.CursorPos = savedCursorPos;
		}

		// Update cursor position
		ImGui::SetCursorPosY(cursorPos.y + verticalPadding * 2.0f + (selectedFontSize > 0.0f ? selectedFontSize : defaultFontSize) * 1.5f);

		return tabChanged;
	}

	bool CustomTabBar(
		const char* const* labels,
		int count,
		int& activeTab,
		ImFont* selectedFont,
		ImFont* unselectedFont,
		float selectedFontSize,
		float unselectedFontSize,
		const ImVec4& selectedColor,
		const ImVec4& unselectedColor,
		const ImVec4& glowColor,
		float glowSize,
		float glowIntensity,
		float horizontalSpacing,
		float verticalPadding
	)
	{
		return CustomTabBar(labels, count, activeTab, selectedFont, unselectedFont,
			selectedFontSize, unselectedFontSize,
			ImGui::ColorConvertFloat4ToU32(selectedColor),
			ImGui::ColorConvertFloat4ToU32(unselectedColor),
			ImGui::ColorConvertFloat4ToU32(glowColor),
			glowSize, glowIntensity, horizontalSpacing, verticalPadding);
	}

	// ====================================================================
	// CUSTOM WINDOWS
	// ====================================================================

	// Store texture info for current window
	struct WindowTextureData
	{
		ImTextureID textureId = (ImTextureID)nullptr;
		ImU32 tintColor = IM_COL32_WHITE;
		ImVec2 uv0 = ImVec2(0, 0);
		ImVec2 uv1 = ImVec2(1, 1);
	};

	static WindowTextureData g_WindowTextureData;

	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImGuiWindowFlags flags
	)
	{
		return BeginWindowWithTexture(name, p_open, backgroundTextureId, IM_COL32_WHITE, ImVec2(0, 0), ImVec2(1, 1), flags);
	}

	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImU32 textureTintColor,
		ImGuiWindowFlags flags
	)
	{
		return BeginWindowWithTexture(name, p_open, backgroundTextureId, textureTintColor, ImVec2(0, 0), ImVec2(1, 1), flags);
	}

	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImU32 textureTintColor,
		const ImVec2& uv0,
		const ImVec2& uv1,
		ImGuiWindowFlags flags
	)
	{
		g_WindowTextureData.textureId = backgroundTextureId;
		g_WindowTextureData.tintColor = textureTintColor;
		g_WindowTextureData.uv0 = uv0;
		g_WindowTextureData.uv1 = uv1;

		// Use NoBackground flag so ImGui doesn't draw its own background
		flags |= ImGuiWindowFlags_NoBackground;
		bool result = ImGui::Begin(name, p_open, flags);
		
		if (result && backgroundTextureId)
		{
			// Draw texture in the window background
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if (window)
			{
				ImDrawList* drawList = window->DrawList;
				
				// Get the window's content area (excluding title bar if present)
				ImVec2 windowMin = window->Pos;
				ImVec2 windowMax = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
				
				// Adjust for title bar if present
				if (!(flags & ImGuiWindowFlags_NoTitleBar) && window->TitleBarHeight > 0.0f)
				{
					windowMin.y += window->TitleBarHeight;
				}
				
				// Draw texture behind everything in the window
				drawList->AddImage(backgroundTextureId, windowMin, windowMax,
					uv0, uv1, textureTintColor);
			}
		}

		return result;
	}

	void EndWindowWithTexture()
	{
		// Reset texture data
		g_WindowTextureData = WindowTextureData();
		ImGui::End();
	}

	// ====================================================================
	// UTILITY FUNCTIONS
	// ====================================================================

	RenderBackend GetRenderBackend()
	{
		return g_App.g_RenderBackend;
	}

	bool IsDX12()
	{
		return g_App.g_RenderBackend == RenderBackend::DX12;
	}

	bool IsDX11()
	{
		return g_App.g_RenderBackend == RenderBackend::DX11;
	}
}

