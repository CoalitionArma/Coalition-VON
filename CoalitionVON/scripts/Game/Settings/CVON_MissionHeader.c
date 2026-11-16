//Server admins set CVON's server settings in the server config, the same way as ACE:
//  "missionHeader": { "m_CVON_Settings": { "m_bSquelchTones": false, ... } }
modded class SCR_MissionHeader : MissionHeader
{
	[Attribute(desc: "Coalition VON server settings (server config: missionHeader.m_CVON_Settings)")]
	protected ref CVON_ServerSettings m_CVON_Settings;

	//==========================================================================================================================================================================
	CVON_ServerSettings CVON_GetSettings()
	{
		return m_CVON_Settings;
	}
}

//Keeps the server config's CVON settings for the whole session. Only the first mission header has them:
//later ones (scenario changes) don't carry the server config's overrides (https://feedback.bistudio.com/T172515),
//which is why ACE applies it only once too.
modded class ArmaReforgerScripted : ChimeraGame
{
	protected static ref CVON_ServerSettings s_CVON_ServerConfigSettings;
	protected static bool s_bCVON_MissionHeaderRead = false;

	//==========================================================================================================================================================================
	override protected void OnMissionSet(MissionHeader mission)
	{
		super.OnMissionSet(mission);

		if (!Replication.IsServer() || s_bCVON_MissionHeaderRead)
			return;

		SCR_MissionHeader header = SCR_MissionHeader.Cast(mission);
		if (!header)
			return;

		s_bCVON_MissionHeaderRead = true;
		s_CVON_ServerConfigSettings = header.CVON_GetSettings();
		if (s_CVON_ServerConfigSettings)
			Print("[CVON] Server settings from the server config's missionHeader", LogLevel.NORMAL);

		//A game mode already running gets them now; one created later reads them itself
		CVON_VONGameModeComponent gameMode = CVON_VONGameModeComponent.GetInstance();
		if (gameMode)
			gameMode.ApplyServerSettings();
	}

	//The server config's CVON settings, or null when it has none (or on a client)
	//==========================================================================================================================================================================
	static CVON_ServerSettings CVON_GetServerConfigSettings()
	{
		return s_CVON_ServerConfigSettings;
	}
}
