class CVON_VehicleRadioComponentClass: CVON_RadioComponentClass
{
}

//A radio built into a vehicle, plus the vehicle's intercom. The mod adds it to the tracked, wheeled APC
//and helicopter base prefabs; add it to any other vehicle prefab to give that vehicle comms.
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

	[Attribute("25", desc: "Metres from the vehicle at which the speaker fades out")]
	int m_iSpeakerRange;

	[RplProp()]
	bool m_bSpeakerOn = true;

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

	bool HasActiveSpeaker()
	{
		return m_bHasRadio && m_bHasSpeaker && m_bSpeakerOn && m_bPower;
	}

	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		//One frame on the authority to register the speaker (the gamemode may not exist yet at post init)
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//Authority, once: register with the gamemode so transmissions can find this speaker, and take the
	//vehicle's faction. A vehicle without one (civilian, unclaimed) gets its first crew member's instead.
	//==========================================================================================================================================================================
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		ClearEventMask(owner, EntityEvent.FRAME);
		if (!Replication.IsServer())
			return;

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode || !m_bHasRadio)
			return;

		gamemode.RegisterVehicleRadio(this);
		InitializeRadios();
	}

	//==========================================================================================================================================================================
	override void InitializeRadios()
	{
		if (!m_bHasRadio || m_sFactionKey != "")
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;

		string factionKey = factionComp.GetAffiliatedFactionKey();
		if (factionKey == "")
			factionKey = factionComp.GetDefaultFactionKey();
		InitializeForFaction(factionKey);
	}

	//Authority: a crew member is using a radio that has no faction yet; it takes theirs
	//==========================================================================================================================================================================
	void InitializeForCrew(IEntity character)
	{
		if (!m_bHasRadio || m_sFactionKey != "" || !character)
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
		if (factionComp)
			InitializeForFaction(factionComp.GetAffiliatedFactionKey());
	}

	//==========================================================================================================================================================================
	void ToggleSpeakerServer()
	{
		m_bSpeakerOn = !m_bSpeakerOn;
		Replication.BumpMe();
	}

	//==========================================================================================================================================================================
	void ~CVON_VehicleRadioComponent()
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.UnregisterVehicleRadio(this);
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

	//Crew: driver/pilot and turret seats (gunner, commander, door gunner). Cargo seats are passengers.
	static bool IsCrewSeat(BaseCompartmentSlot seat)
	{
		ECompartmentType type = seat.GetType();
		return type == ECompartmentType.PILOT || type == ECompartmentType.TURRET;
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
		if (!comms || !comms.m_bHasRadio)
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
		if (!comms || !comms.m_bHasIntercom)
			return null;

		if (!IsCrewSeat(seat) && !comms.m_bPassengersOnIntercom)
			return null;
		return comms.GetOwner();
	}
}
