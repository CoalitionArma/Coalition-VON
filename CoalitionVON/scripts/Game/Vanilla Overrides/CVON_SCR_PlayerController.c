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
	static ref array<int> m_aVolumeValues = {3, 8, 25, 40, 60};
	
	//Teamspeak has been detected, also used to not have the annoying game locking popup everytime TS wants to crash
	bool m_bHasConnectedToTeamspeakForFirstTime = false;
	
	//Used so we can keep Stereo and Volume values
	ref array<ref CVON_RadioSettingObject> m_aRadioSettings = {};
	
	//Local variable stored to remove radios from the radio data json to simulate taking your headset off
	//The asshole who made this will do better in A4
	bool m_bIsHeadsetLowered = false;
	
	int m_iAmountOfTimesRadioRotated = 1;
	
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
	}
	
	void ToggleHeadsetLoweredState()
	{
		m_bIsHeadsetLowered = !m_bIsHeadsetLowered;
		
		//Grab the first radio to use its radio component
		//Really regretting how I made this
		IEntity radio;
		IEntity player = GetControlledEntity();
		if (!player)
			return;
		SCR_InventoryStorageManagerComponent inventoryComp = SCR_InventoryStorageManagerComponent.Cast(player.FindComponent(SCR_InventoryStorageManagerComponent));
		ref array<IEntity> items = {};
		inventoryComp.GetItems(items);
		foreach (IEntity item: items)
		{
			if (!item.FindComponent(CVON_RadioComponent))
				continue;
			
			radio = item;
			break;
		}
		
		if (!radio)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;

		radioComp.WriteJSON(player);
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
		
		//Only this path may wipe m_aLocalEntries
		GetGame().GetCallqueue().CallLater(InitializeRadios, 500, false, to, true);
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
	//clearLocalEntries must ONLY be true when we swapped controlled entity. m_aLocalEntries is everyone we can currently
	//hear, and the server only pushes an entry once at the start of a transmission - if we drop a speaker mid-message
	//they are gone from VONData.json for the rest of it and teamspeak mutes them. ActivateCVON calls this on every PTT
	//keydown, so clearing unconditionally made radios single duplex.
	void InitializeRadios(IEntity to, bool clearLocalEntries = false)
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
		if (clearLocalEntries)
			m_aLocalEntries.Clear();
		array<RplId> radios = CVON_VONGameModeComponent.GetInstance().GetRadios(to);
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
	
	void OpenHandMicMenuClient(int playerId, RplId handMicEntityId)
	{
		Rpc(RpcAsk_OpenHandMicClient, playerId, handMicEntityId);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)] 
	void RpcAsk_OpenHandMicClient(int playerId, RplId handMicEntityId)
	{
		CVON_VONGameModeComponent.GetInstance().OpenHandMicServer(playerId, handMicEntityId);
	}
	
	void OpenHandMicOwner(array<string> freqs, int amountOfRadios, array<string> radioNames)
	{
		Rpc(RpcDo_OpenHandMicOwner, freqs, amountOfRadios, radioNames);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	void RpcDo_OpenHandMicOwner(array<string> freqs, int amountOfRadios, array<string> radioNames)
	{
		SCR_VONController vonController = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
		vonController.GetVONMenu().OpenMenu(freqs, amountOfRadios, radioNames);
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
			vonContainerLocal.m_eVonType = CVON_EVONType.RADIO;
			vonContainerLocal.m_sFrequency = VONContainer.m_sFrequency;
			vonContainerLocal.m_iRadioId = VONContainer.m_iRadioId;
			vonContainerLocal.m_sFactionKey = VONContainer.m_sFactionKey;
			vonContainerLocal.m_iMaxDistance = maxDistance;
			vonContainerLocal.m_vSenderLocation = senderOrigin;
			return;	
		}
		VONContainer.m_iMaxDistance = maxDistance;
		VONContainer.m_vSenderLocation = senderOrigin;
		m_aLocalEntries.Insert(playerId, VONContainer);
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
		
		m_aLocalEntries.Remove(playerIdToRemove);
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
		if (!radioComp)
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
		if (!radioComp)
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
		if (!radioComp)
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
		if (!radioComp)
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
		if (!radioComp)
			return;
		
		radioComp.AddChannelServer();
	}
	
	void GrabHandMicServer(int playerId, RplId radioId)
	{
		Rpc(RpcAsk_GrabHandMicServer, playerId, radioId);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_GrabHandMicServer(int playerId, RplId radioId)
	{
		if (!Replication.FindItem(radioId))
			return;
		
		IEntity radio = RplComponent.Cast(Replication.FindItem(radioId)).GetEntity();
		if (!radio)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
		radioComp.GrabHandMicServer(playerId);
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
	
	void UpdateFreqArray(string radioFreqs, string factionKeys)
	{
		Rpc(RpcDo_UpdateFreqArray, SCR_PlayerController.GetLocalPlayerId(), radioFreqs, factionKeys);
	}
	
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcDo_UpdateFreqArray(int playerId, string radioFreqs, string factionKeys)
	{
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (!gamemode.m_PlayerFreqArray.Contains(playerId))
			gamemode.m_PlayerFreqArray.Insert(playerId, radioFreqs);
		else
			gamemode.m_PlayerFreqArray.Set(playerId, radioFreqs);
		
		if (!gamemode.m_PlayerKeyArray.Contains(playerId))
			gamemode.m_PlayerKeyArray.Insert(playerId, factionKeys);
		else
			gamemode.m_PlayerKeyArray.Set(playerId, factionKeys);
	}
}