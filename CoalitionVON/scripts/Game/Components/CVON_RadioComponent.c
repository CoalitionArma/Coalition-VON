class CVON_RadioComponentClass: ScriptComponentClass
{
}

enum CVON_ERadioType
{
	SHORT,
	LONG
}

class CVON_RadioComponent: ScriptComponent
{
	// What displays on the VON HUD
	[Attribute("AN/PRC-148")] string m_sRadioName;

	//Used for allocating the frequencies to the radios in the SCR_CallsignBlahBlahsomething. Also is how we sort it out so your active first is always your SR.
	[Attribute("0", UIWidgets.ComboBox, "Used for sorting frequencies", "", enumType: CVON_ERadioType )] int m_eRadioType;

	//How far it go...
	[Attribute("5000")] int m_iRadioRange;

	//How much terrain gets in this radio's way: a powerful set with a good antenna (a manpack) shrugs off hills a
	//handheld loses the signal behind. A link uses the average of the sending and receiving radios'.
	[Attribute("1", UIWidgets.Slider, desc: "How much terrain interference this radio suffers: 1 = handheld, lower for powerful sets (0 = none)", params: "0 2 0.05")]
	float m_fInterference;

	//The Icon at the center of the VON Radial Menu
	[Attribute("{D9F335476016F8D7}UI/layouts/Radios/ANPRC152/data/ANPRC152Icon.edds", UIWidgets.ResourcePickerThumbnail, "Icon in the center of the radial menu", params: "edds")] ResourceName m_sRadioIcon;

	[Attribute("{0470C6B83320621C}UI/layouts/Radios/ANPRC152/data/ANPRC152DialOn.edds", UIWidgets.ResourcePickerThumbnail, "Dial On", params: "edds")] ResourceName m_sDialOn;

	[Attribute("{606059BCB597FCFC}UI/layouts/Radios/ANPRC152/data/ANPRC152DialOff.edds", UIWidgets.ResourcePickerThumbnail, "Dial Off", params: "edds")] ResourceName m_sDialOff;

	//The Menu we use whenever we open up this radio.
	[Attribute(ChimeraMenuPreset.ANPRC152.ToString(), UIWidgets.ComboBox, "", "", enumType: ChimeraMenuPreset )] int m_eRadioMenu;

	//What channel we are on, used in conjunction with m_aChannels, make sure to -1 for the index;
	[RplProp(onRplName: "OnRadioStateReplicated")] int m_iCurrentChannel = 1;

	//Yep : )
	[RplProp(onRplName: "OnRadioStateReplicated")] string m_sFrequency = "55500";

	//Time Deviation, used to shift the times of the radio making it so others not on the same time cant hear you.
	[RplProp(onRplName: "OnRadioStateReplicated")] int m_iTimeDeviation = 0;

	//Who is holding this radio's handset (a manpack's, see CVON_Manpack); invalid when nobody is
	[RplProp(onRplName: "OnHandsetHolderReplicated")] RplId m_HandMicHolder = RplId.Invalid();

	//This is used in conjuction with the gamemode setting m_bIsFactionEncryptionEnabled
	//If enabled it makes it so only factions can hear just their factions radios, this is initialized when a radio is picked up for the first time
	//This means if you pick up a russian radio you can hear a russian broadcast.
	//If m_bIsFactionEncrypto.... disabled then this does nothing.
	//These two strings below are generated on radio initilization onto a player and should almost never change except for when a frequency is shared
	//Consider this the "Key Fill" for the radio.
	[RplProp(onRplName: "OnRadioStateReplicated")] string m_sFactionKey = "";

	//Store this for any frequency changes that may result in it folding over into a shared frequency
	string m_sOriginalFactionKey = "";
	//Used to store any channels the player makes and the base channels definied in the faction.
	[RplProp()] ref array<string> m_aChannels = {};

	[RplProp(onRplName: "OnRadioStateReplicated")] bool m_bPower = true;

	//Authority only: how often an uninitialised radio checks whether it is on a character yet
	static const float INIT_POLL_INTERVAL = 0.5;
	float m_fInitPollTimer = 0;

	//How loud the radio is
	int m_iVolume = 9;

	//What ear it comes out of, neither needs to be tracked across clients cause who gives af.
	CVON_EStereo m_eStereo = CVON_EStereo.BOTH;

	//Speaker: the radio plays out loud for the people around it, positioned where it is. Speakers start
	//off; a vehicle's crew or the radio's carrier turns theirs on (CanUseSpeaker / SpeakerOnByDefault).
	//A radio lying on the ground plays out loud by itself (IsDropped).
	[Attribute("15", desc: "Metres at which the radio's speaker fades out (manpacks and vehicles; handheld and dropped radios carry at most HANDHELD_SPEAKER_RANGE)")]
	int m_iSpeakerRange;

	//A handheld radio's (or a dropped one's) small speaker doesn't carry as far as a manpack's
	static const int HANDHELD_SPEAKER_RANGE = 8;

	//Flipped from the radio's default by the Toggle Radio Speaker keybind
	[RplProp()] bool m_bSpeakerToggled = false;

	//Authority: how often the handset cord is checked while someone holds it
	static const float HANDSET_CHECK_INTERVAL = 0.5;
	float m_fHandsetCheckTimer = 0;

	//==========================================================================================================================================================================	

	//The character carrying this radio. GetRootParent() isn't enough: for a seated character it returns
	//the vehicle. Vehicle radios override this to return the vehicle itself.
	//==========================================================================================================================================================================
	IEntity GetCarrier()
	{
		IEntity parent = GetOwner().GetParent();
		while (parent)
		{
			if (ChimeraCharacter.Cast(parent))
				return parent;
			parent = parent.GetParent();
		}
		return GetOwner().GetRootParent();
	}

	//Is this one of the local player's radios?
	//==========================================================================================================================================================================
	bool IsHeldByLocalPlayer()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local)
			return false;
		if (GetCarrier() == local)
			return true;
		//Holding its handset: a retune by the carrier must reach our RadioData.json too
		return IsHandsetHeldBy(local);
	}

	//A replicated radio value changed (channel, frequency, time deviation, encryption key, power). Every
	//client used to compare these against copies every frame for every radio in the world; now only a
	//change to one of our own radios rewrites RadioData.json.
	//==========================================================================================================================================================================
	void OnRadioStateReplicated()
	{
		if (IsHeldByLocalPlayer())
			WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//Authority changes: replicate, and on a listen server (Workbench, player-hosted) the host's own radios
	//need the refresh too, since replication callbacks don't run on the authority.
	//==========================================================================================================================================================================
	protected void BumpAndNotify()
	{
		Replication.BumpMe();
		if (!System.IsConsoleApp())
			OnRadioStateReplicated();
	}

	//m_fInterference as sent with a transmission (CVON_VONContainer.m_iInterference)
	//==========================================================================================================================================================================
	int GetInterferencePercent()
	{
		return (int)Math.Round(Math.Clamp(m_fInterference, 0, 2) * 100);
	}

	//Returns the RplId of the radio.
	//==========================================================================================================================================================================
	RplId GetRplId()
	{
		return RplComponent.Cast(GetOwner().FindComponent(RplComponent)).Id();
	}


	//Called on the authority, updates the channel.
	//==========================================================================================================================================================================
	void UpdateChannelServer(int input)
	{
		m_iCurrentChannel = input;
		BumpAndNotify();
	}

	//Called on the authority, updates the Frequency.
	//==========================================================================================================================================================================
	void UpdateFrequncyServer(string freq, int input = -1, bool updateSettings = true)
	{
		//Update the channels array for the new freq
		if (m_aChannels.Count() >= m_iCurrentChannel)
			m_aChannels.Set(m_iCurrentChannel + input, freq);
		m_sFrequency = freq;
		bool isShared = false;
		IEntity carrier = GetCarrier();
		if (!carrier)
		{
			BumpAndNotify();
			return;
		}
		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(carrier.FindComponent(FactionAffiliationComponent));
		if (!factionComp)
		{
			BumpAndNotify();
			return;
		}

		string ownerFactionKey = factionComp.GetAffiliatedFactionKey();
		foreach (CVON_SharedFrequencyObject sharedFreq: CVON_VONGameModeComponent.GetInstance().m_aSharedFrequencies)
		{
			if (sharedFreq.m_sSharedFrequency != freq)
				continue;

			string sharedFactionKey = "";
			if (!sharedFreq.m_aFactionIds.Contains(ownerFactionKey))
				continue;
			foreach (string factionKey: sharedFreq.m_aFactionIds)
			{
				sharedFactionKey += factionKey;
				sharedFactionKey += "|";
			}
			isShared = true;
			m_sFactionKey = sharedFactionKey;
		}
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(carrier);
		if (playerId > 0 && updateSettings)
			SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).UpdateSettings();
		if (!isShared && m_sOriginalFactionKey == "")
			GetGame().GetCallqueue().CallLater(SetFactionKey, 1000, false);
		else if (!isShared && m_sOriginalFactionKey != "")
			SetFactionKey();
		BumpAndNotify();
	}

	void SetFactionKey()
	{
		if (!Replication.IsServer())
			return;

		m_sFactionKey = m_sOriginalFactionKey;
		BumpAndNotify();
	}

	//Toggles radio power from the server, hmm... who named this
	void TogglePowerServer()
	{
		m_bPower = !m_bPower;
		BumpAndNotify();
	}

	//Called on the authority, updates the TimeDeviation.
	//==========================================================================================================================================================================
	void UpdateTimeDeviationServer(int input)
	{
		m_iTimeDeviation = input;
		BumpAndNotify();
	}

	//Called on the authority, updates the TimeDeviation.
	//==========================================================================================================================================================================
	void AddChannelServer()
	{
		m_aChannels.Insert("55500");
		Replication.BumpMe();
	}

	//Manpack: a long-range radio carried by someone wearing a radio backpack (CVON_Manpack). Others can grab
	//its handset; its carrier can turn its speaker on.
	//==========================================================================================================================================================================
	bool IsManpack()
	{
		return CVON_Manpack.IsManpackRadio(this);
	}

	//Speaker
	//==========================================================================================================================================================================
	bool SpeakerOnByDefault()
	{
		return false;
	}

	//Its carrier can turn it on: always for a manpack, for a handheld while the server allows it
	//(m_bHandheldSpeakers)
	bool CanUseSpeaker()
	{
		if (!ChimeraCharacter.Cast(GetCarrier()))
			return false;
		if (IsManpack())
			return true;

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		return gamemode && gamemode.IsServerSettingOn(CVON_VONGameModeComponent.SETTING_HANDHELD_SPEAKERS);
	}

	bool IsSpeakerOn()
	{
		return SpeakerOnByDefault() != m_bSpeakerToggled;
	}

	//Lying loose on the ground after someone carried it (it has a key fill). Radios nobody has carried yet,
	//like spares in a crate, stay silent.
	bool IsDropped()
	{
		return !GetOwner().GetParent() && m_sFactionKey != "";
	}

	//Playing out loud right now: switched on, and either dropped (while the server allows it,
	//m_bDroppedRadiosTransmit) or carried with its speaker turned on
	bool HasActiveSpeaker()
	{
		if (!m_bPower)
			return false;

		if (IsDropped())
		{
			CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
			return gamemode && gamemode.IsServerSettingOn(CVON_VONGameModeComponent.SETTING_DROPPED_RADIOS);
		}
		return CanUseSpeaker() && IsSpeakerOn();
	}

	//Metres at which the speaker fades out
	int GetSpeakerRange()
	{
		if (IsManpack() || m_iSpeakerRange < HANDHELD_SPEAKER_RANGE)
			return m_iSpeakerRange;
		return HANDHELD_SPEAKER_RANGE;
	}

	//What the speaker's sound comes from: the character carrying the radio (vehicle radios: the vehicle; a
	//dropped radio: itself)
	IEntity GetSpeakerEntity()
	{
		return GetCarrier();
	}

	vector GetSpeakerPosition()
	{
		IEntity speaker = GetSpeakerEntity();
		if (!speaker || speaker == GetOwner())
		{
			vector groundOffset = "0 0.1 0";
			return GetOwner().GetOrigin() + groundOffset;
		}

		vector offset = "0 1.3 0";
		return speaker.GetOrigin() + offset;
	}

	//Authority: the Toggle Radio Speaker keybind (validated by the player controller)
	void ToggleSpeakerServer()
	{
		m_bSpeakerToggled = !m_bSpeakerToggled;
		Replication.BumpMe();

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.RegisterSpeakerRadio(this);
	}

	//Handset
	//==========================================================================================================================================================================
	bool IsHandsetHeldBy(IEntity character)
	{
		if (!character || m_HandMicHolder == RplId.Invalid())
			return false;

		RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
		if (!rpl)
			return false;
		return rpl.Id() == m_HandMicHolder;
	}

	IEntity GetHandsetHolder()
	{
		if (m_HandMicHolder == RplId.Invalid())
			return null;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_HandMicHolder));
		if (!rpl)
			return null;
		return rpl.GetEntity();
	}

	//Authority (validated by the player controller)
	void GrabHandsetServer(IEntity holder)
	{
		RplComponent rpl = RplComponent.Cast(holder.FindComponent(RplComponent));
		if (!rpl)
			return;

		m_HandMicHolder = rpl.Id();
		m_fHandsetCheckTimer = HANDSET_CHECK_INTERVAL;
		SetEventMask(GetOwner(), EntityEvent.FRAME);

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.SetHandsetHeld(this, true, holder);
		BumpAndNotifyHandset();
	}

	void ReleaseHandsetServer()
	{
		if (m_HandMicHolder == RplId.Invalid())
			return;

		//Read before it's cleared: the gamemode ends what the holder had on this radio
		IEntity holder = GetHandsetHolder();
		m_HandMicHolder = RplId.Invalid();
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.SetHandsetHeld(this, false, holder);
		BumpAndNotifyHandset();
	}

	//Would this radio receive the transmission: its frequency, time deviation and, with encryption on, its key
	//==========================================================================================================================================================================
	bool Receives(CVON_VONContainer VONContainer)
	{
		if (!VONContainer || VONContainer.m_sFrequency != m_sFrequency || VONContainer.m_iTimeDeviation != m_iTimeDeviation)
			return false;

		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		return !gamemode || !gamemode.IsEncryptionEnabled() || VONContainer.m_sFactionKey == m_sFactionKey;
	}

	protected void BumpAndNotifyHandset()
	{
		Replication.BumpMe();
		if (!System.IsConsoleApp())
			OnHandsetHolderReplicated();
	}

	//Every machine: whoever now holds the handset adds this radio to their radio keys, whoever let go removes it
	void OnHandsetHolderReplicated()
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.UpdateHandsetRadio(this);
	}

	//Authority: the cord only reaches so far; the handset is also dropped if the holder dies or the radio
	//stops being a manpack (backpack taken off, radio dropped)
	protected void CheckHandset(float timeSlice)
	{
		m_fHandsetCheckTimer -= timeSlice;
		if (m_fHandsetCheckTimer > 0)
			return;
		m_fHandsetCheckTimer = HANDSET_CHECK_INTERVAL;

		IEntity holder = GetHandsetHolder();
		IEntity carrier = GetCarrier();
		if (!holder || !carrier || !IsManpack())
		{
			ReleaseHandsetServer();
			return;
		}

		ChimeraCharacter holderCharacter = ChimeraCharacter.Cast(holder);
		if (holderCharacter)
		{
			SCR_CharacterControllerComponent holderController = SCR_CharacterControllerComponent.Cast(holderCharacter.GetCharacterController());
			if (holderController && holderController.IsDead())
			{
				ReleaseHandsetServer();
				return;
			}
		}

		if (vector.Distance(holder.GetOrigin(), carrier.GetOrigin()) > CVON_Manpack.HANDSET_CORD_LENGTH)
			ReleaseHandsetServer();
	}

	void TogglePowerClient()
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).ToggleRadioPower(GetRplId());
		WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//==========================================================================================================================================================================
	void UpdateChannelClient(int input)
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).UpdateRadioChannel(input, GetRplId());
		WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//==========================================================================================================================================================================
	void UpdateFrequencyClient(string input)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		pc.UpdateRadioFrequency(input, GetRplId());
		GetGame().GetCallqueue().CallLater(pc.UpdateSettings, 500, false);
		WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//==========================================================================================================================================================================
	void UpdateTimeDeviationClient(int input)
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).UpdateRadioTimeDeviation(input, GetRplId());
		WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//==========================================================================================================================================================================
	void AddChannelClient()
	{
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).AddChannelServer(GetRplId());
		WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}

	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		SetEventMask(owner, EntityEvent.FRAME);
	}

	bool IsAlreadyAChannel(string freq, array<string> LRFreqs, array<string> SRFreqs)
	{
		return LRFreqs.Contains(freq) || SRFreqs.Contains(freq);
	}

	//Handles assigning 
	//==========================================================================================================================================================================
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		//Only the authority has work here: giving the radio its faction and channels once it is on a character
		//(SCR_CharacterInventoryStorageComponent also does this when it's picked up; this is the fallback), and
		//checking the handset cord while someone holds it. Clients get radio changes through
		//OnRadioStateReplicated instead of polling every frame.
		if (!Replication.IsServer())
		{
			ClearEventMask(owner, EntityEvent.FRAME);
			return;
		}

		bool needsFrame = false;
		if (m_HandMicHolder != RplId.Invalid())
		{
			needsFrame = true;
			CheckHandset(timeSlice);
		}

		if (m_sFactionKey == "")
		{
			needsFrame = true;
			m_fInitPollTimer -= timeSlice;
			if (m_fInitPollTimer <= 0)
			{
				m_fInitPollTimer = INIT_POLL_INTERVAL;
				//Redundancy, also used in base CVON without the CRF as they don't have the GS.
				InitializeRadios();
			}
		}

		if (!needsFrame)
			ClearEventMask(owner, EntityEvent.FRAME);
	}

	//Authority: gives a radio on a character that character's faction key and channels.
	//In Workbench the game is a listen server; elsewhere only the dedicated server does this.
	//==========================================================================================================================================================================
	void InitializeRadios()
	{
		#ifdef WORKBENCH
		#else
		if (!System.IsConsoleApp())
			return;
		#endif
		//Faction found no longer needed.
		if (m_sFactionKey != "")
			return;

		//Radio is in the inventory of a player.
		IEntity carrier = GetCarrier();
		if (!SCR_ChimeraCharacter.Cast(carrier))
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(carrier.FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;

		InitializeForFaction(factionComp.GetAffiliatedFactionKey());
	}

	//The radio's "key fill": a faction's encryption key, the preset group frequencies and then that
	//faction's group overrides, without duplicates. Also used by vehicle radios.
	//==========================================================================================================================================================================
	void InitializeForFaction(string factionKey)
	{
		//No faction yet (e.g. during slotting): try again later rather than filling channels without a key
		if (factionKey == "")
			return;

		m_sFactionKey = factionKey;
		m_sOriginalFactionKey = factionKey;

		ref array<string> SRFrequencies = {};
		ref array<string> LRFrequencies = {};
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode && gamemode.m_FreqConfig)
		{
			foreach (CVON_GroupFrequencyContainer groupContainer: gamemode.m_FreqConfig.m_aPresetGroupFrequencyContainers)
				AddGroupFrequencies(groupContainer, SRFrequencies, LRFrequencies);

			//A station in a world with no faction manager gets the preset frequencies only
			SCR_Faction faction;
			if (!CVON_StationaryRadioComponent.Cast(this) || GetGame().GetFactionManager())
				faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(factionKey));
			if (faction && faction.GetCallsignInfo())
			{
				foreach (CVON_GroupFrequencyContainer overrideContainer: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
					AddGroupFrequencies(overrideContainer, SRFrequencies, LRFrequencies);
			}
		}

		//Every radio with a key fill can end up playing out loud (its speaker turned on, or dropped), so
		//transmissions check it (BroadcastToSpeakers skips radios on other frequencies first)
		if (gamemode)
			gamemode.RegisterSpeakerRadio(this);

		m_aChannels.InsertAll(SRFrequencies);
		m_aChannels.InsertAll(LRFrequencies);
		//If there are no frequencies added, just add a default channel.
		if (m_aChannels.Count() == 0)
		{
			m_aChannels.Insert("55500");
			m_iCurrentChannel = 1;
			m_sFrequency = m_aChannels.Get(0);
		}
		OnChannelsInitialized();
		BumpAndNotify();
	}

	//Authority: the channel list has just been built for the radio's faction. Handheld radios keep their
	//channel (players' radios are tuned when they're handed out); vehicle radios pick where they start
	//(CVON_VehicleRadioComponent).
	protected void OnChannelsInitialized()
	{
	}

	//Can this radio be used at all. Vehicle radios answer from their prefab (CVON_VehicleRadioComponent).
	//==========================================================================================================================================================================
	bool HasRadio()
	{
		return true;
	}

	//Authority: "key fill" from a user. If the radio isn't on their encryption it takes their key and
	//channels, copied from their own radio so it matches however the mission set their radios up (frequency
	//config, the Lobby's faction channels); with no radio of their own, from their faction. Used by a
	//vehicle's crew (VONVehicleKeyFill keybind, or automatically when the radio has no key at all) and by
	//the first user of a station (CVON_StationaryRadioComponent). Until then the radio keeps its key and
	//goes on playing that traffic. A vehicle's prefab faction often isn't the players' (vanilla "US"
	//vehicles, "BLUFOR" players).
	//==========================================================================================================================================================================
	void KeyFillFromCrew(IEntity character)
	{
		if (!HasRadio() || !character)
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;

		string crewKey = factionComp.GetAffiliatedFactionKey();
		if (crewKey == "" || SharesKey(crewKey))
			return;

		m_aChannels.Clear();
		CVON_RadioComponent crewRadio = FindCrewRadio(character);
		if (crewRadio && !crewRadio.m_aChannels.IsEmpty())
		{
			m_sFactionKey = crewKey;
			m_sOriginalFactionKey = crewKey;
			m_aChannels.InsertAll(crewRadio.m_aChannels);
			SelectStartingChannel(crewRadio);
		}
		else
		{
			InitializeForFaction(crewKey);
			SelectStartingChannel(null);
		}

		BumpAndNotify();
	}

	//Authority, after a key fill: start on the channel, frequency and time deviation of the user's radio the
	//channels were copied from. Null when there was none to copy: InitializeForFaction built the channels
	//and OnChannelsInitialized picked where the radio starts. Vehicle radios pick their own start.
	//==========================================================================================================================================================================
	protected void SelectStartingChannel(CVON_RadioComponent userRadio = null)
	{
		if (!userRadio)
			return;

		m_iCurrentChannel = userRadio.m_iCurrentChannel;
		m_sFrequency = userRadio.m_sFrequency;
		m_iTimeDeviation = userRadio.m_iTimeDeviation;
	}

	//Already on this key: the faction's own, or a shared-frequency key listing it ("US|BLUFOR|")
	bool SharesKey(string factionKey)
	{
		if (m_sFactionKey == factionKey)
			return true;

		array<string> sharedKeys = {};
		m_sFactionKey.Split("|", sharedKeys, true);
		return sharedKeys.Contains(factionKey);
	}

	//The user's own radio to copy channels from: a long-range one if they have it
	protected CVON_RadioComponent FindCrewRadio(IEntity character)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode)
			return null;

		array<IEntity> radios = gamemode.GetRadioEntities(character);
		if (!radios)
			return null;

		CVON_RadioComponent firstRadio;
		foreach (IEntity radio: radios)
		{
			if (radio == GetOwner())
				continue;

			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			if (!radioComp || CVON_VehicleRadioComponent.Cast(radioComp))
				continue;

			if (radioComp.m_eRadioType == CVON_ERadioType.LONG)
				return radioComp;

			if (!firstRadio)
				firstRadio = radioComp;
		}
		return firstRadio;
	}

	//Every long-range frequency the frequency config gives a faction: the preset groups' and the faction's
	//own overrides (SCR_FactionCallsignInfo.m_aGroupFrequencyOverrides)
	void GetConfiguredLongRangeFrequencies(string factionKey, notnull array<string> outFrequencies)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode || !gamemode.m_FreqConfig)
			return;

		array<ref CVON_GroupFrequencyContainer> containers = {};
		foreach (CVON_GroupFrequencyContainer presetContainer: gamemode.m_FreqConfig.m_aPresetGroupFrequencyContainers)
			containers.Insert(presetContainer);

		SCR_Faction faction;
		if (GetGame().GetFactionManager() && factionKey != "")
			faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(factionKey));
		if (faction && faction.GetCallsignInfo() && faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
		{
			foreach (CVON_GroupFrequencyContainer overrideContainer: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
				containers.Insert(overrideContainer);
		}

		foreach (CVON_GroupFrequencyContainer container: containers)
		{
			if (!container || !container.m_aLRFrequencies)
				continue;

			foreach (string freq: container.m_aLRFrequencies)
			{
				if (freq != "" && !outFrequencies.Contains(freq))
					outFrequencies.Insert(freq);
			}
		}
	}

	protected void AddGroupFrequencies(CVON_GroupFrequencyContainer container, array<string> SRFrequencies, array<string> LRFrequencies)
	{
		if (container.m_aSRFrequencies)
		{
			foreach (string freq: container.m_aSRFrequencies)
			{
				if (freq != "" && !IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies))
					SRFrequencies.Insert(freq);
			}
		}
		if (container.m_aLRFrequencies)
		{
			foreach (string freq: container.m_aLRFrequencies)
			{
				if (freq != "" && !IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies))
					LRFrequencies.Insert(freq);
			}
		}
	}

	//Hmm I wonder what OpenMenu does... 
	//==========================================================================================================================================================================
	void OpenMenu()
	{
		CVON_RadioMenu radioUI = CVON_RadioMenu.Cast(GetGame().GetMenuManager().OpenMenu(m_eRadioMenu));
		radioUI.m_RadioEntity = GetOwner();
	}

	//This writes the RadioData.json, this is what teamspeak looks at and compares a object in VONData.json to.
	//It does this to see if we match the freq, if we do do we match the faction key, if its not defined then its an automatic yes.
	//Then determines volume and what ears.
	//magik :)
	//==========================================================================================================================================================================
	void WriteJSON(IEntity entity)
	{
		WriteRadioData(entity);
	}

	//Static so it can run with no radio to call it on (leaving a vehicle with its radio and none of your own)
	static void WriteRadioData(IEntity entity)
	{
		if (!GetGame().GetPlayerController())
			return;
		if (entity != SCR_PlayerController.GetLocalControlledEntity())
			return;
		JsonSaveContext VONSave = new JsonSaveContext();
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gamemode)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		ref array<RplId> radios = {};
		if (pc.GetHeadsetLoweredState())
			radios = {};
		else 
			radios = CVON_VONGameModeComponent.GetInstance().GetRadios(entity);

		if (!radios)
			return;

		string radioFreqs = "";
		string factionKeys = "";
		foreach (RplId radio: radios)
		{
			RplComponent radioRpl = RplComponent.Cast(Replication.FindItem(radio));
			if (!radioRpl || !radioRpl.GetEntity())
				continue;
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioRpl.GetEntity().FindComponent(CVON_RadioComponent));
			if (!radioComp || !radioComp.m_bPower)
				continue;
			VONSave.StartObject(radio.ToString());
			VONSave.WriteValue("Freq", radioComp.m_sFrequency);
			VONSave.WriteValue("TimeDeviation", radioComp.m_iTimeDeviation);
			VONSave.WriteValue("Volume", radioComp.m_iVolume);
			VONSave.WriteValue("Stereo", radioComp.m_eStereo);
			if (gamemode.IsEncryptionEnabled())
				VONSave.WriteValue("FactionKey", radioComp.m_sFactionKey);
			else
				VONSave.WriteValue("FactionKey", "");
			VONSave.EndObject();
			radioFreqs += radioComp.m_sFrequency + "|";
			factionKeys += radioComp.m_sFactionKey + "|";
		}
		//Listening to another player's radios (SCR_PlayerController.SetRadioListenTarget): theirs too, so the
		//plugin plays their traffic as radio. Not in the frequency list sent to the server, which only routes
		//to radios a player really carries. Volume and ear aren't replicated, so they play at full volume and
		//are spread across the ears to tell the nets apart: one radio in both ears; with more, the first left,
		//the second right, any others in both.
		int listenTarget = pc.GetRadioListenTarget();
		if (listenTarget != 0 && !pc.GetHeadsetLoweredState())
		{
			IEntity target = GetGame().GetPlayerManager().GetPlayerControlledEntity(listenTarget);
			array<RplId> targetRadios = CVON_VONGameModeComponent.GetInstance().GetRadios(target);
			array<RplId> listenedIds = {};
			array<CVON_RadioComponent> listenedRadios = {};
			if (targetRadios)
			{
				foreach (RplId targetRadio: targetRadios)
				{
					if (radios.Contains(targetRadio))
						continue;

					RplComponent targetRadioRpl = RplComponent.Cast(Replication.FindItem(targetRadio));
					if (!targetRadioRpl || !targetRadioRpl.GetEntity())
						continue;

					CVON_RadioComponent targetRadioComp = CVON_RadioComponent.Cast(targetRadioRpl.GetEntity().FindComponent(CVON_RadioComponent));
					if (!targetRadioComp || !targetRadioComp.m_bPower)
						continue;

					listenedIds.Insert(targetRadio);
					listenedRadios.Insert(targetRadioComp);
				}
			}

			foreach (int listenedIndex, CVON_RadioComponent listenedRadio: listenedRadios)
			{
				CVON_EStereo ear = CVON_EStereo.BOTH;
				if (listenedRadios.Count() > 1 && listenedIndex == 0)
					ear = CVON_EStereo.LEFT;
				else if (listenedRadios.Count() > 1 && listenedIndex == 1)
					ear = CVON_EStereo.RIGHT;

				VONSave.StartObject("L" + listenedIds[listenedIndex].ToString());
				VONSave.WriteValue("Freq", listenedRadio.m_sFrequency);
				VONSave.WriteValue("TimeDeviation", listenedRadio.m_iTimeDeviation);
				VONSave.WriteValue("Volume", 9);
				VONSave.WriteValue("Stereo", ear);
				if (gamemode.IsEncryptionEnabled())
					VONSave.WriteValue("FactionKey", listenedRadio.m_sFactionKey);
				else
					VONSave.WriteValue("FactionKey", "");
				VONSave.EndObject();
			}
		}

		SCR_PlayerController.Cast(GetGame().GetPlayerController()).UpdateFreqArray(radioFreqs, factionKeys);
		VONSave.SaveToFile("$profile:/RadioData.json");
	}

	void ~CVON_RadioComponent()
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
		{
			gamemode.UnregisterSpeakerRadio(this);
			gamemode.SetHandsetHeld(this, false);
		}

		if (!GetGame().GetWorld())
			return;

		PlayerManager playerMan = GetGame().GetPlayerManager();
		if (System.IsConsoleApp())
		{
			array<int> playerIds = {};
			playerMan.GetPlayers(playerIds);
			foreach (int playerId: playerIds)
			{
				SCR_PlayerController playerCon = SCR_PlayerController.Cast(playerMan.GetPlayerController(playerId));
				if (!playerCon)
					continue;

				if (!playerCon.m_aRadios.Contains(GetOwner()))
					continue;

				playerCon.m_aRadios.RemoveItemOrdered(GetOwner());
			}
		}
		else
		{
			SCR_PlayerController playerCon = SCR_PlayerController.Cast(GetGame().GetPlayerController());
			if (!playerCon)
				return;

			if (playerCon.m_aRadios.Contains(GetOwner()))
				playerCon.m_aRadios.RemoveItemOrdered(GetOwner());
		}
	}
}
