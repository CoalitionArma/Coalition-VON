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
	
	//The Icon at the center of the VON Radial Menu
	[Attribute("{D9F335476016F8D7}UI/layouts/Radios/ANPRC152/data/ANPRC152Icon.edds", UIWidgets.ResourcePickerThumbnail, "Icon in the center of the radial menu", params: "edds")] ResourceName m_sRadioIcon;
	
	[Attribute("{0470C6B83320621C}UI/layouts/Radios/ANPRC152/data/ANPRC152DialOn.edds", UIWidgets.ResourcePickerThumbnail, "Dial On", params: "edds")] ResourceName m_sDialOn;

	[Attribute("{606059BCB597FCFC}UI/layouts/Radios/ANPRC152/data/ANPRC152DialOff.edds", UIWidgets.ResourcePickerThumbnail, "Dial Off", params: "edds")] ResourceName m_sDialOff;
	
	//The Menu we use whenever we open up this radio.
	[Attribute(ChimeraMenuPreset.ANPRC152.ToString(), UIWidgets.ComboBox, "", "", enumType: ChimeraMenuPreset )] int m_eRadioMenu;
	
	//What channel we are on, used in conjunction with m_aChannels, make sure to -1 for the index;
	[RplProp()] int m_iCurrentChannel = 1;
	
	//Yep : )
	[RplProp()] string m_sFrequency = "55500";
	
	//Time Deviation, used to shift the times of the radio making it so others not on the same time cant hear you.
	[RplProp()] int m_iTimeDeviation = 0;
	
	[RplProp()] RplId m_HandMicHolder = RplId.Invalid();
	
	//This is used in conjuction with the gamemode setting m_bIsFactionEncryptionEnabled
	//If enabled it makes it so only factions can hear just their factions radios, this is initialized when a radio is picked up for the first time
	//This means if you pick up a russian radio you can hear a russian broadcast.
	//If m_bIsFactionEncrypto.... disabled then this does nothing.
	[RplProp()] string m_sFactionKey = "";
	//Used to store any channels the player makes and the base channels definied in the faction.
	[RplProp()] ref array<string> m_aChannels = {};
	
	[RplProp()] bool m_bPower = true;
	
	//All of these are temp freqeuncies we check in case somehow a change slips through the cracks, this mostly happens when we first take over an entity.
	int m_iTempChannel = 1;
	string m_sTempFrequency = "55500";
	int m_iTempTimeDeviation = 0;
	string m_sTempFactionKey = "";
	
	//How loud the radio is
	int m_iVolume = 9;
	
	//What ear it comes out of, neither needs to be tracked across clients cause who gives af.
	CVON_EStereo m_eStereo = CVON_EStereo.BOTH;
	
	//==========================================================================================================================================================================	
	
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
		Replication.BumpMe();
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
		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().GetRootParent().FindComponent(FactionAffiliationComponent));
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
			}
			isShared = true;
			m_sFactionKey = sharedFactionKey;
		}
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(GetOwner().GetRootParent());
		if (playerId > 0 && updateSettings)
			SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).UpdateSettings();
		Replication.BumpMe();
	}
	
	//Toggles radio power from the server, hmm... who named this
	void TogglePowerServer()
	{
		m_bPower = !m_bPower;
		Replication.BumpMe();
	}
	
	//Called on the authority, updates the TimeDeviation.
	//==========================================================================================================================================================================
	void UpdateTimeDeviationServer(int input)
	{
		m_iTimeDeviation = input;
		Replication.BumpMe();
	}
	
	//Called on the authority, updates the TimeDeviation.
	//==========================================================================================================================================================================
	void AddChannelServer()
	{
		m_aChannels.Insert("55500");
		Replication.BumpMe();
	}
	
	void GrabHandMicClient()
	{
		if (m_HandMicHolder != RplId.Invalid())
			return;
		
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).GrabHandMicServer(SCR_PlayerController.GetLocalPlayerId(), GetRplId());
	}
	
	void GrabHandMicServer(int playerId)
	{
		IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!player)
			return;
		
		RplId playerRplId = RplComponent.Cast(player.FindComponent(RplComponent)).Id();
		m_HandMicHolder = playerRplId;
		Replication.BumpMe();
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
		if (System.IsConsoleApp())
		{
			//Redundancy, also used in base CVON without the CRF as they don't have the GS.
			if (m_sFactionKey == "")
				InitializeRadios();
			return;
		}
			
		
		if (m_iTempChannel != m_iCurrentChannel || m_sTempFrequency != m_sFrequency || m_iTempTimeDeviation != m_iTimeDeviation || m_sTempFactionKey != m_sFactionKey)
		{
			m_iTempChannel = m_iCurrentChannel;
			m_sTempFrequency = m_sFrequency;
			m_iTempTimeDeviation = m_iTimeDeviation;
			m_sTempFactionKey = m_sFactionKey;
			WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		}
	}
	
	void InitializeRadios()
	{
		//Have this ifdef here cause in the workshop its a listen server, I explain the code in the non workshop version.
		#ifdef WORKBENCH
		if (m_sFactionKey != "")
			return;
	
		if (!GetOwner().GetRootParent())
			return;

		if (!SCR_ChimeraCharacter.Cast(GetOwner().GetRootParent()))
			return;
		
		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().GetRootParent().FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;
	
		m_sFactionKey = factionComp.GetAffiliatedFactionKey();
		
		SCR_Faction faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sFactionKey));
		ref array<string> SRFrequencies = {};
		ref array<string> LRFrequencies = {};
		if (CVON_VONGameModeComponent.GetInstance().m_FreqConfig)
		{
			foreach (ref CVON_GroupFrequencyContainer groupContainer: CVON_VONGameModeComponent.GetInstance().m_FreqConfig.m_aPresetGroupFrequencyContainers)
			{
				if (groupContainer.m_aSRFrequencies)
					if (groupContainer.m_aSRFrequencies.Count() > 0)
					{
						foreach (string freq: groupContainer.m_aSRFrequencies)
						{
								if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
									SRFrequencies.Insert(freq);
						}
					}
				if (groupContainer.m_aLRFrequencies)
					if (groupContainer.m_aLRFrequencies.Count() > 0)
					{
						foreach (string freq: groupContainer.m_aLRFrequencies)
						{
								if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
									LRFrequencies.Insert(freq);
						}
				}
			}
			foreach (CVON_GroupFrequencyContainer overrideContainer: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
			{
				if (overrideContainer.m_aSRFrequencies)
					if (overrideContainer.m_aSRFrequencies.Count() > 0)
					{
						foreach (string freq: overrideContainer.m_aSRFrequencies)
						{
								if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
									SRFrequencies.Insert(freq);
						}
					}
				if (overrideContainer.m_aLRFrequencies)
					if (overrideContainer.m_aLRFrequencies.Count() > 0)
					{
						foreach (string freq: overrideContainer.m_aLRFrequencies)
						{
								if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
									LRFrequencies.Insert(freq);
						}
					}
			}
		}
		
		m_aChannels.InsertAll(SRFrequencies);
		m_aChannels.InsertAll(LRFrequencies);
		if (m_aChannels.Count() == 0)
		{
			m_aChannels.Insert("55500");
			m_iCurrentChannel = 1;
			m_sFrequency = m_aChannels.Get(0);
		}
		Replication.BumpMe();
		#else
		if (System.IsConsoleApp())
		{
			//Faction found no longer needed.
			if (m_sFactionKey != "")
				return;
		
			if (!GetOwner().GetRootParent())
				return;
			
			//Radio is in the inventory of a player.
			if (!SCR_ChimeraCharacter.Cast(GetOwner().GetRootParent()))
				return;
			
			FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(GetOwner().GetRootParent().FindComponent(FactionAffiliationComponent));
			if (!factionComp)
				return;
			
			m_sFactionKey = factionComp.GetAffiliatedFactionKey();
			//Add that faction to this radio and get the faction to prep to load the factions frequencies
			SCR_Faction faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(m_sFactionKey));
	
			ref array<string> SRFrequencies = {};
			ref array<string> LRFrequencies = {};
			if (CVON_VONGameModeComponent.GetInstance().m_FreqConfig)
			{
				foreach (ref CVON_GroupFrequencyContainer groupContainer: CVON_VONGameModeComponent.GetInstance().m_FreqConfig.m_aPresetGroupFrequencyContainers)
				{
					if (groupContainer.m_aSRFrequencies)
						if (groupContainer.m_aSRFrequencies.Count() > 0)
						{
							foreach (string freq: groupContainer.m_aSRFrequencies)
							{
									if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
										SRFrequencies.Insert(freq);
							}
						}
					if (groupContainer.m_aLRFrequencies)
						if (groupContainer.m_aLRFrequencies.Count() > 0)
						{
							foreach (string freq: groupContainer.m_aLRFrequencies)
							{
									if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
										LRFrequencies.Insert(freq);
							}
					}
				}
				foreach (CVON_GroupFrequencyContainer overrideContainer: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
				{
					if (overrideContainer.m_aSRFrequencies)
						if (overrideContainer.m_aSRFrequencies.Count() > 0)
						{
							foreach (string freq: overrideContainer.m_aSRFrequencies)
							{
									if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
										SRFrequencies.Insert(freq);
							}
						}
					if (overrideContainer.m_aLRFrequencies)
						if (overrideContainer.m_aLRFrequencies.Count() > 0)
						{
							foreach (string freq: overrideContainer.m_aLRFrequencies)
							{
									if (!IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies) && freq != "")
										LRFrequencies.Insert(freq);
							}
						}
				}
			}
			m_aChannels.InsertAll(SRFrequencies);
			m_aChannels.InsertAll(LRFrequencies);
			//If there are no frequencies added, just add a default channel.
			if (m_aChannels.Count() == 0)
			{
				m_aChannels.Insert("55500");
				m_iCurrentChannel = 1;
				m_sFrequency = m_aChannels.Get(0);
			}
			//hehe
			Replication.BumpMe();
		}
		#endif
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
		if (!GetGame().GetPlayerController())
			return;
		SCR_JsonSaveContext VONSave = new SCR_JsonSaveContext();
		ref array<RplId> radios = CVON_VONGameModeComponent.GetInstance().GetRadios(entity);
		if (!radios)
			return;
		foreach (RplId radio: radios)
		{
			IEntity radioEntity = RplComponent.Cast(Replication.FindItem(radio)).GetEntity();
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioEntity.FindComponent(CVON_RadioComponent));
			if (!m_bPower)
				continue;
			VONSave.StartObject(radio.ToString());
			VONSave.WriteValue("Freq", m_sFrequency);
			VONSave.WriteValue("TimeDeviation", m_iTimeDeviation);
			VONSave.WriteValue("Volume", m_iVolume);
			VONSave.WriteValue("Stereo", m_eStereo);
			if (CVON_VONGameModeComponent.GetInstance().m_bUseFactionEcncryption)
				VONSave.WriteValue("FactionKey", m_sFactionKey);
			else
				VONSave.WriteValue("FactionKey", "");
			VONSave.EndObject();
		}
		VONSave.SaveToFile("$profile:/RadioData.json");
	}
	
	void ~CVON_RadioComponent()
	{
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