//Player settings for CVON, saved with the game's user settings and shown on the "Coalition VON" tab of the
//settings menu (CVON_SettingsSubMenu).
class CVON_GameSettings : ModuleGameSettings
{
	static const string MODULE = "CVON_GameSettings";
	static const string RADIO_GAME_AUDIO = "m_bRadioGameAudio";

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "While talking on a radio, the TeamSpeak plugin mixes the game's sound (gunfire, explosions, vehicles) into the transmission.")]
	bool m_bRadioGameAudio;

	//The player's own choice, whatever the server enforces
	//==========================================================================================================================================================================
	static bool GetPlayerRadioGameAudio()
	{
		bool enabled = true;
		BaseContainer settings = GetGame().GetGameUserSettings().GetModule(MODULE);
		if (settings)
			settings.Get(RADIO_GAME_AUDIO, enabled);
		return enabled;
	}

	//What the server enforces (CVON_ServerSettings.m_eRadioGameAudio); PLAYER_CHOICE outside a CVON game
	//==========================================================================================================================================================================
	static CVON_ESettingEnforcement GetRadioGameAudioEnforcement()
	{
		CVON_VONGameModeComponent gameMode = CVON_VONGameModeComponent.GetInstance();
		if (!gameMode)
			return CVON_ESettingEnforcement.PLAYER_CHOICE;
		return gameMode.GetRadioGameAudioEnforcement();
	}

	//In force for this player: the server's choice when it enforces one, else the player's
	//==========================================================================================================================================================================
	static bool IsRadioGameAudioEnabled()
	{
		switch (GetRadioGameAudioEnforcement())
		{
			case CVON_ESettingEnforcement.FORCE_ON: return true;
			case CVON_ESettingEnforcement.FORCE_OFF: return false;
		}
		return GetPlayerRadioGameAudio();
	}
}
