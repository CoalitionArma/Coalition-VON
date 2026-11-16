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

//How radio signal quality is worked out (SCR_VONController.LinkQuality)
enum CVON_ESignalModel
{
	ARCADE,		//a curve over each radio's range, times a terrain penalty (hills above the line between the radios)
	REALISTIC	//a link budget: transmit power, path loss (free space / ground reflection) and terrain diffraction, against the receiver's sensitivity
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

	[Attribute("0", desc: "A transmitting radio's microphone picks up other players talking near the sender, so the net hears them in the background")]
	bool m_bRadioVoicePickup;

	[Attribute(SCR_Enum.GetDefault(CVON_ESignalModel.ARCADE), UIWidgets.ComboBox, desc: "Radio signal model. 0 = arcade: a quality curve over each radio's range, with a terrain penalty. 1 = realistic: transmit power, free-space and ground-reflection path loss and terrain diffraction against the receiver's sensitivity; out of reach means no signal at all", enums: ParamEnumArray.FromEnum(CVON_ESignalModel))]
	CVON_ESignalModel m_eSignalModel;

	[Attribute("1", desc: "Hills and mountains between sender and receiver weaken radio signals")]
	bool m_bTerrainInterference;

	[Attribute("0.5", UIWidgets.Slider, desc: "How much terrain weakens radio signals (0 = none, 0.5 = normal, 1 = strongest)", params: "0 1 0.05")]
	float m_fTerrainInterferenceStrength;

	[Attribute(SCR_Enum.GetDefault(CVON_ERadioDuplex.HALF), UIWidgets.ComboBox, desc: "0 = half duplex: a radio can't receive while its user transmits on it. 1 = full duplex: radios receive and transmit at the same time", enums: ParamEnumArray.FromEnum(CVON_ERadioDuplex))]
	CVON_ERadioDuplex m_eRadioDuplex;

	[Attribute("1", desc: "Listeners hear a squelch burst when a transmission opens and a squelch tail when it closes")]
	bool m_bSquelchTones;

	[Attribute("1", desc: "A radio operator killed or knocked out mid-transmission is cut off with a burst of static")]
	bool m_bTransmissionCutOff;

	[Attribute("1", desc: "Players can turn on the speaker of a handheld radio they carry (Toggle Radio Speaker keybind), so people around them hear its traffic. Manpack and vehicle speakers work either way")]
	bool m_bHandheldSpeakers;

	[Attribute("1", desc: "A switched-on radio dropped on the ground keeps playing its traffic out loud for anyone nearby")]
	bool m_bDroppedRadiosTransmit;

	[Attribute("0", UIWidgets.Slider, desc: "Enemy AI can hear players talking within their voice range and turn toward them (0 = off, 1 = AI in range hear you almost at once, even through walls)", params: "0 1 0.05")]
	float m_fAIHearing;
}
