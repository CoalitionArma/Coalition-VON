//The "Coalition VON" tab of the settings menu (UI/layouts/Menus/SettingsSubMenus/CVON_SettingsSubMenu.layout).
//Each widget is named after the CVON_GameSettings variable it edits; the base class saves on leaving the tab.
//A setting the server enforces (CVON_ServerSettings) shows the server's value, greyed out, and the player's
//own choice is kept for servers that leave it to them.
class CVON_SettingsSubMenu : SCR_SettingsSubMenuBase
{
	protected static const string HINT_WIDGET = "RadioGameAudioHint";

	//==========================================================================================================================================================================
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		m_aSettingsBindings.Clear();

		CVON_ESettingEnforcement enforcement = CVON_GameSettings.GetRadioGameAudioEnforcement();
		if (enforcement == CVON_ESettingEnforcement.PLAYER_CHOICE)
			m_aSettingsBindings.Insert(new SCR_SettingBindingGameplay(CVON_GameSettings.MODULE, CVON_GameSettings.RADIO_GAME_AUDIO, CVON_GameSettings.RADIO_GAME_AUDIO));
		else
			ShowEnforced(CVON_GameSettings.RADIO_GAME_AUDIO, HINT_WIDGET, enforcement == CVON_ESettingEnforcement.FORCE_ON);

		LoadSettings();
	}

	//Shows the server's value in a Yes/No spin box without binding it, so the player's own value isn't overwritten
	//==========================================================================================================================================================================
	protected void ShowEnforced(string widgetName, string hintWidgetName, bool value)
	{
		Widget widget = GetRootWidget().FindAnyWidget(widgetName);
		if (widget)
		{
			SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(widget.FindHandler(SCR_SpinBoxComponent));
			int item = 0; //"No"
			if (value)
				item = 1; //"Yes"
			if (spin)
				spin.SetCurrentItem(item, false, false, false);
			widget.SetEnabled(false);
		}

		TextWidget hint = TextWidget.Cast(GetRootWidget().FindAnyWidget(hintWidgetName));
		if (hint)
		{
			string state = "off";
			if (value)
				state = "on";
			hint.SetText(string.Format("Set by the server: %1 for everyone. %2", state, hint.GetText()));
		}
	}
}
