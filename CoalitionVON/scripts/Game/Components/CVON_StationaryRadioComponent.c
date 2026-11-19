class CVON_StationaryRadioComponentClass: CVON_RadioComponentClass
{
}

//A radio set placed in the world, like a field radio on a desk at a command post. Nobody carries it: the
//set is its own carrier, it never counts as dropped, and its speaker carries the full m_iSpeakerRange.
//
//Key: a mission maker can set m_sFactionKeyOverride to key the set from the start. Left empty, the set
//stays unkeyed and silent until the first player of a military faction grabs its handset; it then takes
//that player's key, channels and net (CVON_RadioComponent.KeyFillFromCrew) and keeps them for good.
//Handset: picked up within m_fHandsetReach of the set, pulled out of the hand beyond m_fHandsetCordLength,
//and dropped when the holder is knocked out, dies or leaves.
//Speaker: plays out loud for the people around the set; m_bSpeakerOnByDefault sets how it starts.
//==========================================================================================================================================================================
class CVON_StationaryRadioComponent: CVON_RadioComponent
{
	[Attribute("", desc: "Faction key the set starts with (e.g. BLUFOR). Empty: the first player of a military faction to grab the handset keys it with their own.")]
	string m_sFactionKeyOverride;

	[Attribute("", desc: "Frequency the set starts on when it is keyed by its faction key, or by a player who carries no radio of their own. Empty: the faction's long-range frequency when there is no frequency config, else the config's first long-range frequency, else channel 1.")]
	string m_sStartFrequency;

	[Attribute("0", desc: "The speaker starts on")]
	bool m_bSpeakerOnByDefault;

	[Attribute("2", desc: "Metres from the set to pick up its handset")]
	float m_fHandsetReach;

	[Attribute("4", desc: "Metres from the set before the cord pulls the handset out of your hand")]
	float m_fHandsetCordLength;

	static const int STATION_RADIO_RANGE = 20000;	// metres, used when the prefab leaves m_iRadioRange unset
	static const float SPEAKER_SWITCH_REACH = 2.5;	// metres from the set to reach its speaker switch

	//The set is the radio's carrier
	//==========================================================================================================================================================================
	override IEntity GetCarrier()
	{
		return GetOwner();
	}

	//Never lying loose on the ground, so the speaker switch decides whether it plays
	override bool IsDropped()
	{
		return false;
	}

	//An unkeyed set stays silent: its speaker switch may flip, but it plays only once it has a key
	override bool HasActiveSpeaker()
	{
		return m_sFactionKey != "" && super.HasActiveSpeaker();
	}

	override int GetSpeakerRange()
	{
		return m_iSpeakerRange;
	}

	override bool CanUseSpeaker()
	{
		return true;
	}

	override bool SpeakerOnByDefault()
	{
		return m_bSpeakerOnByDefault;
	}

	//m_iRadioRange belongs to CVON_RadioComponent, whose default suits a handheld; a station reaches further
	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		//Frames until the set is keyed (the gamemode may not exist yet at post init)
		SetEventMask(owner, EntityEvent.FRAME);

		BaseContainer source = GetComponentSource(owner);
		if (source && !source.IsVariableSet("m_iRadioRange"))
			m_iRadioRange = STATION_RADIO_RANGE;
	}

	//Only the mission's key here; a set without one waits for its first user (GrabHandsetServer). Nothing
	//without CVON's gamemode.
	//==========================================================================================================================================================================
	override void InitializeRadios()
	{
		//Without the gamemode there is nothing to wait for (picking up the handset sets the frame again)
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			ClearEventMask(GetOwner(), EntityEvent.FRAME);
			return;
		}

		if (m_sFactionKey != "")
			return;

		if (m_sFactionKeyOverride == "")
		{
			//Unkeyed with no override: nothing to poll for until a handset is held (GrabHandsetServer sets the frame again)
			if (m_HandMicHolder == RplId.Invalid())
				ClearEventMask(GetOwner(), EntityEvent.FRAME);
			return;
		}

		InitializeForFaction(m_sFactionKeyOverride);
	}

	//Authority: tune to m_sStartFrequency, adding it to the channel list if the list doesn't have it. With no
	//start frequency and no frequency config, the faction's long-range frequency; otherwise the config's first
	//long-range frequency, else channel 1. Sets the channel and the frequency together so they always agree.
	//==========================================================================================================================================================================
	override protected void OnChannelsInitialized()
	{
		string startFrequency = m_sStartFrequency;
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		FactionManager factionManager = GetGame().GetFactionManager();
		if (startFrequency == "" && (!gamemode || !gamemode.m_FreqConfig) && factionManager)
		{
			SCR_Faction faction = SCR_Faction.Cast(factionManager.GetFactionByKey(m_sOriginalFactionKey));
			if (faction && faction.GetFactionRadioFrequency() > 0)
				startFrequency = faction.GetFactionRadioFrequency().ToString();
		}

		//A long-range set: a command net, not the squad net that heads the list
		if (startFrequency == "")
		{
			array<string> longRange = {};
			GetConfiguredLongRangeFrequencies(m_sOriginalFactionKey, longRange);
			if (!longRange.IsEmpty())
				startFrequency = longRange[0];
		}

		int index = -1;
		if (startFrequency != "")
		{
			index = m_aChannels.Find(startFrequency);
			if (index < 0)
				index = m_aChannels.Insert(startFrequency);
		}

		if (index < 0)
			index = 0;

		m_iCurrentChannel = index + 1;
		m_sFrequency = m_aChannels.Get(index);
		UseSharedFrequencyKey();
	}

	//Authority, after a key fill from a user's radio: its net, and the key that radio uses on it. Copied
	//channels don't go through InitializeForFaction, so the speaker is registered here. Keyed mid-call with its
	//speaker on, it joins the calls already under way.
	//==========================================================================================================================================================================
	override protected void SelectStartingChannel(CVON_RadioComponent userRadio = null)
	{
		super.SelectStartingChannel(userRadio);
		UseSharedFrequencyKey();

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode)
			return;

		gamemode.RegisterSpeakerRadio(this);
		gamemode.StartSpeakerTicking();
	}

	//Authority: the base retune folds the key for a shared frequency from the carrier's faction, and the set
	//has none, so it folds it here from the key it was filled with
	//==========================================================================================================================================================================
	override void UpdateFrequncyServer(string freq, int input = -1, bool updateSettings = true)
	{
		super.UpdateFrequncyServer(freq, input, updateSettings);
		UseSharedFrequencyKey();
		BumpAndNotify();
	}

	//Authority: on a frequency shared between factions (CVON_VONGameModeComponent.m_aSharedFrequencies) that
	//lists the set's faction, the shared key ("US|BLUFOR|") a carried radio of that faction gets; otherwise
	//the set's own key
	//==========================================================================================================================================================================
	protected void UseSharedFrequencyKey()
	{
		m_sFactionKey = m_sOriginalFactionKey;
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode || !gamemode.m_aSharedFrequencies || m_sOriginalFactionKey == "")
			return;

		foreach (CVON_SharedFrequencyObject sharedFreq: gamemode.m_aSharedFrequencies)
		{
			if (!sharedFreq || sharedFreq.m_sSharedFrequency != m_sFrequency || !sharedFreq.m_aFactionIds || !sharedFreq.m_aFactionIds.Contains(m_sOriginalFactionKey))
				continue;

			m_sFactionKey = "";
			foreach (string factionKey: sharedFreq.m_aFactionIds)
			{
				m_sFactionKey += factionKey + "|";
			}
		}
	}

	//Authority (validated by the player controller): a set nobody has keyed takes the key of its first user,
	//if their faction is military. Civilians, spectators and players without a faction leave it unkeyed.
	//Picking up the handset and switching the speaker both count as using it.
	//==========================================================================================================================================================================
	void KeyFillFromFirstUser(IEntity user)
	{
		if (m_sFactionKey != "" || !user)
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(user.FindComponent(FactionAffiliationComponent));
		if (!factionComp || factionComp.GetAffiliatedFactionKey() == "")
			return;

		SCR_Faction faction = SCR_Faction.Cast(factionComp.GetAffiliatedFaction());
		if (faction && faction.IsMilitary())
			KeyFillFromCrew(user);
	}

	//Authority (validated by the player controller)
	//==========================================================================================================================================================================
	override void GrabHandsetServer(IEntity holder)
	{
		KeyFillFromFirstUser(holder);
		super.GrabHandsetServer(holder);
	}

	//Authority (validated by the player controller): a speaker switched on also joins calls already under way
	//==========================================================================================================================================================================
	override void ToggleSpeakerServer()
	{
		super.ToggleSpeakerServer();

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.StartSpeakerTicking();
	}

	//Authority (validated by the player controller): switched on with its speaker on, it joins calls already
	//under way too
	//==========================================================================================================================================================================
	override void TogglePowerServer()
	{
		super.TogglePowerServer();

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.StartSpeakerTicking();
	}

	//A living player within reach metres of the set
	//==========================================================================================================================================================================
	bool IsWithinReach(IEntity user, float reach)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (!character)
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE)
			return false;

		return vector.Distance(user.GetOrigin(), GetOwner().GetOrigin()) <= reach;
	}

	//Holding the handset, alive, within the cord: may talk on the set and switch or tune it
	//==========================================================================================================================================================================
	bool CanOperate(IEntity user)
	{
		return IsHandsetHeldBy(user) && IsWithinReach(user, m_fHandsetCordLength);
	}

	//Authority: the cord only reaches so far from the set; the handset is also dropped if the holder is dead or
	//unconscious, or is no longer a connected player
	//==========================================================================================================================================================================
	override protected void CheckHandset(float timeSlice)
	{
		m_fHandsetCheckTimer -= timeSlice;
		if (m_fHandsetCheckTimer > 0)
			return;
		m_fHandsetCheckTimer = HANDSET_CHECK_INTERVAL;

		IEntity holder = GetHandsetHolder();
		if (!holder || GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(holder) <= 0)
		{
			ReleaseHandsetServer();
			return;
		}

		ChimeraCharacter holderCharacter = ChimeraCharacter.Cast(holder);
		if (holderCharacter)
		{
			SCR_CharacterControllerComponent holderController = SCR_CharacterControllerComponent.Cast(holderCharacter.GetCharacterController());
			if (holderController && holderController.GetLifeState() != ECharacterLifeState.ALIVE)
			{
				ReleaseHandsetServer();
				return;
			}
		}

		if (vector.Distance(holder.GetOrigin(), GetOwner().GetOrigin()) > m_fHandsetCordLength)
			ReleaseHandsetServer();
	}
}
