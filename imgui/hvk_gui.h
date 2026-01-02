#pragma once

#include "imgui.h"
#include "../example_win32_directx12/settings.h"
#include <string>
#include <vector>

// Forward declarations
struct AppState;
enum class RenderBackend;

namespace HvkGui
{
	// ====================================================================
	// TEXT RENDERING
	// ====================================================================

	/// <summary>
	/// Renders text with a real glow effect using multiple layered passes.
	/// This provides a much better glow than simple offset text rendering.
	/// Works like ImGui::Text() - uses current cursor position and advances it after rendering.
	/// </summary>
	/// <param name="font">The font to use for rendering (nullptr uses default font)</param>
	/// <param name="fontSize">Size of the font</param>
	/// <param name="color">Color of the text</param>
	/// <param name="text">Text to render</param>
	/// <param name="glowColor">Color of the glow (defaults to text color)</param>
	/// <param name="glowSize">Size/intensity of the glow effect (default 8.0f, higher = more glow)</param>
	/// <param name="glowIntensity">Intensity/opacity of the glow (0.0-1.0, default 0.6f)</param>
	void GlowText(
		ImFont* font,
		float fontSize,
		ImU32 color,
		const char* text,
		ImU32 glowColor = IM_COL32_WHITE,
		float glowSize = 8.0f,
		float glowIntensity = 0.6f
	);

	/// <summary>
	/// Overload that accepts ImVec4 color
	/// </summary>
	void GlowText(
		ImFont* font,
		float fontSize,
		const ImVec4& color,
		const char* text,
		const ImVec4& glowColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		float glowSize = 8.0f,
		float glowIntensity = 0.6f
	);

	/// <summary>
	/// Overload that accepts std::string
	/// </summary>
	void GlowText(
		ImFont* font,
		float fontSize,
		ImU32 color,
		const std::string& text,
		ImU32 glowColor = IM_COL32_WHITE,
		float glowSize = 8.0f,
		float glowIntensity = 0.6f
	);

	// ====================================================================
	// TEXTURE WIDGETS
	// ====================================================================

	/// <summary>
	/// Renders a texture with rounded corners
	/// </summary>
	/// <param name="textureId">ImTextureID of the texture</param>
	/// <param name="size">Size of the texture widget</param>
	/// <param name="uv0">UV coordinates for top-left (default 0,0)</param>
	/// <param name="uv1">UV coordinates for bottom-right (default 1,1)</param>
	/// <param name="tintColor">Tint color for the texture (default white)</param>
	/// <param name="borderColor">Border color (default transparent)</param>
	/// <param name="rounding">Corner rounding amount (default 0.0f = no rounding)</param>
	/// <returns>true if the widget was rendered</returns>
	bool ImageRounded(
		ImTextureID textureId,
		const ImVec2& size,
		const ImVec2& uv0 = ImVec2(0, 0),
		const ImVec2& uv1 = ImVec2(1, 1),
		ImU32 tintColor = IM_COL32_WHITE,
		ImU32 borderColor = IM_COL32_BLACK_TRANS,
		float rounding = 0.0f
	);

	/// <summary>
	/// Renders a texture with a custom border texture
	/// The border texture should be a 9-slice style texture (corners, edges, center)
	/// </summary>
	/// <param name="textureId">ImTextureID of the main texture</param>
	/// <param name="borderTextureId">ImTextureID of the border texture</param>
	/// <param name="size">Size of the widget</param>
	/// <param name="borderSize">Size of the border (in pixels)</param>
	/// <param name="tintColor">Tint color for the main texture</param>
	/// <param name="borderTintColor">Tint color for the border texture</param>
	/// <param name="uv0">UV coordinates for top-left of main texture</param>
	/// <param name="uv1">UV coordinates for bottom-right of main texture</param>
	/// <returns>true if the widget was rendered</returns>
	bool ImageWithCustomBorder(
		ImTextureID textureId,
		ImTextureID borderTextureId,
		const ImVec2& size,
		float borderSize = 8.0f,
		ImU32 tintColor = IM_COL32_WHITE,
		ImU32 borderTintColor = IM_COL32_WHITE,
		const ImVec2& uv0 = ImVec2(0, 0),
		const ImVec2& uv1 = ImVec2(1, 1)
	);

	/// <summary>
	/// Button with a texture background and rounded corners
	/// </summary>
	/// <param name="label">Button label</param>
	/// <param name="textureId">Background texture ID</param>
	/// <param name="size">Button size</param>
	/// <param name="rounding">Corner rounding</param>
	/// <returns>true if button was clicked</returns>
	bool TextureButton(
		const char* label,
		ImTextureID textureId,
		const ImVec2& size = ImVec2(0, 0),
		float rounding = 8.0f
	);

	// ====================================================================
	// CUSTOM TAB BAR
	// ====================================================================

	/// <summary>
	/// A fully custom tab bar with glow effects for selected tab
	/// This is a complete rewrite optimized for custom rendering
	/// </summary>
	/// <param name="labels">Array of tab labels</param>
	/// <param name="count">Number of tabs</param>
	/// <param name="activeTab">Reference to the currently active tab index (will be modified on click)</param>
	/// <param name="selectedFont">Font for the selected tab (nullptr = default)</param>
	/// <param name="unselectedFont">Font for unselected tabs (nullptr = default)</param>
	/// <param name="selectedFontSize">Font size for selected tab</param>
	/// <param name="unselectedFontSize">Font size for unselected tabs</param>
	/// <param name="selectedColor">Color for selected tab text</param>
	/// <param name="unselectedColor">Color for unselected tab text</param>
	/// <param name="glowColor">Glow color for selected tab</param>
	/// <param name="glowSize">Size of the glow effect</param>
	/// <param name="glowIntensity">Intensity of the glow effect (0.0-1.0)</param>
	/// <param name="horizontalSpacing">Spacing between tabs horizontally</param>
	/// <param name="verticalPadding">Padding above and below the tab bar</param>
	/// <returns>true if tab selection changed</returns>
	bool CustomTabBar(
		const char* const* labels,
		int count,
		int& activeTab,
		ImFont* selectedFont = nullptr,
		ImFont* unselectedFont = nullptr,
		float selectedFontSize = 0.0f,
		float unselectedFontSize = 0.0f,
		ImU32 selectedColor = IM_COL32_WHITE,
		ImU32 unselectedColor = IM_COL32(150, 150, 150, 255),
		ImU32 glowColor = IM_COL32_WHITE,
		float glowSize = 10.0f,
		float glowIntensity = 0.7f,
		float horizontalSpacing = 12.0f,
		float verticalPadding = 8.0f
	);

	/// <summary>
	/// Overload with ImVec4 colors
	/// </summary>
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
		const ImVec4& glowColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f),
		float glowSize = 10.0f,
		float glowIntensity = 0.7f,
		float horizontalSpacing = 12.0f,
		float verticalPadding = 8.0f
	);

	// ====================================================================
	// CUSTOM WINDOWS
	// ====================================================================

	/// <summary>
	/// Begins a custom window with a texture background
	/// This works similar to ImGui::Begin() but draws a texture behind the window content
	/// </summary>
	/// <param name="name">Unique window name/ID</param>
	/// <param name="p_open">Pointer to bool to control window open/close state (can be nullptr)</param>
	/// <param name="backgroundTextureId">ImTextureID of the background texture</param>
	/// <param name="flags">ImGuiWindowFlags (same as ImGui::Begin)</param>
	/// <returns>true if window is visible and should be rendered</returns>
	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImGuiWindowFlags flags = 0
	);

	/// <summary>
	/// Overload with texture tint color
	/// </summary>
	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImU32 textureTintColor,
		ImGuiWindowFlags flags = 0
	);

	/// <summary>
	/// Overload with texture UV coordinates
	/// </summary>
	bool BeginWindowWithTexture(
		const char* name,
		bool* p_open,
		ImTextureID backgroundTextureId,
		ImU32 textureTintColor,
		const ImVec2& uv0,
		const ImVec2& uv1,
		ImGuiWindowFlags flags = 0
	);

	/// <summary>
	/// Ends the custom window (must be called after BeginWindowWithTexture)
	/// </summary>
	void EndWindowWithTexture();

	// ====================================================================
	// UTILITY FUNCTIONS
	// ====================================================================

	/// <summary>
	/// Gets the current render backend (DX11 or DX12)
	/// Requires access to global AppState
	/// </summary>
	RenderBackend GetRenderBackend();

	/// <summary>
	/// Checks if the current backend is DirectX 12
	/// </summary>
	bool IsDX12();

	/// <summary>
	/// Checks if the current backend is DirectX 11
	/// </summary>
	bool IsDX11();
}

