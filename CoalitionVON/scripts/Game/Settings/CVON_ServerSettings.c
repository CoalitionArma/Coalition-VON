//How the server treats a player setting
enum CVON_ESettingEnforcement
{
	PLAYER_CHOICE,	//each player's own setting (settings menu, Coalition VON tab)
	FORCE_ON,
	FORCE_OFF
}

//Whether a radio can receive while its user transmits on it
enum CVON_ERadioDuplex
{
	HALF,	//no: while you transmit, you don't hear that frequency (people next to you are still heard directly)
	FULL	//yes: transmit and receive at the same time
}

//Server-wide CVON settings. Where they come from, highest priority first:
//  1. the server config: "missionHeader": { "m_CVON_Settings": { ... } } (see readme), as ACE does it
//  2. the mission: m_MissionSettings on the world's CVON_VONGameModeComponent
//  3. the defaults below
//The whole object comes from one source: a field left out of the server config takes the default
//below, not the mission's value. The server replicates the result to every client
//(CVON_VONGameModeComponent.ApplyServerSettings).
[BaseContainerProps()]
class CVON_ServerSettings
{
	[Attribute(SCR_Enum.GetDefault(CVON_ESettingEnforcement.PLAYER_CHOICE), UIWidgets.ComboBox, desc: "Mix game audio into radio transmissions: 0 = each player's own setting, 1 = on for everyone, 2 = off for everyone", enums: ParamEnumArray.FromEnum(CVON_ESettingEnforcement))]
	CVON_ESettingEnforcement m_eRadioGameAudio;

	[Attribute("1", desc: "Hills and mountains between sender and receiver weaken radio signals")]
	bool m_bTerrainBlocksRadio;

	[Attribute("1", UIWidgets.Slider, desc: "How much terrain weakens radio signals (0 = none, 1 = normal, 2 = double)", params: "0 2 0.05")]
	float m_fTerrainBlockStrength;

	[Attribute(SCR_Enum.GetDefault(CVON_ERadioDuplex.HALF), UIWidgets.ComboBox, desc: "0 = half duplex: a radio can't receive while its user transmits on it. 1 = full duplex: radios receive and transmit at the same time", enums: ParamEnumArray.FromEnum(CVON_ERadioDuplex))]
	CVON_ERadioDuplex m_eRadioDuplex;

	[Attribute("1", desc: "Listeners hear a squelch burst when a transmission opens and a squelch tail when it closes")]
	bool m_bSquelchTones;

	[Attribute("1", desc: "A radio operator killed or knocked out mid-transmission is cut off with a burst of static")]
	bool m_bTransmissionCutOff;

	[Attribute("0", UIWidgets.Slider, desc: "Enemy AI can hear players talking within their voice range and turn toward them (0 = off, 1 = AI in range hear you almost at once, even through walls)", params: "0 1 0.05")]
	float m_fAIHearing;
}
