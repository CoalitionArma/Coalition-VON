//Player settings for CVON, saved with the game's user settings and shown on the "Coalition VON" tab of the
//settings menu (CVON_SettingsSubMenu).
class CVON_GameSettings : ModuleGameSettings
{
	static const string MODULE = "CVON_GameSettings";
	static const string RADIO_GAME_AUDIO = "m_bRadioGameAudio";

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "While talking on a radio, the TeamSpeak plugin mixes the game's sound (gunfire, explosions, vehicles) into the transmission.")]
	bool m_bRadioGameAudio;

	//==========================================================================================================================================================================
	static bool IsRadioGameAudioEnabled()
	{
		bool enabled = true;
		BaseContainer settings = GetGame().GetGameUserSettings().GetModule(MODULE);
		if (settings)
			settings.Get(RADIO_GAME_AUDIO, enabled);
		return enabled;
	}
}
