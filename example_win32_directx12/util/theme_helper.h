#pragma once

#include "../settings.h"
#include "imgui.h"

namespace ThemeHelper
{
	/// <summary>
	/// Gets the default secondary color for a given background theme
	/// </summary>
	ImVec4 GetSecondaryColorForTheme(BgTheme theme);

	/// <summary>
	/// Updates the user's main_secondary_color based on the current bg_theme
	/// </summary>
	void UpdateSecondaryColorFromTheme(c_usersettings* user);
}

