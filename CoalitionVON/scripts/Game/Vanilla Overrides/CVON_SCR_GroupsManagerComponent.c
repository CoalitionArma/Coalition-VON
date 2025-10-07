modded class SCR_GroupsManagerComponent
{
	//Called when spawning in. All this handles automatically assigning frequencies when you join a group.
	//==========================================================================================================================================================================
	override void TunePlayersFrequency(int playerId, IEntity player)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.TunePlayersFrequency(playerId, player);
			return;
		}
		GetGame().GetCallqueue().CallLater(TuneFreqDelayWithPresets, 600, false, playerId, player);
	}
	
	//Is there any preset frequencies, used to determine if we use these customs freqs or vanilla.
	bool AnyPlayerFrequencies(int playerId)
	{
		if (!CVON_VONGameModeComponent.GetInstance().m_FreqConfig)
			return false;
		
		if (CVON_VONGameModeComponent.GetInstance().m_FreqConfig.m_aPresetGroupFrequencyContainers.Count() > 0)
			return true;
		
		return false;
	}
	
	//Needed as if you character has already been spawned and you make a new group this is what it calls, because YES!
	//==========================================================================================================================================================================
	override private void TuneAgentsRadio(AIAgent agentEntity)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.TuneAgentsRadio(agentEntity);
			return;
		}
		IEntity player = agentEntity.GetControlledEntity();
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(player);
		if (playerId == -1)
			return;
		GetGame().GetCallqueue().CallLater(TuneFreqDelayWithPresets, 600, false, playerId, player);
	}
	
	//Needed so we wait for the group to initialize
	//==========================================================================================================================================================================
	void TuneFreqDelayWithPresets(int playerId, IEntity player)
	{
		if (!player)
			return;
		
		if (!AnyPlayerFrequencies(playerId))
		{
			TuneFreqWithoutPresets(playerId, player);
			return;
		}
			
		
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!playerController)
			return;
		
		int SRIndex = 0;
		int LRIndex = 0;
		array<IEntity> radiosDone = {};
		if (playerController.m_aRadioSettings.Count() > 0)
		{
			foreach (IEntity radio: playerController.m_aRadios)
			{
				CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
				int index = playerController.m_aRadios.Find(radio);
				if (playerController.m_aRadioSettings.Count() <= index)
					break;
				CVON_RadioSettingObject radioSetting = playerController.m_aRadioSettings.Get(index);
				
				if (radioComp.m_aChannels.Contains(radioSetting.m_sFreq))
				{
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(radioSetting.m_sFreq) + 1);
					radioComp.UpdateFrequncyServer(radioSetting.m_sFreq, -1, false);
					radiosDone.Insert(radio);
					if (radioComp.m_eRadioType == 0)
						SRIndex++;
					else
						LRIndex++;
				}			
			}
		}
		
		SCR_Faction playerFaction = SCR_Faction.Cast(SCR_FactionManager.Cast(GetGame().GetFactionManager()).GetPlayerFaction(playerId));
		
		array<SCR_AIGroup> groups = GetPlayableGroupsByFaction(playerFaction);
		SCR_AIGroup playersGroup = GetPlayerGroup(playerId);
		int index = groups.Find(playersGroup);
		if (index == -1)
			return;
	
		//Gonna get the groups callsign so we know which frequency object is theres
		string company;
		string platoon;
		string squad;
		string character;
		string format;
		playersGroup.GetCallsigns(company, platoon, squad, character, format);
		string playersGroupName = string.Format(format, company, platoon, squad, character);
		CVON_GroupFrequencyContainer freqContainer;
		CVON_VONGameModeComponent gamemodeComp = CVON_VONGameModeComponent.GetInstance();
		SCR_FactionManager factionMan = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (gamemodeComp.m_FreqConfig)
		{
			foreach (CVON_GroupFrequencyContainer freqItem: gamemodeComp.m_FreqConfig.m_aPresetGroupFrequencyContainers)
			{
				foreach (string groupName: freqItem.m_aGroupNames)
				{
					if (groupName != playersGroupName && groupName != playersGroup.GetCustomNameServer())
						continue;
					
					freqContainer = freqItem;
					break;
				}
			}
		}
		foreach (CVON_GroupFrequencyContainer container: playerFaction.GetCallsignInfo().m_aGroupFrequencyOverrides)
		{
			foreach (string groupName: container.m_aGroupNames)
			{
				if (groupName != playersGroupName && groupName != playersGroup.GetCustomNameServer())
					continue;
				
				freqContainer = container;
				break;
			}
		}
		for (int i = 0; i < playerController.m_aRadios.Count(); i++)
		{
			if (radiosDone.Contains(playerController.m_aRadios.Get(i)))
				continue;
			bool m_bFrequencyFound = false;
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(playerController.m_aRadios.Get(i).FindComponent(CVON_RadioComponent));
			if (!freqContainer)
			{
				radioComp.UpdateChannelServer(1);
				if (radioComp.m_aChannels.Count() > 0)
					radioComp.UpdateFrequncyServer(radioComp.m_aChannels.Get(0));
				else
					radioComp.UpdateFrequncyServer("55500");
				continue;
			}
			switch (radioComp.m_eRadioType)
			{
				case CVON_ERadioType.SHORT: 
				{
					if (!freqContainer.m_aSRFrequencies)
						break;
					if (freqContainer.m_aSRFrequencies.Count() < SRIndex + 1)
						break;
					string freq = freqContainer.m_aSRFrequencies.Get(SRIndex);
					if (!radioComp.m_aChannels.Contains(freq))
						break;
					SRIndex++;
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freq) + 1);
					radioComp.UpdateFrequncyServer(freq);
					m_bFrequencyFound = true;
					break;
				}
				case CVON_ERadioType.LONG: 
				{
					if (!freqContainer.m_aLRFrequencies)
						break;
					if (freqContainer.m_aLRFrequencies.Count() < LRIndex + 1)
						break;
					string freq = freqContainer.m_aLRFrequencies.Get(LRIndex);
					if (!radioComp.m_aChannels.Contains(freq))
						break;
					LRIndex++;
					radioComp.UpdateChannelServer(radioComp.m_aChannels.Find(freq) + 1);
					radioComp.UpdateFrequncyServer(freq);
					m_bFrequencyFound = true;
					break;
				}
			}
			if (m_bFrequencyFound)
				continue;
			
			radioComp.UpdateChannelServer(1);
			if (radioComp.m_aChannels.Count() > 0)
				radioComp.UpdateFrequncyServer(radioComp.m_aChannels.Get(0));
			else
				radioComp.UpdateFrequncyServer("55500");
		}
	}
	
	
	//Used if no frequency config is added or is not populated, just the vanilla frequencies.
	//==========================================================================================================================================================================
	void TuneFreqWithoutPresets(int playerId, IEntity player)
	{
		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!playerController)
			return;
		
		if (playerController.m_aRadioSettings.Count() > 0)
		{
			foreach (IEntity radio: playerController.m_aRadios)
			{
				CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
				CVON_RadioSettingObject radioSetting = playerController.m_aRadioSettings.Get(playerController.m_aRadios.Find(radio));
				radioComp.UpdateChannelServer(1);
				radioComp.UpdateFrequncyServer(radioSetting.m_sFreq);
				playerController.SetVolumeFromServer(radioSetting.m_iVolume, playerController.m_aRadios.Find(radio));
				playerController.SetStereoFromServer(radioSetting.m_Stereo, playerController.m_aRadios.Find(radio));
			}
			return;
		}
		
		SCR_Faction playerFaction = SCR_Faction.Cast(SCR_FactionManager.Cast(GetGame().GetFactionManager()).GetPlayerFaction(playerId));
		SCR_AIGroup playersGroup = GetPlayerGroup(playerId);
		for (int i = 0; i < playerController.m_aRadios.Count(); i++)
		{
			bool m_bFrequencyFound = false;
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(playerController.m_aRadios.Get(i).FindComponent(CVON_RadioComponent));
			if (!playersGroup)
			{
				radioComp.UpdateChannelServer(1);
				radioComp.UpdateFrequncyServer("55500");
				continue;
			}
			switch (radioComp.m_eRadioType)
			{
				case CVON_ERadioType.SHORT:
				{
					radioComp.UpdateChannelServer(1);
					radioComp.UpdateFrequncyServer(playersGroup.GetRadioFrequency().ToString());
					break;
				}
				case CVON_ERadioType.LONG:
				{
					radioComp.UpdateChannelServer(1);
					radioComp.UpdateFrequncyServer(playerFaction.GetFactionRadioFrequency().ToString());
					break;
				}
			}
			
		}
	}
}