//Also the VONType the plugin reads. INTERCOM is only ever written by the listener (a talker on their
//vehicle intercom); SPEAKER is a radio transmission heard through a nearby vehicle's speaker.
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
	string m_sTeamspeakPluginVersion = "3.0.1";
	ref map <int, string> m_PlayerFreqArray = new map <int, string>;
	ref map <int, string> m_PlayerKeyArray = new map <int, string>;

	// Server: which players have a radio on each frequency, kept current from UpdateFreqArray. A radio
	// transmission only looks at these players instead of splitting every player's frequency list and
	// scanning their inventory.
	ref map<string, ref array<int>> m_mFrequencyListeners = new map<string, ref array<int>>();
	// Server: who received each sender's current radio transmission, so the remove only goes to them
	// (it used to go to every player on every push-to-talk release).
	ref map<int, ref array<int>> m_mBroadcastReceivers = new map<int, ref array<int>>();

	// Server: radios with a speaker (vehicle radios, manpacks whose speaker was turned on), so a transmission
	// can find the speakers tuned to its frequency
	ref array<CVON_RadioComponent> m_aSpeakerRadios = {};

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
			RemoveLocalVONBroadcasts(playerId);
			RemoveFromFrequencyIndex(playerId);
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
			}
		}

		if (!m_aSpeakerRadios.IsEmpty())
			BroadcastToSpeakers(VONContainer, playerIdToAdd, radioId, senderOrigin, maxDist, receivers, gamemode.IsEncryptionEnabled());
	}

	//Vehicle speakers: players near a vehicle whose radio is on the transmission's frequency hear it from
	//the vehicle, unless they already have it on a radio of their own. Each player gets their nearest
	//speaker (one voice per talker); a listener also near the sender hears whichever of the speaker and the
	//sender's own voice is louder for them (worked out on their client, see SCR_VONController.WriteJSON).
	//==========================================================================================================================================================================
	protected void BroadcastToSpeakers(CVON_VONContainer VONContainer, int senderId, RplId transmittingRadio, vector senderOrigin, float maxDist, array<int> receivers, bool encryption)
	{
		PlayerManager pm = GetGame().GetPlayerManager();

		m_aSpeakerPlayerIds.Clear();
		pm.GetPlayers(m_aSpeakerPlayerIds);
		m_mNearestSpeaker.Clear();
		m_mNearestSpeakerDistSq.Clear();

		foreach (CVON_RadioComponent speaker: m_aSpeakerRadios)
		{
			if (!speaker || !speaker.HasActiveSpeaker())
				continue;

			// A radio doesn't play its own transmission
			if (speaker.GetRplId() == transmittingRadio)
				continue;

			if (speaker.m_sFrequency != VONContainer.m_sFrequency || speaker.m_iTimeDeviation != VONContainer.m_iTimeDeviation)
				continue;

			if (encryption && speaker.m_sFactionKey != VONContainer.m_sFactionKey)
				continue;

			IEntity speakerEntity = speaker.GetSpeakerEntity();
			if (!speakerEntity)
				continue;

			// Out of the transmitting radio's range
			vector speakerOrigin = speakerEntity.GetOrigin();
			if (maxDist > 0 && vector.DistanceSq(speakerOrigin, senderOrigin) > maxDist * maxDist)
				continue;

			float reach = speaker.m_iSpeakerRange + SPEAKER_SEND_MARGIN;
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

			receiverController.AddLocalSpeakerBroadcast(VONContainer, senderId, senderOrigin, maxDist, speaker.GetRplId(), speaker.m_iSpeakerRange);
			receivers.Insert(playerId);
		}
	}

	void RegisterSpeakerRadio(CVON_RadioComponent radio)
	{
		if (!m_aSpeakerRadios.Contains(radio))
			m_aSpeakerRadios.Insert(radio);
	}

	void UnregisterSpeakerRadio(CVON_RadioComponent radio)
	{
		m_aSpeakerRadios.RemoveItem(radio);
	}

	void SetHandsetHeld(CVON_RadioComponent radio, bool held)
	{
		if (!held)
		{
			m_aHeldHandsets.RemoveItem(radio);
			return;
		}
		if (!m_aHeldHandsets.Contains(radio))
			m_aHeldHandsets.Insert(radio);
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
