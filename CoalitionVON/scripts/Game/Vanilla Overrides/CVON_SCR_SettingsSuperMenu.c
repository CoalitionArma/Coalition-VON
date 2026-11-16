//Adds the "Coalition VON" tab (CVON_SettingsSubMenu) to the settings menu, in game and in the main menu.
modded class SCR_SettingsSuperMenu
{
	static const ResourceName CVON_SETTINGS_LAYOUT = "{4E91BFFAD4E23B80}UI/layouts/Menus/SettingsSubMenus/CVON_SettingsSubMenu.layout";

	//==========================================================================================================================================================================
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		if (!m_SuperMenuComponent || !m_SuperMenuComponent.GetTabView())
			return;

		m_SuperMenuComponent.GetTabView().AddTab(CVON_SETTINGS_LAYOUT, "Coalition VON", true, identifier: "CVON_GameSettings");
	}
}
