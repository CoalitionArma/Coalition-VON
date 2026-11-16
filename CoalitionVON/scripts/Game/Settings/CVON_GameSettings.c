//Player settings for CVON, shown on the "Coalition VON" tab of the settings menu (CVON_SettingsSubMenu) and
//kept in $profile:CVON_Settings.json.
//
//Not a ModuleGameSettings in the game's user settings: Reforger reads and saves those before any mod is
//loaded, drops modules it doesn't know ("Unknown keyword/data 'CVON_GameSettings'") and writes the file
//back without them, so a player's choice was lost every time the game started. Our own file isn't touched
//by vanilla.
class CVON_GameSettings
{
	static const string FILE_PATH = "$profile:CVON_Settings.json";

	//Widget names on the settings tab
	static const string RADIO_GAME_AUDIO = "m_bRadioGameAudio";
	static const string SQUELCH_TONES = "m_bSquelchTones";

	protected static bool s_bLoaded;
	protected static bool s_bRadioGameAudio = true;	//While talking on a radio, mix the game's sound into the transmission
	protected static bool s_bSquelchTones = true;	//Squelch burst and tail on incoming transmissions

	//==========================================================================================================================================================================
	protected static void Load()
	{
		if (s_bLoaded)
			return;
		s_bLoaded = true;

		if (!FileIO.FileExists(FILE_PATH))
			return;

		JsonLoadContext context = new JsonLoadContext();
		if (!context.LoadFromFile(FILE_PATH))
			return;

		bool value;
		if (context.ReadValue("RadioGameAudio", value))
			s_bRadioGameAudio = value;
		if (context.ReadValue("SquelchTones", value))
			s_bSquelchTones = value;
	}

	protected static void Save()
	{
		JsonSaveContext context = new JsonSaveContext();
		context.WriteValue("RadioGameAudio", s_bRadioGameAudio);
		context.WriteValue("SquelchTones", s_bSquelchTones);
		if (!context.SaveToFile(FILE_PATH))
			Print("[CVON] Could not save " + FILE_PATH, LogLevel.WARNING);
	}

	//The player's own choices
	//==========================================================================================================================================================================
	static bool GetPlayerRadioGameAudio()
	{
		Load();
		return s_bRadioGameAudio;
	}

	static void SetPlayerRadioGameAudio(bool enabled)
	{
		Load();
		s_bRadioGameAudio = enabled;
		Save();
	}

	static bool GetPlayerSquelchTones()
	{
		Load();
		return s_bSquelchTones;
	}

	static void SetPlayerSquelchTones(bool enabled)
	{
		Load();
		s_bSquelchTones = enabled;
		Save();
	}

	//Off for everyone when the server turns squelch off (CVON_ServerSettings.m_bSquelchTones)
	//==========================================================================================================================================================================
	static bool IsServerSquelchOff()
	{
		CVON_VONGameModeComponent gameMode = CVON_VONGameModeComponent.GetInstance();
		return gameMode && !gameMode.IsServerSettingOn(CVON_VONGameModeComponent.SETTING_SQUELCH);
	}

	//==========================================================================================================================================================================
	static bool IsSquelchTonesEnabled()
	{
		return !IsServerSquelchOff() && GetPlayerSquelchTones();
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
