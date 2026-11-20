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
//Speaker: the radio can also play out loud, so anyone near the vehicle hears its traffic. It starts off;
//the crew turn it on and off (VONVehicleSpeaker keybind).
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

	[Attribute("0", desc: "Infantry phone at the back of the vehicle: someone standing behind it can pick it up and talk with the crew over the intercom. Needs the intercom, and the CVON_InfantryPhone action context and CVON_InfantryPhoneAction on the vehicle's ActionsManagerComponent (see TrackedVehicle_Base).")]
	bool m_bHasInfantryPhone;

	//Who is holding the infantry phone (their character's RplId); invalid when nobody is
	[RplProp()] RplId m_PhoneHolder = RplId.Invalid();

	static const float PHONE_REACH = 2.0;		// metres (flat) from the phone to pick it up
	static const float PHONE_CORD_LENGTH = 4.0;	// metres (flat) before the cord pulls it out of your hand
	static const float PHONE_HEIGHT = 1.0;		// metres above the bottom of the vehicle
	static const int PHONE_CHECK_MS = 500;

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

	override bool HasRadio()
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

	//Speaker: off until the crew turn it on, comes from the vehicle (m_iSpeakerRange is set on the base prefabs)
	override bool CanUseSpeaker()
	{
		return HasSpeaker();
	}

	override IEntity GetSpeakerEntity()
	{
		return GetOwner();
	}

	//Built in: never lying on the ground, and its speaker carries the full m_iSpeakerRange
	override bool IsDropped()
	{
		return false;
	}

	override int GetSpeakerRange()
	{
		return m_iSpeakerRange;
	}

	override vector GetSpeakerPosition()
	{
		vector offset = "0 1.5 0";
		return GetOwner().CoordToParent(offset);
	}

	//Infantry phone: a handset at the back of the vehicle. Whoever holds it is on the crew intercom
	//(CVON_VehicleComms.GetIntercomVehicle), so they and the crew hear each other clearly.
	//==========================================================================================================================================================================
	bool HasInfantryPhone()
	{
		if (!m_bHasInfantryPhone || !HasIntercom())
			return false;

		DamageManagerComponent damage = DamageManagerComponent.Cast(GetOwner().FindComponent(DamageManagerComponent));
		return !damage || damage.GetState() != EDamageState.DESTROYED;
	}

	//Where the phone hangs: the middle of the vehicle's rear (vehicles face +Z), about waist height
	vector GetPhonePosition()
	{
		vector mins, maxs;
		GetOwner().GetBounds(mins, maxs);
		vector local = Vector((mins[0] + maxs[0]) * 0.5, mins[1] + PHONE_HEIGHT, mins[2]);
		return GetOwner().CoordToParent(local);
	}

	//On foot, alive and within reach of the phone
	bool CanUsePhone(IEntity character, float reach)
	{
		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera || CVON_VehicleComms.GetSeat(character))
			return false;

		CharacterControllerComponent controller = chimera.GetCharacterController();
		if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE)
			return false;

		vector phone = GetPhonePosition();
		vector origin = character.GetOrigin();
		return vector.DistanceXZ(origin, phone) <= reach && Math.AbsFloat(origin[1] - phone[1]) <= 2.5;
	}

	bool IsPhoneHeldBy(IEntity character)
	{
		if (!character || m_PhoneHolder == RplId.Invalid())
			return false;

		RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
		return rpl && rpl.Id() == m_PhoneHolder;
	}

	IEntity GetPhoneHolder()
	{
		if (m_PhoneHolder == RplId.Invalid())
			return null;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_PhoneHolder));
		if (!rpl)
			return null;
		return rpl.GetEntity();
	}

	//Authority (validated by SCR_PlayerController.RpcAsk_ToggleInfantryPhone)
	void PickUpPhoneServer(IEntity holder)
	{
		RplComponent rpl = RplComponent.Cast(holder.FindComponent(RplComponent));
		if (!rpl)
			return;

		m_PhoneHolder = rpl.Id();
		Replication.BumpMe();
		GetGame().GetCallqueue().Remove(CheckPhone);
		GetGame().GetCallqueue().CallLater(CheckPhone, PHONE_CHECK_MS, true);
	}

	void HangUpPhoneServer()
	{
		GetGame().GetCallqueue().Remove(CheckPhone);
		if (m_PhoneHolder == RplId.Invalid())
			return;

		m_PhoneHolder = RplId.Invalid();
		Replication.BumpMe();
	}

	//Authority, while held: the cord only reaches so far, and the phone drops when the holder dies, is
	//knocked out, gets in a vehicle or the vehicle is destroyed
	protected void CheckPhone()
	{
		IEntity holder = GetPhoneHolder();
		if (!holder || !HasInfantryPhone() || !CanUsePhone(holder, PHONE_CORD_LENGTH))
			HangUpPhoneServer();
	}

	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		//One frame to register with the gamemode (it may not exist yet at post init)
		SetEventMask(owner, EntityEvent.FRAME);
	}

	void ~CVON_VehicleRadioComponent()
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.UnregisterVehicleComms(this);
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(CheckPhone);
	}

	//Once: every machine registers the vehicle so infantry phone holders can be found (GetPhoneVehicle).
	//The authority also registers the speaker so transmissions can find it, and takes the vehicle's faction
	//until a crew member key-fills it with theirs (KeyFillFromCrew, VONVehicleKeyFill keybind).
	//==========================================================================================================================================================================
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		ClearEventMask(owner, EntityEvent.FRAME);
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode && m_bHasInfantryPhone)
			gamemode.RegisterVehicleComms(this);

		if (!Replication.IsServer())
			return;

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

	//Channels were (re)built for the radio's faction
	//==========================================================================================================================================================================
	override protected void OnChannelsInitialized()
	{
		SelectStartingChannel();
	}

	//Authority: tune to the starting frequency (GetStartingFrequency), adding it to the channel list if the
	//list doesn't have it, else channel 1. Sets the channel and the frequency together so they always agree.
	//==========================================================================================================================================================================
	override protected void SelectStartingChannel(CVON_RadioComponent userRadio = null)
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

	//The vehicle whose infantry phone this character is holding, or null. Getting into any seat (this
	//vehicle's or another) disconnects the phone straight away on every machine; the server hangs it up on
	//its next cord check (CheckPhone).
	static CVON_VehicleRadioComponent GetPhoneVehicle(IEntity character)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!character || !gamemode || GetSeat(character))
			return null;

		RplId characterId = RplId.Invalid();
		foreach (CVON_VehicleRadioComponent comms: gamemode.m_aVehicleComms)
		{
			if (!comms || comms.m_PhoneHolder == RplId.Invalid())
				continue;

			// Only looked up once a phone is actually held
			if (characterId == RplId.Invalid())
			{
				RplComponent rpl = RplComponent.Cast(character.FindComponent(RplComponent));
				if (!rpl)
					return null;
				characterId = rpl.Id();
			}
			if (comms.m_PhoneHolder == characterId && comms.HasInfantryPhone())
				return comms;
		}
		return null;
	}

	//The vehicle whose intercom this character is on (seated, or holding its infantry phone), or null
	static IEntity GetIntercomVehicle(IEntity character)
	{
		BaseCompartmentSlot seat = GetSeat(character);
		if (!seat)
		{
			CVON_VehicleRadioComponent phone = GetPhoneVehicle(character);
			if (phone)
				return phone.GetOwner();
			return null;
		}

		CVON_VehicleRadioComponent comms = GetVehicleComms(seat);
		if (!comms || !comms.HasIntercom())
			return null;

		if (!IsCrewSeat(seat) && !comms.m_bPassengersOnIntercom)
			return null;
		return comms.GetOwner();
	}
}
