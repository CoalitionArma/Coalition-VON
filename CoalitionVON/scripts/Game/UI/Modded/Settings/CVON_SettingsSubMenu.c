//The "Coalition VON" tab of the settings menu (UI/layouts/Menus/SettingsSubMenus/CVON_SettingsSubMenu.layout).
//Each widget is named after the CVON_GameSettings variable it edits; the base class saves on leaving the tab.
class CVON_SettingsSubMenu : SCR_SettingsSubMenuBase
{
	//==========================================================================================================================================================================
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		m_aSettingsBindings.Clear();
		m_aSettingsBindings.Insert(new SCR_SettingBindingGameplay(CVON_GameSettings.MODULE, CVON_GameSettings.RADIO_GAME_AUDIO, CVON_GameSettings.RADIO_GAME_AUDIO));

		LoadSettings();
	}
}
