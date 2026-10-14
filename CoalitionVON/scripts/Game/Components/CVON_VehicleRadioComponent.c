class CVON_VehicleRadioComponentClass: CVON_RadioComponentClass
{
}

//A radio built into a vehicle, plus the vehicle's intercom. The mod adds it to the tracked, wheeled APC,
//helicopter, car and truck base prefabs (cars and trucks military only: their default faction must be
//military, so civilian vehicles get nothing); add it to any other vehicle prefab to give that vehicle comms.
//
//Radio: only the crew (driver/pilot and turret seats: gunner, commander, door gunner) can use it. While
//seated it sits at the end of their radio list, so it's on whichever radio key comes after their own
//radios, and it can be opened and tuned like any other radio.
//Speaker: the radio also plays out loud, so anyone near the vehicle hears its traffic. The crew can
//turn it off (VONVehicleSpeaker keybind).
//Intercom: the crew hear each other clearly whenever they talk, whatever the engine noise or distance
//between seats.
//==========================================================================================================================================================================
class CVON_VehicleRadioComponent: CVON_RadioComponent
{
	[Attribute("1", desc: "The vehicle has a radio the crew can use")]
	bool m_bHasRadio;

	[Attribute("1", desc: "The crew can talk to each other over the intercom")]
	bool m_bHasIntercom;

	[Attribute("0", desc: "Passenger (cargo) seats are on the intercom too")]
	bool m_bPassengersOnIntercom;

	[Attribute("1", desc: "The radio plays out loud so people near the vehicle hear it")]
	bool m_bHasSpeaker;

	[Attribute("0", desc: "Comms only when the vehicle's default faction is military. For base prefabs shared with civilian vehicles (cars, trucks).")]
	bool m_bMilitaryOnly;

	[Attribute("", desc: "Frequency this vehicle's radio starts on, and returns to after a key fill (e.g. an air net for helicopters). Empty: the faction's vehicle frequency, then the frequency config's, then the first long-range frequency.")]
	string m_sStartFrequency;

	//Military check result: -1 not worked out yet, 0 civilian, 1 military. Comes from prefab data (the
	//default faction), so every machine reaches the same answer.
	protected int m_iMilitaryState = -1;

	//The vehicle is the radio's carrier: its faction, and no player to update settings for
	//==========================================================================================================================================================================
	override IEntity GetCarrier()
	{
		return GetOwner();
	}

	//"Ours" when we're in one of the vehicle's crew seats, so a retune by another crew member rewrites
	//our RadioData.json
	//==========================================================================================================================================================================
	override bool IsHeldByLocalPlayer()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local)
			return false;
		return CVON_VehicleComms.GetOperatedRadio(local) == this;
	}

	//Does this vehicle get comms at all (see m_bMilitaryOnly)
	bool IsEquipped()
	{
		if (!m_bMilitaryOnly)
			return true;

		if (m_iMilitaryState < 0)
		{
			//Factions not loaded yet: decide later rather than caching "civilian"
			if (!GetGame().GetFactionManager())
				return false;

			m_iMilitaryState = 0;
			FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().FindComponent(FactionAffiliationComponent));
			if (factionComp)
			{
				SCR_Faction faction = SCR_Faction.Cast(factionComp.GetDefaultAffiliatedFaction());
				if (faction && faction.IsMilitary())
					m_iMilitaryState = 1;
			}
		}
		return m_iMilitaryState == 1;
	}

	bool HasRadio()
	{
		return m_bHasRadio && IsEquipped();
	}

	bool HasIntercom()
	{
		return m_bHasIntercom && IsEquipped();
	}

	bool HasSpeaker()
	{
		return m_bHasSpeaker && HasRadio();
	}

	//Speaker: on from the start, comes from the vehicle (m_iSpeakerRange is set on the base prefabs)
	override bool SpeakerOnByDefault()
	{
		return true;
	}

	override bool CanUseSpeaker()
	{
		return HasSpeaker();
	}

	override IEntity GetSpeakerEntity()
	{
		return GetOwner();
	}

	override vector GetSpeakerPosition()
	{
		vector offset = "0 1.5 0";
		return GetOwner().CoordToParent(offset);
	}

	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		//One frame on the authority to register the speaker (the gamemode may not exist yet at post init)
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//Authority, once: register with the gamemode so transmissions can find this speaker, and take the
	//vehicle's faction until a crew member key-fills it with theirs (KeyFillFromCrew, VONVehicleKeyFill keybind).
	//==========================================================================================================================================================================
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		ClearEventMask(owner, EntityEvent.FRAME);
		if (!Replication.IsServer())
			return;

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode || !HasRadio())
			return;

		gamemode.RegisterSpeakerRadio(this);
		InitializeRadios();
	}

	//==========================================================================================================================================================================
	override void InitializeRadios()
	{
		if (!HasRadio() || m_sFactionKey != "")
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;

		string factionKey = factionComp.GetAffiliatedFactionKey();
		if (factionKey == "")
			factionKey = factionComp.GetDefaultFactionKey();
		InitializeForFaction(factionKey);
	}

	//Authority: "key fill", done by a crew member with the VONVehicleKeyFill keybind (or automatically when
	//the radio has no key at all). If the radio isn't on their encryption it takes their key and channels,
	//copied from their own radio so it matches however the mission set their radios up (frequency config,
	//the Lobby's faction channels); with no radio of their own, from their faction. Until then it keeps its
	//key and goes on playing that traffic. A vehicle's prefab faction often isn't the players' (vanilla "US"
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
		}
		else
		{
			InitializeForFaction(crewKey);
		}

		SelectStartingChannel();
		BumpAndNotify();
	}

	//Channels were (re)built for the radio's faction
	//==========================================================================================================================================================================
	override protected void OnChannelsInitialized()
	{
		SelectStartingChannel();
	}

	//Authority: tune to the starting frequency (GetStartingFrequency), adding it to the channel list if the
	//list doesn't have it, else channel 1. Sets the channel and the frequency together so they always agree.
	//==========================================================================================================================================================================
	protected void SelectStartingChannel()
	{
		if (m_aChannels.IsEmpty())
			m_aChannels.Insert("55500");

		int index = -1;
		string startFrequency = GetStartingFrequency();
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
	}

	//Where the radio starts, first match wins: this vehicle's m_sStartFrequency, the faction's
	//vehicle frequency (SCR_FactionCallsignInfo), the frequency config's, then the first long-range
	//frequency on the radio (it's a LONG radio, so command nets beat the squad nets that head the list).
	//Empty: none of those apply, start on channel 1.
	//==========================================================================================================================================================================
	string GetStartingFrequency()
	{
		if (m_sStartFrequency != "")
			return m_sStartFrequency;

		string factionKey = m_sOriginalFactionKey;
		if (factionKey == "")
			factionKey = m_sFactionKey;

		SCR_Faction faction;
		FactionManager factionManager = GetGame().GetFactionManager();
		if (factionManager && factionKey != "")
			faction = SCR_Faction.Cast(factionManager.GetFactionByKey(factionKey));
		if (faction && faction.GetCallsignInfo() && faction.GetCallsignInfo().m_sVehicleFrequency != "")
			return faction.GetCallsignInfo().m_sVehicleFrequency;

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode && gamemode.m_FreqConfig && gamemode.m_FreqConfig.m_sVehicleFrequency != "")
			return gamemode.m_FreqConfig.m_sVehicleFrequency;

		array<string> longRange = {};
		GetConfiguredLongRangeFrequencies(factionKey, longRange);
		foreach (string channelFrequency: m_aChannels)
		{
			if (longRange.Contains(channelFrequency))
				return channelFrequency;
		}

		return "";
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

	//The crew member's own radio to copy channels from: a long-range one if they have it
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

}

//Who is sitting where, for the vehicle radio and intercom
//==========================================================================================================================================================================
class CVON_VehicleComms
{
	//The seat a character is in, or null on foot
	static BaseCompartmentSlot GetSeat(IEntity character)
	{
		if (!character)
			return null;

		CompartmentAccessComponent access = CompartmentAccessComponent.Cast(character.FindComponent(CompartmentAccessComponent));
		if (!access)
			return null;

		BaseCompartmentSlot seat = access.GetCompartment();
		if (seat)
			return seat;

		//Fallback for other players' characters: look for them among the seats of the vehicle they're parented to
		IEntity vehicle = character.GetRootParent();
		if (!vehicle || vehicle == character)
			return null;

		SCR_BaseCompartmentManagerComponent compartmentManager = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (!compartmentManager)
			return null;

		array<BaseCompartmentSlot> seats = {};
		compartmentManager.GetCompartments(seats);
		foreach (BaseCompartmentSlot candidate: seats)
		{
			if (candidate.GetOccupant() == character)
				return candidate;
		}
		return null;
	}

	//Crew: driver/pilot and turret seats (gunner, door gunner), plus commanders. Vanilla often makes the
	//commander a cargo seat linked to a commander's sight turret (LAV-25), so a cargo seat that operates a
	//turret, or is labelled as the commander's, counts too. Other cargo seats are passengers.
	static bool IsCrewSeat(BaseCompartmentSlot seat)
	{
		ECompartmentType type = seat.GetType();
		if (type == ECompartmentType.PILOT || type == ECompartmentType.TURRET)
			return true;

		if (seat.GetAttachedTurret() || !seat.GetAutoConnectTurret().IsEmpty())
			return true;

		UIInfo info = seat.GetUIInfo();
		if (info)
		{
			string seatName = info.GetName();
			seatName.ToLower();
			if (seatName.Contains("commander"))
				return true;
		}
		return false;
	}

	static CVON_VehicleRadioComponent GetVehicleComms(BaseCompartmentSlot seat)
	{
		IEntity vehicle = seat.GetVehicle();
		if (!vehicle)
			return null;
		return CVON_VehicleRadioComponent.Cast(vehicle.FindComponent(CVON_VehicleRadioComponent));
	}

	//The vehicle radio this character can use: they're in a crew seat of a vehicle that has one
	static CVON_VehicleRadioComponent GetOperatedRadio(IEntity character)
	{
		BaseCompartmentSlot seat = GetSeat(character);
		if (!seat || !IsCrewSeat(seat))
			return null;

		CVON_VehicleRadioComponent comms = GetVehicleComms(seat);
		if (!comms || !comms.HasRadio())
			return null;
		return comms;
	}

	//The vehicle whose intercom this character is on, or null
	static IEntity GetIntercomVehicle(IEntity character)
	{
		BaseCompartmentSlot seat = GetSeat(character);
		if (!seat)
			return null;

		CVON_VehicleRadioComponent comms = GetVehicleComms(seat);
		if (!comms || !comms.HasIntercom())
			return null;

		if (!IsCrewSeat(seat) && !comms.m_bPassengersOnIntercom)
			return null;
		return comms.GetOwner();
	}
}
