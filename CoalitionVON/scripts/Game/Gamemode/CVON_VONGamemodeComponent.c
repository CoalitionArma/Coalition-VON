//Also the VONType the plugin reads. INTERCOM is only ever written by the listener (a talker on their
//vehicle intercom); SPEAKER is a radio transmission heard through a nearby radio's speaker (a vehicle's,
//a carried radio with its speaker on, a dropped radio).
enum CVON_EVONType
{
	DIRECT,
	RADIO,
	INTERCOM,
	SPEAKER
}

[BaseContainerProps()]
class CVON_SharedFrequencyObject
{
	[Attribute()] ref array<string> m_aFactionIds;
	[Attribute()] string m_sSharedFrequency;
}

class CVON_VONGameModeComponentClass: SCR_BaseGameModeComponentClass
{
}

class CVON_VONGameModeComponent: SCR_BaseGameModeComponent
{
	//These are stored from server settings on the server so clients can know what ChannelName and whats the password to join that VOIP channel.
	[RplProp()] string m_sTeamSpeakChannelName = "";
	[RplProp()] string m_sTeamSpeakChannelPassword = "";
	[RplProp()] string m_sTeamSpeakServerIP = "";
	[RplProp()] string m_sTeamSpeakServerPassword = "";
	[RplProp()] ref array<int> m_aPlayerVolumes = {};
	[RplProp()] ref array<int> m_aPlayerClientIds = {};
	[RplProp()] ref array<int> m_aPlayerIds = {};
	string m_sTeamspeakPluginVersion = "3.2.4";
	ref map <int, string> m_PlayerFreqArray = new map <int, string>;
	ref map <int, string> m_PlayerKeyArray = new map <int, string>;

	// Server: which players have a radio on each frequency, kept current from UpdateFreqArray. A radio
	// transmission only looks at these players instead of splitting every player's frequency list and
	// scanning their inventory.
	ref map<string, ref array<int>> m_mFrequencyListeners = new map<string, ref array<int>>();
	// Server: who received each sender's current radio transmission, so the remove only goes to them
	// (it used to go to every player on every push-to-talk release).
	ref map<int, ref array<int>> m_mBroadcastReceivers = new map<int, ref array<int>>();

	//Radio listening (SetRadioListenTarget): listener -> target, target -> listeners, and each sender's
	//current transmission so a listener who starts mid-call hears the rest of it
	protected ref map<int, int> m_mRadioListenTarget = new map<int, int>();
	protected ref map<int, ref array<int>> m_mRadioListeners = new map<int, ref array<int>>();
	protected ref map<int, ref CVON_VONContainer> m_mActiveBroadcasts = new map<int, ref CVON_VONContainer>();
	protected ref map<int, float> m_mActiveBroadcastRange = new map<int, float>();

	// Server: radios that can play out loud (vehicle radios, and every carried radio once it has a key fill:
	// its speaker may be turned on, or it may be dropped), so a transmission can find the speakers tuned to
	// its frequency
	ref array<CVON_RadioComponent> m_aSpeakerRadios = {};

	// Every machine: vehicles with an infantry phone, so its holder can be put on the crew intercom
	// (CVON_VehicleComms.GetPhoneVehicle)
	ref array<CVON_VehicleRadioComponent> m_aVehicleComms = {};

	// Server: radios whose handset someone is holding, so the holder's radios can include it
	ref array<CVON_RadioComponent> m_aHeldHandsets = {};

	//Manpacks (handset, speaker) need a radio backpack as well as a long-range radio; off: any carried long-range radio counts
	[Attribute("1", desc: "Manpack radios (grab the handset, speaker) need a radio backpack worn as well as a long-range radio")]
	bool m_bManpackNeedsRadioBackpack;

	// Server, reused by BroadcastToSpeakers: the nearest speaker each player hears a transmission through
	static const float SPEAKER_SEND_MARGIN = 10; // m past the speaker range, so people walking up hear it (the client fades it in)
	ref array<int> m_aSpeakerPlayerIds = {};
	ref map<int, CVON_RadioComponent> m_mNearestSpeaker = new map<int, CVON_RadioComponent>();
	ref map<int, float> m_mNearestSpeakerDistSq = new map<int, float>();

	//If disabled everyone shares the same frequencies.
	[Attribute("1")] bool m_bUseFactionEcncryption;

	//If disabled there will be no babbel for other factions
	[Attribute("0")] bool m_bUseBabbel;

	//Mostly used so i don't lose my fucking MIND testing the mod in dedicated.
	[Attribute("1")] bool m_bTeamspeakChecks;

	[Attribute()] ref array<ref CVON_SharedFrequencyObject> m_aSharedFrequencies;

	//Server settings for this mission. A server config's missionHeader.m_CVON_Settings replaces them (see
	//CVON_ServerSettings); without either, the defaults apply.
	[Attribute(desc: "CVON server settings for this mission. The server config's missionHeader.m_CVON_Settings replaces them when present.")]
	ref CVON_ServerSettings m_MissionSettings;

	//The server settings in force, replicated to every client (ApplyServerSettings)
	static const int SETTING_TERRAIN = 1;
	static const int SETTING_HALF_DUPLEX = 2;
	static const int SETTING_SQUELCH = 4;
	static const int SETTING_CUT_OFF = 8;
	static const int SETTING_HANDHELD_SPEAKERS = 16;
	static const int SETTING_DROPPED_RADIOS = 32;
	static const int SETTING_VOICE_PICKUP = 64;
	[RplProp()] protected int m_iServerSettingFlags = SETTING_TERRAIN | SETTING_HALF_DUPLEX | SETTING_SQUELCH | SETTING_CUT_OFF | SETTING_HANDHELD_SPEAKERS | SETTING_DROPPED_RADIOS;
	[RplProp()] protected float m_fTerrainInterferenceStrength = 0.5;
	[RplProp()] protected int m_iRadioGameAudioEnforcement = CVON_ESettingEnforcement.PLAYER_CHOICE;
	//Replicated so clients only report their talking (SetSpeakingForAI) while it's on
	[RplProp()] protected float m_fAIHearing = 0;
	//Server only, while AI hearing is on
	protected ref CVON_AIHearing m_AIHearing;

	//Wow a pointer, so performant
	static CVON_VONGameModeComponent m_Instance;

	//Preset Frequency Config resource
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "", "conf class=CVON_FreqConfig")]
	ResourceName m_sFreqConfig;

	//The actual Frequency Config Resource loaded OnPostInit
	ref CVON_FreqConfig m_FreqConfig;

	//When the gamemode is created on the server we need to create a server setting file.
	//If already created lets populate that data into the gamemode for clients to us.
	//Delay is necessary
	//==========================================================================================================================================================================
	void CVON_VONGameModeComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		m_Instance = this;
		if (!System.IsConsoleApp())
			return;

		GetGame().GetCallqueue().CallLater(InitializeChannelId, 500, false);
	}

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (m_sFreqConfig != "")
			m_FreqConfig = CVON_FreqConfig.Cast(BaseContainerTools.CreateInstanceFromContainer(BaseContainerTools.LoadContainer(m_sFreqConfig).GetResource().ToBaseContainer()));

		if (Replication.IsServer())
			ApplyServerSettings();
	}

	//Server: puts the settings in force (server config, else the mission's, else the defaults) into the
	//replicated values every client reads
	//==========================================================================================================================================================================
	void ApplyServerSettings()
	{
		if (!Replication.IsServer())
			return;

		CVON_ServerSettings settings = ArmaReforgerScripted.CVON_GetServerConfigSettings();
		if (!settings)
			settings = m_MissionSettings;
		if (!settings)
			settings = new CVON_ServerSettings();

		int flags = 0;
		if (settings.m_bTerrainInterference && settings.m_fTerrainInterferenceStrength > 0)
			flags |= SETTING_TERRAIN;
		if (settings.m_eRadioDuplex != CVON_ERadioDuplex.FULL)
			flags |= SETTING_HALF_DUPLEX;
		if (settings.m_bSquelchTones)
			flags |= SETTING_SQUELCH;
		if (settings.m_bTransmissionCutOff)
			flags |= SETTING_CUT_OFF;
		if (settings.m_bHandheldSpeakers)
			flags |= SETTING_HANDHELD_SPEAKERS;
		if (settings.m_bDroppedRadiosTransmit)
			flags |= SETTING_DROPPED_RADIOS;
		if (settings.m_bRadioVoicePickup)
			flags |= SETTING_VOICE_PICKUP;

		m_iServerSettingFlags = flags;
		m_fTerrainInterferenceStrength = Math.Clamp(settings.m_fTerrainInterferenceStrength, 0, 1);
		m_iRadioGameAudioEnforcement = settings.m_eRadioGameAudio;
		m_fAIHearing = Math.Clamp(settings.m_fAIHearing, 0, 1);
		if (m_fAIHearing > 0 && !m_AIHearing)
			m_AIHearing = new CVON_AIHearing();
		if (m_AIHearing)
			m_AIHearing.SetStrength(m_fAIHearing);
		Replication.BumpMe();
	}

	//How keenly enemy AI hear players talking; 0 = off (CVON_AIHearing)
	//==========================================================================================================================================================================
	float GetAIHearing()
	{
		return m_fAIHearing;
	}

	//Server: a player started or stopped talking (SCR_PlayerController.SetSpeakingForAI)
	//==========================================================================================================================================================================
	void SetPlayerSpeakingForAI(int playerId, bool speaking)
	{
		if (m_AIHearing)
			m_AIHearing.SetSpeaking(playerId, speaking);
	}

	//==========================================================================================================================================================================
	bool IsServerSettingOn(int setting)
	{
		return (m_iServerSettingFlags & setting) != 0;
	}

	//The terrain formula's multiplier (ComputeTerrainFactor): the 0-1 setting doubled, so the default 0.5 is
	//the normal 1 and the setting's 1 the strongest, 2
	float GetTerrainInterferenceStrength()
	{
		if (!IsServerSettingOn(SETTING_TERRAIN))
			return 0;
		return m_fTerrainInterferenceStrength * 2;
	}

	CVON_ESettingEnforcement GetRadioGameAudioEnforcement()
	{
		switch (m_iRadioGameAudioEnforcement)
		{
			case CVON_ESettingEnforcement.FORCE_ON: return CVON_ESettingEnforcement.FORCE_ON;
			case CVON_ESettingEnforcement.FORCE_OFF: return CVON_ESettingEnforcement.FORCE_OFF;
		}
		return CVON_ESettingEnforcement.PLAYER_CHOICE;
	}

	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);
		m_aPlayerVolumes.Insert(SCR_PlayerController.m_aVolumeValues[2]);
		m_aPlayerClientIds.Insert(0);
		m_aPlayerIds.Insert(playerId);
		Replication.BumpMe();
	}

	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);
		if (Replication.IsServer())
		{
			//Talking on a station when they left: the receivers hear it cut off, as when the holder is killed
			//(the station only notices the holder is gone on its next handset check)
			if (IsServerSettingOn(SETTING_CUT_OFF) && GetCallStation(playerId))
				CutOffLocalVONBroadcasts(playerId);
			RemoveLocalVONBroadcasts(playerId);
			RemoveFromFrequencyIndex(playerId);
			StopRadioListeningFor(playerId);
			if (m_AIHearing)
				m_AIHearing.OnPlayerLeft(playerId);
		}
		if (m_PlayerFreqArray.Contains(playerId))
			m_PlayerFreqArray.Remove(playerId);

		if (m_PlayerKeyArray.Contains(playerId))
			m_PlayerKeyArray.Remove(playerId);

		if (!m_aPlayerIds.Contains(playerId))
			return;

		int index = m_aPlayerIds.Find(playerId);
		m_aPlayerVolumes.RemoveOrdered(index);
		m_aPlayerClientIds.RemoveOrdered(index);
		m_aPlayerIds.RemoveOrdered(index);
		Replication.BumpMe();
	}

	void UpdateClientId(int playerId, int clientId)
	{
		int index = m_aPlayerIds.Find(playerId);
		m_aPlayerClientIds.Set(index, clientId);
		Replication.BumpMe();
	}

	//Explained above
	//==========================================================================================================================================================================
	void InitializeChannelId()
	{
		JsonLoadContext JSONLoad = new JsonLoadContext();
		if (!JSONLoad.LoadFromFile("$profile:/VONServerSettings.json"))
		{
			JsonSaveContext ServerJSON = new JsonSaveContext();
			ServerJSON.StartObject("Server Settings");
			ServerJSON.WriteValue("VONChannelName", "");
			ServerJSON.WriteValue("VONChannelPassword", "");
			ServerJSON.WriteValue("TeamspeakServerIP", "");
			ServerJSON.WriteValue("TeamspeakServerPassword", "");
			ServerJSON.EndObject();
			ServerJSON.SaveToFile("$profile:/VONServerSettings.json");
		}
		else
		{
			JSONLoad.StartObject("Server Settings");
			bool name = JSONLoad.ReadValue("VONChannelName", m_sTeamSpeakChannelName);
			bool pass = JSONLoad.ReadValue("VONChannelPassword", m_sTeamSpeakChannelPassword);
			bool ip = JSONLoad.ReadValue("TeamspeakServerIP", m_sTeamSpeakServerIP);
			bool serverPass = JSONLoad.ReadValue("TeamspeakServerPassword", m_sTeamSpeakServerPassword);
			JSONLoad.EndObject();
			if (!name || !pass || !ip || !serverPass)
			{
				JsonSaveContext ServerJSON = new JsonSaveContext();
				ServerJSON.StartObject("Server Settings");
				ServerJSON.WriteValue("VONChannelName", m_sTeamSpeakChannelName);
				ServerJSON.WriteValue("VONChannelPassword", m_sTeamSpeakChannelPassword);
				ServerJSON.WriteValue("TeamspeakServerIP", m_sTeamSpeakServerIP);
				ServerJSON.WriteValue("TeamspeakServerPassword", m_sTeamSpeakServerPassword);
				ServerJSON.EndObject();

				ServerJSON.SaveToFile("$profile:/VONServerSettings.json");
			}
			Replication.BumpMe();
		}
	}

	//mmmm more pointer
	//==========================================================================================================================================================================
	static CVON_VONGameModeComponent GetInstance()
	{
		return m_Instance;
	}

	void UpdateVolume(int playerId, int input)
	{
		int index = m_aPlayerIds.Find(playerId);
		if (index == -1)
			return;
		int currentVolume = SCR_PlayerController.m_aVolumeValues.Find(m_aPlayerVolumes.Get(index));

		if (currentVolume == 0 && input == -1)
			return;

		if (currentVolume == 4 && input == 1)
			return;

		currentVolume += input;
		int newVolume = 0;
		switch(currentVolume)
		{
			case 0: {newVolume = SCR_PlayerController.m_aVolumeValues[0];  break;}
			case 1: {newVolume = SCR_PlayerController.m_aVolumeValues[1]; break;}
			case 2: {newVolume = SCR_PlayerController.m_aVolumeValues[2]; break;}
			case 3: {newVolume = SCR_PlayerController.m_aVolumeValues[3]; break;}
			case 4: {newVolume = SCR_PlayerController.m_aVolumeValues[4]; break;}
		}
		m_aPlayerVolumes.Set(index, newVolume);
		Replication.BumpMe();
	}

	int GetPlayerVolume(int playerId)
	{
		int index = m_aPlayerIds.Find(playerId);
		if (index == -1)
			return SCR_PlayerController.m_aVolumeValues[2]; // default mid volume
		return m_aPlayerVolumes.Get(index);
	}

	//I go into more detail in the VONController but this broadcasts to any player in the playerId array
	//This makes it so only the players that need to get the VONEntry get it, saving on that sweet sweet sweet network.
	//Broadcasts are only tracked on the clients, server doesn't track it anywhere.
	//==========================================================================================================================================================================
	void AddLocalVONBroadcasts(CVON_VONContainer VONContainer, int playerIdToAdd, RplId radioId)
	{
		int maxDist = 0;
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gamemode)
			return;
		if (radioId != RplId.Invalid())
		{
			if (!Replication.FindItem(radioId))
				return;

			IEntity radio = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
			if (!radio)
				return;

			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			if (!radioComp)
				return;

			maxDist = radioComp.m_iRadioRange;
		}

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		IEntity sender = pm.GetPlayerControlledEntity(playerIdToAdd);
		if (!sender)
			return;
		vector senderOrigin = sender.GetOrigin();
		//The call's radio, for a station's handset and speaker (the codec doesn't carry it)
		m_mCallRadios.Set(playerIdToAdd, radioId);
		m_mActiveBroadcasts.Set(playerIdToAdd, VONContainer);
		m_mActiveBroadcastRange.Set(playerIdToAdd, maxDist);

		array<int> receivers;
		if (!m_mBroadcastReceivers.Find(playerIdToAdd, receivers) || !receivers)
		{
			receivers = {};
			m_mBroadcastReceivers.Set(playerIdToAdd, receivers);
		}

		// Nobody may have the frequency on a radio of their own and a vehicle speaker still be tuned to it
		array<int> listeners;
		m_mFrequencyListeners.Find(VONContainer.m_sFrequency, listeners);
		if (listeners)
		{
			foreach (int playerId: listeners)
			{
				if (playerId == playerIdToAdd)
					continue;

				IEntity player = pm.GetPlayerControlledEntity(playerId);
				if (!player)
					continue;

				array<IEntity> radios = GetRadioEntities(player);

				bool radioOnTimeAndKeysMatch = false;
				if (radios.Count() > 0)
				{
					foreach (IEntity radio: radios)
					{
						if (!radio.FindComponent(CVON_RadioComponent))
							continue;

						CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
						//Not the radio we are looking for
						if (radioComp.m_sFrequency != VONContainer.m_sFrequency)
							continue;

						if (radioComp.m_iTimeDeviation != VONContainer.m_iTimeDeviation)
							continue;

						if (gamemode.IsEncryptionEnabled())
							if (VONContainer.m_sFactionKey != radioComp.m_sFactionKey)
								continue;

						radioOnTimeAndKeysMatch = true;
					}
				}

				if (!radioOnTimeAndKeysMatch)
					continue;

				SCR_PlayerController receiverController = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
				if (!receiverController)
					continue;
				receiverController.AddLocalVONBroadcast(VONContainer, playerIdToAdd, senderOrigin, maxDist);
				if (!receivers.Contains(playerId))
					receivers.Insert(playerId);

				ForwardToRadioListeners(playerId, VONContainer, playerIdToAdd, senderOrigin, maxDist, receivers);
			}
		}

		if (!m_aSpeakerRadios.IsEmpty())
		{
			BroadcastToSpeakers(VONContainer, playerIdToAdd, radioId, senderOrigin, maxDist, receivers, gamemode.IsEncryptionEnabled());
			//Only while a station's speaker plays: people walking up to it join the call (SpeakerTick)
			StartSpeakerTicking();
		}

		if (IsServerSettingOn(SETTING_VOICE_PICKUP))
		{
			m_mVoicePickupSent.Remove(playerIdToAdd);
			UpdateVoicePickup(playerIdToAdd, receivers);
			StartVoicePickupTicking();
		}
	}

	//Voice pickup (m_bRadioVoicePickup): a transmitting radio's mic picks up the players talking around the
	//sender. Each receiver plays those players' own voices through the same radio (SCR_PlayerController
	//SetVoicePickup), so the sender's voice stream stays clean: nobody standing next to the sender hears
	//themselves echo back through it. The server works out who is close enough and how loud when a
	//transmission starts, then once a second while it's under way, and only resends a list when it changes.
	//==========================================================================================================================================================================
	static const int VOICE_PICKUP_TICK_MS = 1000;	// background voices don't need quick updates; keeps the RPCs down
	static const float VOICE_PICKUP_RANGE_SHARE = 0.5;	// a mic hears a talker out to this share of their voice range
	static const float VOICE_PICKUP_LEVEL = 0.35;		// loudness right next to the mic, against the sender's own voice (the plugin also caps it at half the sender's level)
	static const float VOICE_PICKUP_STEP = 0.05;		// gains are rounded to this, so small moves don't resend
	protected bool m_bVoicePickupTicking;
	// Last list sent for each sender ("playerId:gain;..." plus the receiver count)
	protected ref map<int, string> m_mVoicePickupSent = new map<int, string>();
	protected ref array<int> m_aVoicePickupPlayers = {};

	protected void StartVoicePickupTicking()
	{
		if (m_bVoicePickupTicking)
			return;
		m_bVoicePickupTicking = true;
		GetGame().GetCallqueue().CallLater(VoicePickupTick, VOICE_PICKUP_TICK_MS);
	}

	//Runs while transmissions are under way; stops by itself when there are none
	protected void VoicePickupTick()
	{
		if (!IsServerSettingOn(SETTING_VOICE_PICKUP) || m_mBroadcastReceivers.IsEmpty())
		{
			m_bVoicePickupTicking = false;
			return;
		}
		GetGame().GetCallqueue().CallLater(VoicePickupTick, VOICE_PICKUP_TICK_MS);

		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			if (receivers)
				UpdateVoicePickup(senderId, receivers);
		}
	}

	//Who the sender's mic picks up, sent to each receiver (minus the receiver themselves)
	protected void UpdateVoicePickup(int senderId, array<int> receivers)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		IEntity sender = pm.GetPlayerControlledEntity(senderId);
		if (!sender)
			return;

		vector mic = sender.GetOrigin();
		array<int> ids = {};
		array<float> gains = {};
		m_aVoicePickupPlayers.Clear();
		pm.GetPlayers(m_aVoicePickupPlayers);
		foreach (int playerId: m_aVoicePickupPlayers)
		{
			if (playerId == senderId)
				continue;

			ChimeraCharacter talker = ChimeraCharacter.Cast(pm.GetPlayerControlledEntity(playerId));
			if (!talker)
				continue;
			CharacterControllerComponent controller = talker.GetCharacterController();
			if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE)
				continue;

			float reach = GetPlayerVolume(playerId) * VOICE_PICKUP_RANGE_SHARE;
			float distance = vector.Distance(talker.GetOrigin(), mic);
			if (reach <= 0 || distance >= reach)
				continue;

			float closeness = 1 - distance / reach;
			float gain = Math.Round(VOICE_PICKUP_LEVEL * closeness * closeness / VOICE_PICKUP_STEP) * VOICE_PICKUP_STEP;
			if (gain < VOICE_PICKUP_STEP)
				continue;

			ids.Insert(playerId);
			gains.Insert(gain);
		}

		string all = "";
		foreach (int allIndex, int allId: ids)
			all += allId.ToString() + ":" + gains[allIndex].ToString() + ";";
		string signature = all + "#" + receivers.Count().ToString();
		string previous;
		if (m_mVoicePickupSent.Find(senderId, previous) && previous == signature)
			return;
		m_mVoicePickupSent.Set(senderId, signature);

		foreach (int receiverId: receivers)
		{
			if (receiverId == senderId)
				continue;

			SCR_PlayerController receiverController = SCR_PlayerController.Cast(pm.GetPlayerController(receiverId));
			if (!receiverController)
				continue;

			// Nobody hears their own voice back through the radio
			string list = "";
			foreach (int listIndex, int listId: ids)
			{
				if (listId != receiverId)
					list += listId.ToString() + ":" + gains[listIndex].ToString() + ";";
			}
			receiverController.SetVoicePickup(senderId, list);
		}
	}

	//Speakers: players near a radio playing out loud (a vehicle's, a carried radio with its speaker on, a
	//dropped radio) on the transmission's frequency hear it from that radio, unless they already have it on
	//a radio of their own. Each player gets their nearest speaker (one voice per talker); a listener also
	//near the sender hears whichever of the speaker and the sender's own voice is louder for them (worked
	//out on their client, see SCR_VONController.WriteJSON). stationsOnly: only station speakers (SpeakerTick).
	//onlyPlayerId: only that player (a station's handset put down, ReleaseStationHandset; a station speaker lost,
	//DropLostStationSpeakerListeners).
	//==========================================================================================================================================================================
	protected void BroadcastToSpeakers(CVON_VONContainer VONContainer, int senderId, RplId transmittingRadio, vector senderOrigin, float maxDist, array<int> receivers, bool encryption, bool stationsOnly = false, int onlyPlayerId = 0)
	{
		PlayerManager pm = GetGame().GetPlayerManager();

		m_aSpeakerPlayerIds.Clear();
		if (onlyPlayerId > 0)
			m_aSpeakerPlayerIds.Insert(onlyPlayerId);
		else
			pm.GetPlayers(m_aSpeakerPlayerIds);
		m_mNearestSpeaker.Clear();
		m_mNearestSpeakerDistSq.Clear();

		foreach (CVON_RadioComponent speaker: m_aSpeakerRadios)
		{
			// Cheapest checks first: most registered radios are on other frequencies
			if (!speaker || speaker.m_sFrequency != VONContainer.m_sFrequency || speaker.m_iTimeDeviation != VONContainer.m_iTimeDeviation)
				continue;

			if (encryption && speaker.m_sFactionKey != VONContainer.m_sFactionKey)
				continue;

			if (stationsOnly && !CVON_StationaryRadioComponent.Cast(speaker))
				continue;

			if (!speaker.HasActiveSpeaker())
				continue;

			// A radio doesn't play its own transmission
			if (speaker.GetRplId() == transmittingRadio)
				continue;

			IEntity speakerEntity = speaker.GetSpeakerEntity();
			if (!speakerEntity)
				continue;

			// Out of the transmitting radio's range
			vector speakerOrigin = speakerEntity.GetOrigin();
			if (maxDist > 0 && vector.DistanceSq(speakerOrigin, senderOrigin) > maxDist * maxDist)
				continue;

			float reach = speaker.GetSpeakerRange() + SPEAKER_SEND_MARGIN;
			float reachSq = reach * reach;
			foreach (int playerId: m_aSpeakerPlayerIds)
			{
				if (playerId == senderId || receivers.Contains(playerId))
					continue;

				IEntity player = pm.GetPlayerControlledEntity(playerId);
				if (!player)
					continue;

				vector origin = player.GetOrigin();
				float distSq = vector.DistanceSq(origin, speakerOrigin);
				if (distSq > reachSq)
					continue;

				float nearestSq;
				if (m_mNearestSpeakerDistSq.Find(playerId, nearestSq) && nearestSq <= distSq)
					continue;

				m_mNearestSpeakerDistSq.Set(playerId, distSq);
				m_mNearestSpeaker.Set(playerId, speaker);
			}
		}

		foreach (int playerId, CVON_RadioComponent speaker: m_mNearestSpeaker)
		{
			SCR_PlayerController receiverController = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
			if (!receiverController)
				continue;

			receiverController.AddLocalSpeakerBroadcast(VONContainer, senderId, senderOrigin, maxDist, speaker.GetRplId(), speaker.GetSpeakerRange());
			receivers.Insert(playerId);

			//A station's: kept for the hand-off to another station speaker (SpeakerTick)
			if (CVON_StationaryRadioComponent.Cast(speaker))
			{
				map<int, CVON_RadioComponent> stationListeners = m_mStationSpeakerListeners.Get(senderId);
				if (!stationListeners)
				{
					stationListeners = new map<int, CVON_RadioComponent>();
					m_mStationSpeakerListeners.Set(senderId, stationListeners);
				}
				stationListeners.Set(playerId, speaker);
			}
		}
	}

	//Station speakers join calls already under way: a speaker switched on mid-call, or someone walking up to
	//one, hears the rest of it, and a listener moves to another station speaker on the net when theirs goes
	//off, is retuned or is left behind. BroadcastToSpeakers otherwise runs only when a transmission starts.
	//Only station speakers take part. The checks run while a call is under way and a station speaker plays,
	//or while someone still hears a call through a station speaker that went off, was retuned or was deleted
	//(they are handed on as above, or lose the call); then they stop until a station speaker plays again.
	//==========================================================================================================================================================================
	static const int SPEAKER_TICK_MS = 500;	// ms between station speaker checks while a call is under way
	protected bool m_bSpeakerTicking;
	protected bool m_bStationSpeakerRegistered;	// a station is in m_aSpeakerRadios: until then a server has no station speaker to look for
	// Per sender: who hears the call through which station speaker (BroadcastToSpeakers)
	protected ref map<int, ref map<int, CVON_RadioComponent>> m_mStationSpeakerListeners = new map<int, ref map<int, CVON_RadioComponent>>();
	// Per sender: the radio the call is on (AddLocalVONBroadcasts)
	protected ref map<int, RplId> m_mCallRadios = new map<int, RplId>();

	//The radio the sender is talking on, else invalid
	//==========================================================================================================================================================================
	protected RplId GetCallRadio(int senderId)
	{
		RplId radio;
		if (!m_mCallRadios.Find(senderId, radio))
			return RplId.Invalid();
		return radio;
	}

	//The station the sender is talking on, else null
	//==========================================================================================================================================================================
	protected CVON_StationaryRadioComponent GetCallStation(int senderId)
	{
		RplId radioId = GetCallRadio(senderId);
		if (radioId == RplId.Invalid())
			return null;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(radioId));
		if (!rpl || !rpl.GetEntity())
			return null;
		return CVON_StationaryRadioComponent.Cast(rpl.GetEntity().FindComponent(CVON_RadioComponent));
	}

	//Server: starts the station speaker checks unless they run already or no station's speaker plays
	//==========================================================================================================================================================================
	void StartSpeakerTicking()
	{
		if (m_bSpeakerTicking || !HasStationSpeaker())
			return;
		m_bSpeakerTicking = true;
		GetGame().GetCallqueue().CallLater(SpeakerTick, SPEAKER_TICK_MS);
	}

	//A station's speaker is playing
	//==========================================================================================================================================================================
	protected bool HasStationSpeaker()
	{
		if (!m_bStationSpeakerRegistered)
			return false;

		foreach (CVON_RadioComponent speaker: m_aSpeakerRadios)
		{
			if (CVON_StationaryRadioComponent.Cast(speaker) && speaker.HasActiveSpeaker())
				return true;
		}
		return false;
	}

	//Runs while transmissions are under way and a station's speaker plays or a station speaker listener is
	//left to hand on; stops by itself otherwise
	//==========================================================================================================================================================================
	protected void SpeakerTick()
	{
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		bool stationPlays = HasStationSpeaker();
		if (!gamemode || m_mBroadcastReceivers.IsEmpty() || (!stationPlays && m_mStationSpeakerListeners.IsEmpty()))
		{
			m_bSpeakerTicking = false;
			return;
		}
		GetGame().GetCallqueue().CallLater(SpeakerTick, SPEAKER_TICK_MS);

		PlayerManager pm = GetGame().GetPlayerManager();
		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			CVON_VONContainer container = m_mActiveBroadcasts.Get(senderId);
			IEntity sender = pm.GetPlayerControlledEntity(senderId);
			if (!receivers || !container || !sender)
				continue;

			vector senderOrigin = sender.GetOrigin();
			float maxDist = m_mActiveBroadcastRange.Get(senderId);
			DropLostStationSpeakerListeners(senderId, container, receivers, senderOrigin, maxDist, gamemode.IsEncryptionEnabled());
			if (stationPlays)
				BroadcastToSpeakers(container, senderId, GetCallRadio(senderId), senderOrigin, maxDist, receivers, gamemode.IsEncryptionEnabled(), true);
		}
	}

	//Hand-off: a listener whose station speaker was switched off or retuned, or who walked out of its range,
	//goes on hearing the call through the player they listen to if that player hears it, else through the
	//nearest other speaker playing it, else not at all. Listeners of any other speaker aren't in
	//m_mStationSpeakerListeners.
	//==========================================================================================================================================================================
	protected void DropLostStationSpeakerListeners(int senderId, CVON_VONContainer container, array<int> receivers, vector senderOrigin, float maxDist, bool encryption)
	{
		map<int, CVON_RadioComponent> listeners = m_mStationSpeakerListeners.Get(senderId);
		if (!listeners)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		array<int> lost = {};
		foreach (int playerId, CVON_RadioComponent speaker: listeners)
		{
			IEntity player = pm.GetPlayerControlledEntity(playerId);
			IEntity speakerEntity;
			if (speaker)
				speakerEntity = speaker.GetSpeakerEntity();
			if (!player || !speakerEntity || !speaker.HasActiveSpeaker() || !speaker.Receives(container)
				|| vector.Distance(player.GetOrigin(), speakerEntity.GetOrigin()) > speaker.GetSpeakerRange() + SPEAKER_SEND_MARGIN)
				lost.Insert(playerId);
		}

		//Each first gets the nearest other speaker playing the call, if any. A speaker entry replaces a speaker
		//entry, so the old one goes only when nothing plays it to them.
		array<int> dropped = {};
		foreach (int playerId: lost)
		{
			listeners.Remove(playerId);
			if (!receivers.Contains(playerId))
				continue;

			dropped.Insert(playerId);
			receivers.RemoveItem(playerId);
			BroadcastToSpeakers(container, senderId, GetCallRadio(senderId), senderOrigin, maxDist, receivers, encryption, false, playerId);
		}

		//Then, once all of them are settled, a radio entry, as SetRadioListenTarget gives it, while the player they
		//listen to hears it. Repeated while it keeps someone new, as that player may have been one of them.
		array<int> onTarget = {};
		bool keptNew = true;
		while (keptNew)
		{
			keptNew = false;
			foreach (int playerId: dropped)
			{
				int listenTarget;
				m_mRadioListenTarget.Find(playerId, listenTarget);
				if (listenTarget <= 0 || onTarget.Contains(playerId) || !receivers.Contains(listenTarget))
					continue;

				onTarget.Insert(playerId);
				keptNew = true;
				listeners.Remove(playerId);
				if (!receivers.Contains(playerId))
					receivers.Insert(playerId);
				SCR_PlayerController targetListener = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
				if (targetListener)
					targetListener.AddLocalVONBroadcast(container, senderId, senderOrigin, maxDist);
			}
		}

		foreach (int playerId: dropped)
		{
			if (receivers.Contains(playerId))
				continue;

			SCR_PlayerController playerController = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
			if (playerController)
				playerController.RemoveLocalVONBroadcast(senderId);
		}

		//None left for this call: the checks can stop once no station speaker plays
		if (listeners.IsEmpty())
			m_mStationSpeakerListeners.Remove(senderId);
	}

	void RegisterSpeakerRadio(CVON_RadioComponent radio)
	{
		if (!m_aSpeakerRadios.Contains(radio))
			m_aSpeakerRadios.Insert(radio);
		if (CVON_StationaryRadioComponent.Cast(radio))
			m_bStationSpeakerRegistered = true;
	}

	void UnregisterSpeakerRadio(CVON_RadioComponent radio)
	{
		m_aSpeakerRadios.RemoveItem(radio);
	}

	void RegisterVehicleComms(CVON_VehicleRadioComponent comms)
	{
		if (!m_aVehicleComms.Contains(comms))
			m_aVehicleComms.Insert(comms);
	}

	void UnregisterVehicleComms(CVON_VehicleRadioComponent comms)
	{
		m_aVehicleComms.RemoveItem(comms);
	}

	//Server: someone picked up or let go of a radio's handset (holder: who). A station's holder joins the calls
	//already under way on it; letting go ends their own call on it and takes back what only the station gave them.
	//==========================================================================================================================================================================
	void SetHandsetHeld(CVON_RadioComponent radio, bool held, IEntity holder = null)
	{
		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(radio);
		if (!held)
		{
			m_aHeldHandsets.RemoveItem(radio);
			if (station && holder)
				ReleaseStationHandset(station, holder);
			return;
		}
		if (!m_aHeldHandsets.Contains(radio))
			m_aHeldHandsets.Insert(radio);
		if (station && holder)
			JoinCallsUnderWay(GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(holder), station);
	}

	//The holder let go of a station's handset. Their own call on it ends first, before the release reaches any
	//client: cut off when they were killed or are gone (so the receivers still get the burst, not a clean
	//release that got there first), a normal end otherwise. Then the calls they heard only on the station stop,
	//or carry on through a speaker they stand near.
	//==========================================================================================================================================================================
	protected void ReleaseStationHandset(CVON_StationaryRadioComponent station, IEntity holder)
	{
		int holderId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(holder);
		int callerId;
		RplId stationId = station.GetRplId();
		foreach (int senderId, RplId callRadio: m_mCallRadios)
		{
			//The holder's own call on the set; anyone's when the holder is gone
			if (callRadio == stationId && (holderId <= 0 || senderId == holderId))
			{
				callerId = senderId;
				break;
			}
		}

		bool alive = false;
		ChimeraCharacter holderCharacter = ChimeraCharacter.Cast(holder);
		if (holderId > 0 && holderCharacter && holderCharacter.GetCharacterController())
			alive = holderCharacter.GetCharacterController().GetLifeState() == ECharacterLifeState.ALIVE;

		if (callerId > 0)
		{
			if (!alive && IsServerSettingOn(SETTING_CUT_OFF))
				CutOffLocalVONBroadcasts(callerId);
			else
				RemoveLocalVONBroadcasts(callerId);
		}

		if (holderId <= 0)
			return;

		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		PlayerManager pm = GetGame().GetPlayerManager();
		SCR_PlayerController holderController = SCR_PlayerController.Cast(pm.GetPlayerController(holderId));
		int listenTarget;
		m_mRadioListenTarget.Find(holderId, listenTarget);
		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			if (!receivers || !receivers.Contains(holderId))
				continue;

			//Left as it is: a call not on the station's net, one another of their radios has, or one the player
			//they listen to hears
			CVON_VONContainer container = m_mActiveBroadcasts.Get(senderId);
			if (!station.Receives(container) || (gamemode && HasMatchingRadio(holder, container, gamemode.IsEncryptionEnabled()))
				|| (listenTarget > 0 && receivers.Contains(listenTarget)))
				continue;

			receivers.RemoveItem(holderId);
			if (holderController)
				holderController.RemoveLocalVONBroadcast(senderId);

			//Still near a speaker playing it: heard from that speaker from now on. A speaker entry can't
			//replace one on a radio, so the radio entry goes first.
			IEntity sender = pm.GetPlayerControlledEntity(senderId);
			if (sender && gamemode)
				BroadcastToSpeakers(container, senderId, GetCallRadio(senderId), sender.GetOrigin(), m_mActiveBroadcastRange.Get(senderId), receivers, gamemode.IsEncryptionEnabled(), false, holderId);
		}
	}

	//A player who has a station's handset in hand again (picked it up, raised their headset) hears the calls
	//already under way on it, as SetRadioListenTarget does for a listener. Sent again to a receiver who
	//already has the call, so an entry heard through a speaker turns into one on the radio.
	//==========================================================================================================================================================================
	void JoinCallsUnderWay(int playerId, CVON_StationaryRadioComponent radio)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		SCR_PlayerController playerController = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
		if (!playerController)
			return;

		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			if (!receivers || senderId == playerId)
				continue;

			CVON_VONContainer container = m_mActiveBroadcasts.Get(senderId);
			IEntity sender = pm.GetPlayerControlledEntity(senderId);
			if (!sender || !radio.Receives(container))
				continue;

			vector senderOrigin = sender.GetOrigin();
			float maxDist = m_mActiveBroadcastRange.Get(senderId);
			playerController.AddLocalVONBroadcast(container, senderId, senderOrigin, maxDist);
			if (!receivers.Contains(playerId))
				receivers.Insert(playerId);
			ForwardToRadioListeners(playerId, container, senderId, senderOrigin, maxDist, receivers);

			//On the handset now, not a station speaker's listener any more
			map<int, CVON_RadioComponent> stationListeners = m_mStationSpeakerListeners.Get(senderId);
			if (stationListeners)
				stationListeners.Remove(playerId);
		}
	}

	//The radio whose handset this character is holding, or null
	CVON_RadioComponent GetHeldHandset(IEntity character)
	{
		if (!character)
			return null;

		//Our own character on a client: tracked by the player controller
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc && pc.m_HandsetRadio && character == pc.GetControlledEntity())
		{
			CVON_RadioComponent own = CVON_RadioComponent.Cast(pc.m_HandsetRadio.FindComponent(CVON_RadioComponent));
			if (own && own.IsHandsetHeldBy(character))
				return own;
		}

		foreach (CVON_RadioComponent radio: m_aHeldHandsets)
		{
			if (radio && radio.IsHandsetHeldBy(character))
				return radio;
		}
		return null;
	}

	//Server: keeps m_mFrequencyListeners in step with each player's radios ("freq|freq|" strings)
	//==========================================================================================================================================================================
	void SetPlayerFrequencies(int playerId, string radioFreqs, string factionKeys)
	{
		RemoveFromFrequencyIndex(playerId);
		m_PlayerFreqArray.Set(playerId, radioFreqs);
		m_PlayerKeyArray.Set(playerId, factionKeys);

		array<string> freqs = {};
		radioFreqs.Split("|", freqs, true);
		foreach (string freq: freqs)
		{
			array<int> listeners;
			if (!m_mFrequencyListeners.Find(freq, listeners) || !listeners)
			{
				listeners = {};
				m_mFrequencyListeners.Set(freq, listeners);
			}
			if (!listeners.Contains(playerId))
				listeners.Insert(playerId);
		}
	}

	void RemoveFromFrequencyIndex(int playerId)
	{
		string oldFreqs;
		if (!m_PlayerFreqArray.Find(playerId, oldFreqs))
			return;

		array<string> freqs = {};
		oldFreqs.Split("|", freqs, true);
		foreach (string freq: freqs)
		{
			array<int> listeners;
			if (!m_mFrequencyListeners.Find(freq, listeners) || !listeners)
				continue;
			listeners.RemoveItem(playerId);
			if (listeners.IsEmpty())
				m_mFrequencyListeners.Remove(freq);
		}
	}

	array<string> GetFreqsFromFreqMap(int playerId)
	{
		string freqs = m_PlayerFreqArray.Get(playerId);
		array<string> freq = {};
		freqs.Split("|", freq, false);
		return freq;
	}

	array<string> GetFactionKeysFromFreqMap(int playerId)
	{
		string freqs = m_PlayerKeyArray.Get(playerId);
		array<string> factionKeys = {};
		freqs.Split("|", factionKeys, false);
		return factionKeys;
	}


	//Same as above braodcasts only to the player that specifically needs to remove the broadcast
	//==========================================================================================================================================================================
	void RemoveLocalVONBroadcasts(int playerIdToRemove)
	{
		m_mActiveBroadcasts.Remove(playerIdToRemove);
		m_mActiveBroadcastRange.Remove(playerIdToRemove);
		m_mVoicePickupSent.Remove(playerIdToRemove); // receivers drop the pickup with the transmission
		m_mCallRadios.Remove(playerIdToRemove);
		m_mStationSpeakerListeners.Remove(playerIdToRemove); // only ever holds station speaker listeners

		array<int> receivers;
		if (!m_mBroadcastReceivers.Find(playerIdToRemove, receivers) || !receivers)
			return;
		m_mBroadcastReceivers.Remove(playerIdToRemove);

		foreach (int playerId: receivers)
		{
			if (playerId == playerIdToRemove)
				continue;

			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
			if (!pc)
				continue;

			pc.RemoveLocalVONBroadcast(playerIdToRemove);
		}
	}

	//The sender was killed or knocked out mid-transmission: every receiver is told it was cut off, then the
	//transmission ends as usual (so RemoveLocalVONBroadcasts and anything hooked on it still run)
	//==========================================================================================================================================================================
	void CutOffLocalVONBroadcasts(int senderId)
	{
		array<int> receivers;
		if (m_mBroadcastReceivers.Find(senderId, receivers) && receivers)
		{
			PlayerManager pm = GetGame().GetPlayerManager();
			foreach (int playerId: receivers)
			{
				if (playerId == senderId)
					continue;

				SCR_PlayerController pc = SCR_PlayerController.Cast(pm.GetPlayerController(playerId));
				if (pc)
					pc.CutOffLocalVONBroadcast(senderId);
			}
		}

		RemoveLocalVONBroadcasts(senderId);
	}

	//Radio listening: a player hears what another player's radios receive (an addon's spectators, say).
	//The server forwards every transmission the target receives - including any already under way - and
	//takes them back when the listening stops. Nobody may until an addon allows it (CanListenToRadios).
	//==========================================================================================================================================================================
	bool CanListenToRadios(int listenerId, int targetId)
	{
		return false;
	}

	//targetId 0 stops listening
	//==========================================================================================================================================================================
	void SetRadioListenTarget(int listenerId, int targetId)
	{
		if (targetId != 0 && (targetId == listenerId || !CanListenToRadios(listenerId, targetId)))
			targetId = 0;

		int current;
		m_mRadioListenTarget.Find(listenerId, current);
		if (current == targetId)
			return;

		StopRadioListening(listenerId);
		if (targetId == 0)
			return;

		m_mRadioListenTarget.Set(listenerId, targetId);
		array<int> listeners;
		if (!m_mRadioListeners.Find(targetId, listeners) || !listeners)
		{
			listeners = {};
			m_mRadioListeners.Set(targetId, listeners);
		}
		listeners.Insert(listenerId);

		//Transmissions the target is hearing right now
		PlayerManager pm = GetGame().GetPlayerManager();
		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			if (!receivers || senderId == listenerId || !receivers.Contains(targetId) || receivers.Contains(listenerId))
				continue;

			CVON_VONContainer container = m_mActiveBroadcasts.Get(senderId);
			IEntity sender = pm.GetPlayerControlledEntity(senderId);
			SCR_PlayerController listenerController = SCR_PlayerController.Cast(pm.GetPlayerController(listenerId));
			if (!container || !sender || !listenerController)
				continue;

			listenerController.AddLocalVONBroadcast(container, senderId, sender.GetOrigin(), m_mActiveBroadcastRange.Get(senderId));
			receivers.Insert(listenerId);
		}
	}

	//Stops a player listening, and takes back what they were hearing only through the target
	//==========================================================================================================================================================================
	protected void StopRadioListening(int listenerId)
	{
		int targetId;
		if (!m_mRadioListenTarget.Find(listenerId, targetId))
			return;

		m_mRadioListenTarget.Remove(listenerId);
		array<int> listeners;
		if (m_mRadioListeners.Find(targetId, listeners) && listeners)
		{
			listeners.RemoveItem(listenerId);
			if (listeners.IsEmpty())
				m_mRadioListeners.Remove(targetId);
		}

		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		PlayerManager pm = GetGame().GetPlayerManager();
		SCR_PlayerController listenerController = SCR_PlayerController.Cast(pm.GetPlayerController(listenerId));
		IEntity listener = pm.GetPlayerControlledEntity(listenerId);
		foreach (int senderId, array<int> receivers: m_mBroadcastReceivers)
		{
			if (!receivers || !receivers.Contains(listenerId))
				continue;

			//Still on a radio of their own
			CVON_VONContainer container = m_mActiveBroadcasts.Get(senderId);
			if (container && gamemode && HasMatchingRadio(listener, container, gamemode.IsEncryptionEnabled()))
				continue;

			receivers.RemoveItem(listenerId);
			if (listenerController)
				listenerController.RemoveLocalVONBroadcast(senderId);
		}
	}

	//A player left: they stop listening, and nobody listens to them any more
	//==========================================================================================================================================================================
	protected void StopRadioListeningFor(int playerId)
	{
		StopRadioListening(playerId);

		array<int> listeners;
		if (!m_mRadioListeners.Find(playerId, listeners) || !listeners)
			return;

		array<int> listenerIds = {};
		listenerIds.Copy(listeners);
		foreach (int listenerId: listenerIds)
		{
			StopRadioListening(listenerId);
		}
	}

	//A transmission the receiver just got: to everyone listening to their radios too
	//==========================================================================================================================================================================
	protected void ForwardToRadioListeners(int receiverId, CVON_VONContainer VONContainer, int senderId, vector senderOrigin, float maxDist, array<int> receivers)
	{
		array<int> listeners;
		if (!m_mRadioListeners.Find(receiverId, listeners) || !listeners)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		foreach (int listenerId: listeners)
		{
			if (listenerId == senderId || receivers.Contains(listenerId))
				continue;

			SCR_PlayerController listenerController = SCR_PlayerController.Cast(pm.GetPlayerController(listenerId));
			if (!listenerController)
				continue;

			listenerController.AddLocalVONBroadcast(VONContainer, senderId, senderOrigin, maxDist);
			receivers.Insert(listenerId);
		}
	}

	//Does one of this player's radios receive the transmission (frequency, time deviation, key) - as AddLocalVONBroadcasts checks
	//==========================================================================================================================================================================
	protected bool HasMatchingRadio(IEntity player, CVON_VONContainer VONContainer, bool encryption)
	{
		array<IEntity> radios = GetRadioEntities(player);
		if (!radios)
			return false;

		foreach (IEntity radio: radios)
		{
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
			if (!radioComp || radioComp.m_sFrequency != VONContainer.m_sFrequency || radioComp.m_iTimeDeviation != VONContainer.m_iTimeDeviation)
				continue;

			if (encryption && VONContainer.m_sFactionKey != radioComp.m_sFactionKey)
				continue;

			return true;
		}

		return false;
	}

	//Just an easy way for me to get all the radios on an entity.
	//includeVehicle: also the vehicle radio they can use (crew seat of a vehicle with one) and the radio whose
	//handset they're holding, last
	//==========================================================================================================================================================================
	array<RplId> GetRadios(IEntity entity, bool includeVehicle = true)
	{
		if (!entity)
			return null;

		ref array<RplId> radios = {};
		SCR_InventoryStorageManagerComponent inventoryComp = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		ref array<IEntity> items = {};
		if (inventoryComp)
			inventoryComp.GetItems(items);
		foreach (IEntity item: items)
		{
			if (!item.FindComponent(CVON_RadioComponent))
				continue;

			radios.Insert(RplComponent.Cast(item.FindComponent(RplComponent)).Id());
		}

		if (includeVehicle)
		{
			CVON_VehicleRadioComponent vehicleRadio = CVON_VehicleComms.GetOperatedRadio(entity);
			if (vehicleRadio)
				radios.Insert(vehicleRadio.GetRplId());

			CVON_RadioComponent handset = GetHeldHandset(entity);
			if (handset)
				radios.Insert(handset.GetRplId());
		}
		return radios;
	}

	array<IEntity> GetRadioEntities(IEntity entity)
	{
		if (!entity)
			return null;

		ref array<IEntity> radios = {};
		SCR_InventoryStorageManagerComponent inventoryComp = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		ref array<IEntity> items = {};
		if (inventoryComp)
			inventoryComp.GetItems(items);
		foreach (IEntity item: items)
		{
			if (!item.FindComponent(CVON_RadioComponent))
				continue;

			radios.Insert(item);
		}

		CVON_VehicleRadioComponent vehicleRadio = CVON_VehicleComms.GetOperatedRadio(entity);
		if (vehicleRadio)
			radios.Insert(vehicleRadio.GetOwner());

		CVON_RadioComponent handset = GetHeldHandset(entity);
		if (handset)
			radios.Insert(handset.GetOwner());
		return radios;
	}

}
