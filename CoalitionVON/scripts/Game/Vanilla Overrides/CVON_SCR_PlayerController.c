class CVON_RadioSettingObject
{
	string m_sFreq = "";
	CVON_EStereo m_Stereo = CVON_EStereo.BOTH;
	int m_iVolume = 9;
}

modded class SCR_PlayerController
{
	//Used so we can find the entry by playerId
	ref map<int, ref CVON_VONContainer> m_aLocalEntries = new map<int, ref CVON_VONContainer>;

	//Local client and Server track this array. Used to determine radios priority, 0 = SR, 1 = LR, 2 = MR. Keybinds line up like that.
	ref array<IEntity> m_aRadios = {};

	string m_sTeamspeakPluginVersion = "0";

	//How we link what level the enum below should be at.
	//Voice range in metres per tier: Whisper, Close Contact, Normal, Yelling, Manager
	static ref array<int> m_aVolumeValues = {3, 8, 20, 40, 60};

	//Teamspeak has been detected, also used to not have the annoying game locking popup everytime TS wants to crash
	bool m_bHasConnectedToTeamspeakForFirstTime = false;

	//Used so we can keep Stereo and Volume values
	ref array<ref CVON_RadioSettingObject> m_aRadioSettings = {};

	//Local variable stored to remove radios from the radio data json to simulate taking your headset off
	//The asshole who made this will do better in A4
	bool m_bIsHeadsetLowered = false;

	int m_iAmountOfTimesRadioRotated = 1;

	//Radio listening (see CVON_VONGameModeComponent.SetRadioListenTarget): whose radios we hear as well as our
	//own, 0 for nobody. Their radios join our RadioData.json, so their traffic plays as radio.
	//==========================================================================================================================================================================
	protected int m_iRadioListenTarget;

	int GetRadioListenTarget()
	{
		return m_iRadioListenTarget;
	}

	void SetRadioListenTarget(int targetPlayerId)
	{
		if (targetPlayerId == m_iRadioListenTarget)
			return;

		m_iRadioListenTarget = targetPlayerId;
		Rpc(RpcAsk_SetRadioListenTarget, targetPlayerId);
		CVON_RadioComponent.WriteRadioData(GetLocalControlledEntity());
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetRadioListenTarget(int targetPlayerId)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
			gamemode.SetRadioListenTarget(GetPlayerId(), targetPlayerId);
	}

	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
	}

	void ToggleHeadsetLoweredState()
	{
		m_bIsHeadsetLowered = !m_bIsHeadsetLowered;

		IEntity player = GetControlledEntity();
		if (!player)
			return;

		//Rewrites RadioData.json with no radios while lowered (vehicle radio included)
		CVON_RadioComponent.WriteRadioData(player);
	}

	bool GetHeadsetLoweredState()
	{
		return m_bIsHeadsetLowered;
	}

	void SetTeamspeakClientId(int input)
	{
		Rpc(RpcAsk_SetTeamspeakClientId, GetPlayerId(), input);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_SetTeamspeakClientId(int playerId, int input)
	{
		CVON_VONGameModeComponent.GetInstance().UpdateClientId(playerId, input);
	}

	int GetTeamspeakClientId()
	{
		int index = CVON_VONGameModeComponent.GetInstance().m_aPlayerIds.Find(GetPlayerId());
		if (index == -1)
			return 0;
		return CVON_VONGameModeComponent.GetInstance().m_aPlayerClientIds.Get(index);
	}

	int GetPlayersTeamspeakClientId(int playerId)
	{
		int index = CVON_VONGameModeComponent.GetInstance().m_aPlayerIds.Find(playerId);
		if (index == -1)
			return 0;
		return CVON_VONGameModeComponent.GetInstance().m_aPlayerClientIds.Get(index);
	}


	//Used to initials the m_aRadio array
	//Delay is needed as it can take a sec for the entity to initialized for a client on the server.
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		UpdateSettings();

		GetGame().GetCallqueue().CallLater(InitializeRadios, 500, false, to);
	}

	void UpdateSettings()
	{
		m_aRadioSettings.Clear();
		foreach (IEntity radio: m_aRadios)
		{
			if (!radio)
				continue;
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			if (!radioComp)
				continue;
			//Settings restore our own radios on respawn: not a vehicle radio or someone's handset
			if (radioComp.GetCarrier() != GetControlledEntity())
				continue;
			SCR_FactionManager factionMan = SCR_FactionManager.Cast(GetGame().GetFactionManager());
			if (radioComp.m_sFactionKey != "" && radioComp.m_sFactionKey != factionMan.GetPlayerFaction(GetPlayerId()).GetFactionKey())
				continue;
			ref CVON_RadioSettingObject setting = new CVON_RadioSettingObject();
			setting.m_sFreq = radioComp.m_sFrequency;
			setting.m_Stereo = radioComp.m_eStereo;
			setting.m_iVolume = radioComp.m_iVolume;
			m_aRadioSettings.Insert(setting);
		}
	}

	//Handles initializing the m_aRadios array for both this client and the server so both are on the same page
	//Also used to load any settings the radios may have had on respawn.
	//Loading settings only works if the radios where pe configured with the CVON_FreqConfig.
	void InitializeRadios(IEntity to)
	{
		InitializeInventoryRadios(to);
		//Our own controller only. The server keeps vehicle radios out of its copy of the list: group
		//changes retune every radio in it, and a vehicle radio isn't any one player's.
		if (this == GetGame().GetPlayerController())
		{
			AttachVehicleRadio(to);
			//A station's handset only while we still hold it: a respawned character doesn't keep the old one's
			CVON_StationaryRadioComponent station;
			if (m_HandsetRadio)
				station = CVON_StationaryRadioComponent.Cast(m_HandsetRadio.FindComponent(CVON_StationaryRadioComponent));
			if (station && !station.IsHandsetHeldBy(to))
				m_HandsetRadio = null;
			if (m_HandsetRadio && !m_aRadios.Contains(m_HandsetRadio))
				m_aRadios.Insert(m_HandsetRadio);
		}
	}

	//The radio whose handset we're holding (someone's manpack), kept at the end of m_aRadios like the vehicle radio
	IEntity m_HandsetRadio;

	//Where inventory radios go in m_aRadios: before the vehicle radio and a held handset, which stay last
	int GetAttachedRadiosStart()
	{
		int start = m_aRadios.Count();
		if (m_VehicleRadio)
		{
			int vehicleIndex = m_aRadios.Find(m_VehicleRadio);
			if (vehicleIndex >= 0 && vehicleIndex < start)
				start = vehicleIndex;
		}
		if (m_HandsetRadio)
		{
			int handsetIndex = m_aRadios.Find(m_HandsetRadio);
			if (handsetIndex >= 0 && handsetIndex < start)
				start = handsetIndex;
		}
		return start;
	}

	//Local: a radio's handset holder changed (CVON_RadioComponent.OnHandsetHolderReplicated)
	//==========================================================================================================================================================================
	void UpdateHandsetRadio(CVON_RadioComponent radio)
	{
		IEntity character = GetControlledEntity();
		if (!character || !radio)
			return;

		IEntity radioEntity = radio.GetOwner();
		bool holding = radio.IsHandsetHeldBy(character);
		if (holding && m_HandsetRadio != radioEntity)
		{
			if (m_HandsetRadio)
				m_aRadios.RemoveItemOrdered(m_HandsetRadio);
			m_HandsetRadio = radioEntity;
			if (!m_aRadios.Contains(radioEntity))
				m_aRadios.Insert(radioEntity);
			SCR_HintManagerComponent.ShowCustomHint("Holding the " + radio.m_sRadioName + " handset. It's on your radio keys after your own radios.", "Coalition VON", 4);
		}
		else if (!holding && m_HandsetRadio == radioEntity)
		{
			m_aRadios.RemoveItemOrdered(radioEntity);
			m_HandsetRadio = null;
			SCR_HintManagerComponent.ShowCustomHint("Handset released", "Coalition VON", 2);

			//Put down or pulled out of our hand mid-call on a station: stop talking on it. Not when we were
			//killed: the server sends the cut-off, and a normal release from us could reach the receivers first.
			if (CVON_StationaryRadioComponent.Cast(radio))
			{
				SCR_VONController vonController = SCR_VONController.Cast(FindComponent(SCR_VONController));
				ChimeraCharacter holder = ChimeraCharacter.Cast(character);
				if (vonController && vonController.m_CurrentVONContainer && vonController.m_CurrentVONContainer.m_eVonType == CVON_EVONType.RADIO
					&& vonController.m_CurrentVONContainer.m_iRadioId == radio.GetRplId()
					&& holder && holder.GetCharacterController() && holder.GetCharacterController().GetLifeState() == ECharacterLifeState.ALIVE)
					vonController.DeactivateCVON();
			}
		}
		else
		{
			return;
		}

		//RadioData.json and the server's frequency index follow the new radio list
		CVON_RadioComponent.WriteRadioData(character);
	}

	//Grab or return the handset of the manpack this character carries (CVON_GrabHandMic action)
	//==========================================================================================================================================================================
	void ToggleHandset(RplId carrierId)
	{
		Rpc(RpcAsk_ToggleHandset, carrierId);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_ToggleHandset(RplId carrierId)
	{
		IEntity user = GetControlledEntity();
		if (!user)
			return;

		RplComponent carrierRpl = RplComponent.Cast(Replication.FindItem(carrierId));
		if (!carrierRpl)
			return;

		IEntity carrier = carrierRpl.GetEntity();
		if (!carrier || carrier == user)
			return;

		//Not a manpack: a station, whose handset hangs on the set itself
		CVON_RadioComponent radio = CVON_Manpack.GetManpackRadio(carrier);
		CVON_StationaryRadioComponent station;
		if (!radio)
		{
			station = CVON_StationaryRadioComponent.Cast(carrier.FindComponent(CVON_StationaryRadioComponent));
			radio = station;
		}
		if (!radio)
			return;

		if (radio.IsHandsetHeldBy(user))
		{
			radio.ReleaseHandsetServer();
			return;
		}

		//Someone else has it
		if (radio.GetHandsetHolder())
			return;

		//A station's: alive, within its reach
		if (station)
		{
			if (!station.IsWithinReach(user, station.m_fHandsetReach))
				return;
		}
		else if (vector.Distance(user.GetOrigin(), carrier.GetOrigin()) > CVON_Manpack.HANDSET_CORD_LENGTH)
			return;

		//One handset at a time
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode)
		{
			CVON_RadioComponent held = gamemode.GetHeldHandset(user);
			if (held)
				held.ReleaseHandsetServer();
		}
		radio.GrabHandsetServer(user);
	}

	//Pick up or hang up the infantry phone at the back of an armoured vehicle (CVON_InfantryPhoneAction)
	//==========================================================================================================================================================================
	void ToggleInfantryPhone(RplId vehicleId)
	{
		Rpc(RpcAsk_ToggleInfantryPhone, vehicleId);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_ToggleInfantryPhone(RplId vehicleId)
	{
		IEntity user = GetControlledEntity();
		if (!user)
			return;

		RplComponent vehicleRpl = RplComponent.Cast(Replication.FindItem(vehicleId));
		if (!vehicleRpl || !vehicleRpl.GetEntity())
			return;

		CVON_VehicleRadioComponent comms = CVON_VehicleRadioComponent.Cast(vehicleRpl.GetEntity().FindComponent(CVON_VehicleRadioComponent));
		if (!comms)
			return;

		if (comms.IsPhoneHeldBy(user))
		{
			comms.HangUpPhoneServer();
			return;
		}

		//Someone else has it
		if (comms.GetPhoneHolder() || !comms.HasInfantryPhone() || !comms.CanUsePhone(user, CVON_VehicleRadioComponent.PHONE_REACH))
			return;

		//One phone at a time
		CVON_VehicleRadioComponent held = CVON_VehicleComms.GetPhoneVehicle(user);
		if (held)
			held.HangUpPhoneServer();
		comms.PickUpPhoneServer(user);
	}

	//The vehicle radio we can use while in a crew seat, kept at the end of m_aRadios so the inventory
	//radios keep their keys (and their indices match the server's list)
	IEntity m_VehicleRadio;

	//Local: adds or removes the vehicle radio after getting in, out or changing seats
	//==========================================================================================================================================================================
	void AttachVehicleRadio(IEntity character)
	{
		CVON_VehicleRadioComponent radio = CVON_VehicleComms.GetOperatedRadio(character);
		IEntity radioEntity = null;
		if (radio)
			radioEntity = radio.GetOwner();

		bool newRadio = false;
		if (radioEntity)
			newRadio = radioEntity != m_VehicleRadio;
		if (m_VehicleRadio && m_VehicleRadio != radioEntity)
			m_aRadios.RemoveItemOrdered(m_VehicleRadio);
		if (radioEntity && !m_aRadios.Contains(radioEntity))
			m_aRadios.Insert(radioEntity);
		m_VehicleRadio = radioEntity;

		//A radio with no key at all (vehicle without a faction) has no traffic to keep: fill it straight away.
		//Otherwise it stays on its key until a crew member fills it (VONVehicleKeyFill keybind).
		if (newRadio && radio.m_sFactionKey == "")
			KeyFillVehicleRadio(radio.GetRplId());

		//RadioData.json and the server's frequency index follow the new radio list
		if (character)
			CVON_RadioComponent.WriteRadioData(character);
	}

	//Crew only: load our encryption key and channels into the vehicle radio (CVON_VehicleRadioComponent.KeyFillFromCrew)
	//==========================================================================================================================================================================
	void KeyFillVehicleRadio(RplId radioId)
	{
		Rpc(RpcAsk_KeyFillVehicleRadio, radioId);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_KeyFillVehicleRadio(RplId radioId)
	{
		CVON_VehicleRadioComponent radio = CVON_VehicleRadioComponent.Cast(FindRadioComponent(radioId));
		if (!radio)
			return;

		IEntity player = GetControlledEntity();
		if (CVON_VehicleComms.GetOperatedRadio(player) != radio)
			return;
		radio.KeyFillFromCrew(player);
	}

	//Only the crew (vehicle radio), the carrier (manpack, handheld) or someone at the set (station) can turn the
	//speaker on or off
	//==========================================================================================================================================================================
	void ToggleVehicleSpeaker(RplId radioId)
	{
		Rpc(RpcAsk_ToggleVehicleSpeaker, radioId);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_ToggleVehicleSpeaker(RplId radioId)
	{
		CVON_RadioComponent radio = FindRadioComponent(radioId);
		if (!radio || !radio.CanUseSpeaker())
			return;

		//Station: anyone alive at the set or holding its handset, and the first military user keys it, as
		//picking up its handset does. Vehicle radio: crew only. Carried radio: its carrier only.
		IEntity player = GetControlledEntity();
		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(radio);
		CVON_VehicleRadioComponent vehicleRadio = CVON_VehicleRadioComponent.Cast(radio);
		if (station)
		{
			if (!station.IsWithinReach(player, CVON_StationaryRadioComponent.SPEAKER_SWITCH_REACH) && !station.CanOperate(player))
				return;
			station.KeyFillFromFirstUser(player);
		}
		else if (vehicleRadio)
		{
			if (CVON_VehicleComms.GetOperatedRadio(player) != vehicleRadio)
				return;
		}
		else if (radio.GetCarrier() != player)
		{
			return;
		}
		radio.ToggleSpeakerServer();
	}

	//Only a station's handset holder may switch or tune it; other radios are not checked here
	//==========================================================================================================================================================================
	protected bool MayOperate(CVON_RadioComponent radioComp)
	{
		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(radioComp);
		return !station || station.CanOperate(GetControlledEntity());
	}

	protected CVON_RadioComponent FindRadioComponent(RplId radioId)
	{
		RplComponent rplComp = RplComponent.Cast(Replication.FindItem(radioId));
		if (!rplComp || !rplComp.GetEntity())
			return null;
		return CVON_RadioComponent.Cast(rplComp.GetEntity().FindComponent(CVON_RadioComponent));
	}

	//==========================================================================================================================================================================
	protected void InitializeInventoryRadios(IEntity to)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
			return;

		if (GetGame().GetPlayerController())
		{
			SCR_VONController vonController = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
			vonController.m_CharacterController = SCR_CharacterControllerComponent.Cast(to.FindComponent(SCR_CharacterControllerComponent));
		}
		m_aRadios.Clear();
		//Reforger Lobby bs
		m_aLocalEntries.Clear();
		array<RplId> radios = CVON_VONGameModeComponent.GetInstance().GetRadios(to, false);
		if (!radios)
			return;
		if (radios.Count() == 0)
			return;
		ref array<IEntity> shortRangeRadios = {};
		ref array<IEntity> longRangeRadios = {};
		foreach (RplId radio: radios)
		{
			if (!Replication.FindItem(radio))
				continue;

			IEntity radioObject = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
			if (!radioObject)
				continue;

			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioObject.FindComponent(CVON_RadioComponent));


			switch (radioComp.m_eRadioType)
			{
				case CVON_ERadioType.SHORT:
				{
					if (!shortRangeRadios.Contains(radioObject))
						shortRangeRadios.Insert(radioObject);
					break;
				}
				case CVON_ERadioType.LONG:
				{
					if (!longRangeRadios.Contains(radioObject))
						longRangeRadios.Insert(radioObject);
					break;
				}	
			}
		}
		if (shortRangeRadios)
			m_aRadios.InsertAll(shortRangeRadios);
		if (longRangeRadios)
			m_aRadios.InsertAll(longRangeRadios);
		RplComponent firstRplComp = RplComponent.Cast(Replication.FindItem(radios.Get(0)));
		if (!firstRplComp)
			return;
		IEntity radioEntity = firstRplComp.GetEntity();
		if (!radioEntity)
			return;
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		if (GetGame().GetPlayerController())
		{
			radioComp.WriteJSON(to);
			if (m_aRadioSettings.Count() > 0)
			{
				foreach (IEntity radio: m_aRadios)
				{
					CVON_RadioComponent radioCompSetting = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					int index = m_aRadios.Find(radio);
					if (m_aRadioSettings.Count() <= index)
						break;
					CVON_RadioSettingObject radioSetting = m_aRadioSettings.Get(index);

					radioCompSetting.m_iVolume = radioSetting.m_iVolume;
					radioCompSetting.m_eStereo = radioSetting.m_Stereo;
				}
			}
		}

		int count = m_aRadios.Count();
		if (count < 2) return;

		for (int i = 0; i < m_iAmountOfTimesRadioRotated - 1; i++)
		{
			IEntity last = m_aRadios[count - 1];

		    for (int g = count - 1; g > 0; g--)
		    {
		        m_aRadios[g] = m_aRadios[g - 1];
		    }
		    m_aRadios[0] = last;
		}

	}

	//mmmmgetter
	//==========================================================================================================================================================================
	int ReturnLocalVoiceRange()
	{
		return m_aVolumeValues.Find(CVON_VONGameModeComponent.GetInstance().GetPlayerVolume(GetLocalPlayerId()));
	}

	int ReturnLocalVoiceVolume()
	{
		return CVON_VONGameModeComponent.GetInstance().GetPlayerVolume(GetLocalPlayerId());
	}

	//Links an actual volume I can give to teamspeak to the enum in game.
	//==========================================================================================================================================================================
	void ChangeVoiceRange(int input)
	{
		Rpc(RpcDo_ChangeVoiceRange, SCR_PlayerController.GetLocalPlayerId(), input);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcDo_ChangeVoiceRange(int playerId, int input)
	{
		CVON_VONGameModeComponent.GetInstance().UpdateVolume(playerId, input);
	}

	//This is how we send our VONEntry to other clients so they can write it in their VONData.json
	//==========================================================================================================================================================================
	void BroadcastLocalVONToServer(CVON_VONContainer VONContainer, int playerId, RplId radioId)
	{
		Rpc(RpcAsk_BroadcastLocalVONToServer, VONContainer, playerId, radioId);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_BroadcastLocalVONToServer(CVON_VONContainer VONContainer, int playerId, RplId radioId)
	{
		//A station: only its handset's holder, alive at the set, on the set's own net
		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(FindRadioComponent(radioId));
		if (station && (playerId != GetPlayerId() || !station.CanOperate(GetControlledEntity()) || !station.Receives(VONContainer)))
			return;

		CVON_VONGameModeComponent.GetInstance().AddLocalVONBroadcasts(VONContainer, playerId, radioId);
	}

	//This is how the server talks directly to us, by doing an Rpc to the owner, which is the player of this controller, it sends it directly to them.
	//Performant
	//==========================================================================================================================================================================
	void AddLocalVONBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance)
	{
		Rpc(RpcAsk_AddLocalVONBroadcast, VONContainer, playerId, senderOrigin, maxDistance);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)] 
	void RpcAsk_AddLocalVONBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance)
	{
		if (m_aLocalEntries.Contains(playerId))
		{
			CVON_VONContainer vonContainerLocal = m_aLocalEntries.Get(playerId);
			vonContainerLocal.m_bPickup = false; // their own transmission replaces a pickup
			vonContainerLocal.m_eVonType = CVON_EVONType.RADIO;
			vonContainerLocal.m_sFrequency = VONContainer.m_sFrequency;
			vonContainerLocal.m_iRadioId = VONContainer.m_iRadioId;
			vonContainerLocal.m_sFactionKey = VONContainer.m_sFactionKey;
			vonContainerLocal.m_iInterference = VONContainer.m_iInterference;
			vonContainerLocal.m_iMaxDistance = maxDistance;
			vonContainerLocal.m_vSenderLocation = senderOrigin;
			vonContainerLocal.m_bCutOff = false; // a new transmission replaces a cut-off still showing
			return;
		}
		VONContainer.m_iMaxDistance = maxDistance;
		VONContainer.m_vSenderLocation = senderOrigin;
		m_aLocalEntries.Insert(playerId, VONContainer);
	}

	//A radio transmission we hear through a nearby vehicle's speaker (see BroadcastToSpeakers)
	//==========================================================================================================================================================================
	void AddLocalSpeakerBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance, RplId speakerId, int speakerRange)
	{
		Rpc(RpcAsk_AddLocalSpeakerBroadcast, VONContainer, playerId, senderOrigin, maxDistance, speakerId, speakerRange);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcAsk_AddLocalSpeakerBroadcast(CVON_VONContainer VONContainer, int playerId, vector senderOrigin, float maxDistance, RplId speakerId, int speakerRange)
	{
		CVON_VONContainer container = m_aLocalEntries.Get(playerId);
		if (!container)
		{
			container = VONContainer;
			m_aLocalEntries.Insert(playerId, container);
		}
		else if (container.m_eVonType == CVON_EVONType.RADIO && !container.m_bPickup)
		{
			return; // already heard on a radio of our own
		}

		container.m_bPickup = false; // their own transmission replaces a pickup
		container.m_eVonType = CVON_EVONType.SPEAKER;
		container.m_sFrequency = VONContainer.m_sFrequency;
		//A station's speaker checks the time deviation too (SCR_VONController.ComputeSpeakerLR)
		if (CVON_StationaryRadioComponent.Cast(FindRadioComponent(speakerId)))
			container.m_iTimeDeviation = VONContainer.m_iTimeDeviation;
		container.m_iRadioId = VONContainer.m_iRadioId;
		container.m_sFactionKey = VONContainer.m_sFactionKey;
		container.m_iInterference = VONContainer.m_iInterference;
		container.m_iMaxDistance = maxDistance;
		container.m_vSenderLocation = senderOrigin;
		container.m_SpeakerRplId = speakerId;
		container.m_iSpeakerRange = speakerRange;
		container.m_SpeakerEntity = null;
		container.m_fNextSpeakerOcclusionTime = 0;
		container.m_bCutOff = false;
	}


	//Just tells the clients who have our VON broadcast to remove it.
	//==========================================================================================================================================================================
	void BroadcastRemoveLocalVONToServer(int playerIdToRemove)
	{
		Rpc(RpcAsk_BroadcastRemoveLocalVONToServer, playerIdToRemove);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_BroadcastRemoveLocalVONToServer(int playerIdToRemove)
	{
		CVON_VONGameModeComponent.GetInstance().RemoveLocalVONBroadcasts(playerIdToRemove);	
	}

	//Same concept as above, just direct communication from server to specific client to remove a VONEntry.
	//==========================================================================================================================================================================
	void RemoveLocalVONBroadcast(int playerIdToRemove)
	{
		Rpc(RpcAsk_RemoveLocalVONBroadcast, playerIdToRemove);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcAsk_RemoveLocalVONBroadcast(int playerIdToRemove)
	{
		if (!m_aLocalEntries.Contains(playerIdToRemove))
			return;

		// A cut-off entry stays a moment longer so the plugin can play it (RemoveCutOffEntry takes it)
		CVON_VONContainer container = m_aLocalEntries.Get(playerIdToRemove);
		if (container && container.m_bCutOff)
			return;

		m_aLocalEntries.Remove(playerIdToRemove);
		// Their mic stops picking up the people around them; and if they were picked up by someone
		// else's radio, that pickup can take over again
		m_mVoicePickups.Remove(playerIdToRemove);
		ReconcileVoicePickups();
	}

	//Voice pickup (server setting m_bRadioVoicePickup, see CVON_VONGameModeComponent.UpdateVoicePickup):
	//for each sender we're receiving, the players their mic picks up and how loudly
	//==========================================================================================================================================================================
	ref map<int, ref map<int, float>> m_mVoicePickups = new map<int, ref map<int, float>>();

	void SetVoicePickup(int senderId, string list)
	{
		Rpc(RpcAsk_SetVoicePickup, senderId, list);
	}

	//list: "playerId:gain;playerId:gain;" (empty: nobody)
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcAsk_SetVoicePickup(int senderId, string list)
	{
		map<int, float> talkers = new map<int, float>();
		array<string> pairs = {};
		list.Split(";", pairs, true);
		foreach (string pair: pairs)
		{
			array<string> parts = {};
			pair.Split(":", parts, true);
			if (parts.Count() == 2)
				talkers.Set(parts[0].ToInt(), parts[1].ToFloat());
		}

		if (talkers.IsEmpty())
			m_mVoicePickups.Remove(senderId);
		else
			m_mVoicePickups.Set(senderId, talkers);
		ReconcileVoicePickups();
	}

	//Keeps a pickup entry for every player some sender's mic picks up, unless we already hear them some other
	//way (in person, on their own transmission, on the intercom). A player picked up by several senders goes
	//through the loudest. Called when a list arrives or a transmission ends, and from UpdateMembership.
	//==========================================================================================================================================================================
	void ReconcileVoicePickups()
	{
		// Loudest pickup for each talker, from senders we're actually receiving
		map<int, int> bestSender = new map<int, int>();
		map<int, float> bestGain = new map<int, float>();
		foreach (int senderId, map<int, float> talkers: m_mVoicePickups)
		{
			CVON_VONContainer senderEntry = m_aLocalEntries.Get(senderId);
			if (!talkers || !senderEntry || senderEntry.m_bPickup || (senderEntry.m_eVonType != CVON_EVONType.RADIO && senderEntry.m_eVonType != CVON_EVONType.SPEAKER))
				continue;

			foreach (int talkerId, float gain: talkers)
			{
				float best;
				if (bestGain.Find(talkerId, best) && best >= gain)
					continue;
				bestGain.Set(talkerId, gain);
				bestSender.Set(talkerId, senderId);
			}
		}

		// Pickup entries nobody picks up any more
		array<int> stale = {};
		foreach (int entryId, CVON_VONContainer entry: m_aLocalEntries)
		{
			if (entry.m_bPickup && !bestSender.Contains(entryId))
				stale.Insert(entryId);
		}
		foreach (int staleId: stale)
			m_aLocalEntries.Remove(staleId);

		PlayerManager pm = GetGame().GetPlayerManager();
		foreach (int pickedId, int pickedSender: bestSender)
		{
			if (pickedId == GetPlayerId())
				continue;

			CVON_VONContainer existing = m_aLocalEntries.Get(pickedId);
			if (existing && !existing.m_bPickup)
				continue; // heard in person, on their own radio or on the intercom

			// Dead or knocked out (when we can see them): nothing to pick up
			ChimeraCharacter pickedCharacter = ChimeraCharacter.Cast(pm.GetPlayerControlledEntity(pickedId));
			if (pickedCharacter)
			{
				CharacterControllerComponent controller = pickedCharacter.GetCharacterController();
				if (controller && controller.GetLifeState() != ECharacterLifeState.ALIVE)
				{
					if (existing)
						m_aLocalEntries.Remove(pickedId);
					continue;
				}
			}

			if (!existing)
			{
				existing = new CVON_VONContainer();
				existing.m_bPickup = true;
				existing.m_eVonType = CVON_EVONType.RADIO; // kept by UpdateMembership like other server-sent entries
				existing.m_iPlayerId = pickedId;
				existing.m_iVolume = CVON_VONGameModeComponent.GetInstance().GetPlayerVolume(pickedId);
				m_aLocalEntries.Insert(pickedId, existing);
			}
			existing.m_iClientId = GetPlayersTeamspeakClientId(pickedId);
			existing.m_iPickupSenderId = pickedSender;
			existing.m_fPickupGain = bestGain.Get(pickedId);
		}
	}

	//Cut-off: the sender was killed or knocked out mid-transmission. Like a release, but every receiver
	//hears the radio cut off with a burst of static instead of a clean squelch tail.
	//==========================================================================================================================================================================
	void BroadcastCutOffLocalVONToServer(int playerIdToRemove)
	{
		Rpc(RpcAsk_BroadcastCutOffLocalVONToServer, playerIdToRemove);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_BroadcastCutOffLocalVONToServer(int playerIdToRemove)
	{
		CVON_VONGameModeComponent.GetInstance().CutOffLocalVONBroadcasts(playerIdToRemove);
	}

	//AI hearing: this player started or stopped talking (any speech: direct, radio, intercom - their mouth is
	//where it comes from). Only sent while the server has AI hearing on (CVON_AIHearing).
	//==========================================================================================================================================================================
	void SetSpeakingForAI(bool speaking)
	{
		Rpc(RpcAsk_SetSpeakingForAI, speaking);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SetSpeakingForAI(bool speaking)
	{
		CVON_VONGameModeComponent gameMode = CVON_VONGameModeComponent.GetInstance();
		if (gameMode)
			gameMode.SetPlayerSpeakingForAI(GetPlayerId(), speaking);
	}

	//Server -> receiver: sent just before the usual remove
	//==========================================================================================================================================================================
	void CutOffLocalVONBroadcast(int playerIdToRemove)
	{
		Rpc(RpcAsk_CutOffLocalVONBroadcast, playerIdToRemove);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcAsk_CutOffLocalVONBroadcast(int playerIdToRemove)
	{
		CVON_VONContainer container = m_aLocalEntries.Get(playerIdToRemove);
		if (!container || container.m_bPickup || (container.m_eVonType != CVON_EVONType.RADIO && container.m_eVonType != CVON_EVONType.SPEAKER))
			return;

		// Written to VONData as cut off; the plugin plays the burst and silences them
		container.m_bCutOff = true;
		GetGame().GetCallqueue().CallLater(RemoveCutOffEntry, SCR_VONController.CUT_OFF_HOLD_MS, false, playerIdToRemove, container);
	}

	//Takes the cut-off entry away, unless a new transmission from them replaced it in the meantime
	//==========================================================================================================================================================================
	protected void RemoveCutOffEntry(int playerId, CVON_VONContainer container)
	{
		CVON_VONContainer current = m_aLocalEntries.Get(playerId);
		if (current && current == container && current.m_bCutOff)
		{
			m_aLocalEntries.Remove(playerId);
			m_mVoicePickups.Remove(playerId);
			ReconcileVoicePickups();
		}
	}

	//All these are described already in the radio component, this is just how we get the authority to do it from the proxy.
	//==========================================================================================================================================================================
	void ToggleRadioPower(RplId radio)
	{
		Rpc(RpcAsk_ToggleRadioPower, radio);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_ToggleRadioPower(RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;

		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp || !MayOperate(radioComp))
			return;

		radioComp.TogglePowerServer();
	}

	void UpdateRadioChannel(int input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioChannel, input, radio);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioChannel(int input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;

		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp || !MayOperate(radioComp))
			return;

		//A station's channel in range: the set is shared, and a bad channel breaks its menu for the next holder
		if (CVON_StationaryRadioComponent.Cast(radioComp) && (input < 1 || input > radioComp.m_aChannels.Count()))
			return;

		radioComp.UpdateChannelServer(input);
	}

	//==========================================================================================================================================================================
	void UpdateRadioFrequency(string input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioFrequency, input, radio);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioFrequency(string input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;

		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp || !MayOperate(radioComp))
			return;

		radioComp.UpdateFrequncyServer(input);
	}

	//==========================================================================================================================================================================
	void UpdateRadioTimeDeviation(int input, RplId radio)
	{
		Rpc(RpcAsk_UpdateRadioTimeDeviation, input, radio);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_UpdateRadioTimeDeviation(int input, RplId radio)
	{
		if (!Replication.FindItem(radio))
			return;

		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
		if (!radioEntity)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp || !MayOperate(radioComp))
			return;

		radioComp.UpdateTimeDeviationServer(input);
	}

	//==========================================================================================================================================================================
	void AddChannelServer(RplId radioId)
	{
		Rpc(RpcAsk_AddChannelServer, radioId);
	}

	//==========================================================================================================================================================================
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_AddChannelServer(RplId radioId)
	{
		if (!Replication.FindItem(radioId))
			return;

		IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
		if (!radioEntity)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
		if (!radioComp || !MayOperate(radioComp))
			return;

		radioComp.AddChannelServer();
	}

	void SetVolumeFromServer(int volume, int radioIndex)
	{
		Rpc(RpcDo_SetVolumeFromServer, volume, radioIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_SetVolumeFromServer(int volume, int radioIndex)
	{
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_aRadios.Get(radioIndex).FindComponent(CVON_RadioComponent));
		radioComp.m_iVolume = volume;
		radioComp.WriteJSON(GetLocalControlledEntity());
	}

	void SetStereoFromServer(CVON_EStereo stereo, int radioIndex)
	{
		Rpc(RpcDo_SetStereoFromServer, stereo, radioIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_SetStereoFromServer(CVON_EStereo stereo, int radioIndex)
	{
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_aRadios.Get(radioIndex).FindComponent(CVON_RadioComponent));
		radioComp.m_eStereo = stereo;
		radioComp.WriteJSON(GetLocalControlledEntity());
	}

	// Last radio list sent to the server; RadioData.json is rewritten far more often than the list changes
	string m_sLastSentRadioFreqs = "__unset__";
	string m_sLastSentFactionKeys = "__unset__";

	void UpdateFreqArray(string radioFreqs, string factionKeys)
	{
		if (radioFreqs == m_sLastSentRadioFreqs && factionKeys == m_sLastSentFactionKeys)
			return;
		m_sLastSentRadioFreqs = radioFreqs;
		m_sLastSentFactionKeys = factionKeys;
		Rpc(RpcDo_UpdateFreqArray, SCR_PlayerController.GetLocalPlayerId(), radioFreqs, factionKeys);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcDo_UpdateFreqArray(int playerId, string radioFreqs, string factionKeys)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode)
			return;
		gamemode.SetPlayerFrequencies(playerId, radioFreqs, factionKeys);

		//Holding a station's handset and raising the headset: lowering took us off every frequency, so the
		//station's calls already under way are joined again (the sender's own id, not the one in the message)
		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(gamemode.GetHeldHandset(GetControlledEntity()));
		if (!station)
			return;

		array<string> freqs = {};
		radioFreqs.Split("|", freqs, true);
		if (freqs.Contains(station.m_sFrequency))
			gamemode.JoinCallsUnderWay(GetPlayerId(), station);
	}
}