#include "theme_helper.h"

namespace ThemeHelper
{
	ImVec4 GetSecondaryColorForTheme(BgTheme theme)
	{
		switch (theme)
		{
		case BgTheme::BLACK:
			return ImVec4(0.1f, 0.1f, 0.1f, 1.0f);  // Dark gray/black
		case BgTheme::PURPLE:
			return ImVec4(0.5f, 0.2f, 0.8f, 1.0f);  // Purple
		case BgTheme::YELLOW:
			return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Yellow
		case BgTheme::BLUE:
			return ImVec4(0.2f, 0.5f, 1.0f, 1.0f);  // Blue
		case BgTheme::GREEN:
			return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);  // Green
		case BgTheme::RED:
			return ImVec4(0.8f, 0.2f, 0.2f, 1.0f);  // Red
		default:
			return ImVec4(0.5f, 0.2f, 0.8f, 1.0f);  // Default to purple
		}
	}

	void UpdateSecondaryColorFromTheme(c_usersettings* user)
	{
		if (!user)
			return;

		user->style.main_secondary_color = GetSecondaryColorForTheme(user->style.bg_theme);
		user->style.tabbar_selected_color = GetSecondaryColorForTheme(user->style.bg_theme);
	}
}

