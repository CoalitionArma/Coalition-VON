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
	[RplProp(onRplName: "OnRadioStateReplicated")] int m_iCurrentChannel = 1;
	
	//Yep : )
	[RplProp(onRplName: "OnRadioStateReplicated")] string m_sFrequency = "55500";
	
	//Time Deviation, used to shift the times of the radio making it so others not on the same time cant hear you.
	[RplProp(onRplName: "OnRadioStateReplicated")] int m_iTimeDeviation = 0;
	
	[RplProp()] RplId m_HandMicHolder = RplId.Invalid();
	
	//This is used in conjuction with the gamemode setting m_bIsFactionEncryptionEnabled
	//If enabled it makes it so only factions can hear just their factions radios, this is initialized when a radio is picked up for the first time
	//This means if you pick up a russian radio you can hear a russian broadcast.
	//If m_bIsFactionEncrypto.... disabled then this does nothing.
	//These two strings below are generated on radio initilization onto a player and should almost never change except for when a frequency is shared
	//Consider this the "Key Fill" for the radio.
	[RplProp(onRplName: "OnRadioStateReplicated")] string m_sFactionKey = "";
	
	//Store this for any frequency changes that may result in it folding over into a shared frequency
	string m_sOriginalFactionKey = "";
	//Used to store any channels the player makes and the base channels definied in the faction.
	[RplProp()] ref array<string> m_aChannels = {};
	
	[RplProp(onRplName: "OnRadioStateReplicated")] bool m_bPower = true;
	
	//Authority only: how often an uninitialised radio checks whether it is on a character yet
	static const float INIT_POLL_INTERVAL = 0.5;
	float m_fInitPollTimer = 0;
	
	//How loud the radio is
	int m_iVolume = 9;
	
	//What ear it comes out of, neither needs to be tracked across clients cause who gives af.
	CVON_EStereo m_eStereo = CVON_EStereo.BOTH;
	
	//==========================================================================================================================================================================	
	
	//The character carrying this radio. GetRootParent() isn't enough: for a seated character it returns
	//the vehicle. Vehicle radios override this to return the vehicle itself.
	//==========================================================================================================================================================================
	IEntity GetCarrier()
	{
		IEntity parent = GetOwner().GetParent();
		while (parent)
		{
			if (ChimeraCharacter.Cast(parent))
				return parent;
			parent = parent.GetParent();
		}
		return GetOwner().GetRootParent();
	}

	//Is this one of the local player's radios?
	//==========================================================================================================================================================================
	bool IsHeldByLocalPlayer()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local)
			return false;
		return GetCarrier() == local;
	}
	
	//A replicated radio value changed (channel, frequency, time deviation, encryption key, power). Every
	//client used to compare these against copies every frame for every radio in the world; now only a
	//change to one of our own radios rewrites RadioData.json.
	//==========================================================================================================================================================================
	void OnRadioStateReplicated()
	{
		if (IsHeldByLocalPlayer())
			WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
	}
	
	//Authority changes: replicate, and on a listen server (Workbench, player-hosted) the host's own radios
	//need the refresh too, since replication callbacks don't run on the authority.
	//==========================================================================================================================================================================
	protected void BumpAndNotify()
	{
		Replication.BumpMe();
		if (!System.IsConsoleApp())
			OnRadioStateReplicated();
	}
	
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
		BumpAndNotify();
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
		IEntity carrier = GetCarrier();
		if (!carrier)
		{
			BumpAndNotify();
			return;
		}
		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(carrier.FindComponent(FactionAffiliationComponent));
		if (!factionComp)
		{
			BumpAndNotify();
			return;
		}

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
				sharedFactionKey += "|";
			}
			isShared = true;
			m_sFactionKey = sharedFactionKey;
		}
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(carrier);
		if (playerId > 0 && updateSettings)
			SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).UpdateSettings();
		if (!isShared && m_sOriginalFactionKey == "")
			GetGame().GetCallqueue().CallLater(SetFactionKey, 1000, false);
		else if (!isShared && m_sOriginalFactionKey != "")
			SetFactionKey();
		BumpAndNotify();
	}
	
	void SetFactionKey()
	{
		if (!Replication.IsServer())
			return;

		m_sFactionKey = m_sOriginalFactionKey;
		BumpAndNotify();
	}
	
	//Toggles radio power from the server, hmm... who named this
	void TogglePowerServer()
	{
		m_bPower = !m_bPower;
		BumpAndNotify();
	}
	
	//Called on the authority, updates the TimeDeviation.
	//==========================================================================================================================================================================
	void UpdateTimeDeviationServer(int input)
	{
		m_iTimeDeviation = input;
		BumpAndNotify();
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
		
		//Only the authority has work here: giving the radio its faction and channels once it is on a character
		//(SCR_CharacterInventoryStorageComponent also does this when it's picked up; this is the fallback).
		//Clients get radio changes through OnRadioStateReplicated instead of polling every frame.
		if (!Replication.IsServer() || m_sFactionKey != "")
		{
			ClearEventMask(owner, EntityEvent.FRAME);
			return;
		}
		
		m_fInitPollTimer -= timeSlice;
		if (m_fInitPollTimer > 0)
			return;
		m_fInitPollTimer = INIT_POLL_INTERVAL;
		
		//Redundancy, also used in base CVON without the CRF as they don't have the GS.
		InitializeRadios();
		if (m_sFactionKey != "")
			ClearEventMask(owner, EntityEvent.FRAME);
	}
	
	//Authority: gives a radio on a character that character's faction key and channels.
	//In Workbench the game is a listen server; elsewhere only the dedicated server does this.
	//==========================================================================================================================================================================
	void InitializeRadios()
	{
		#ifdef WORKBENCH
		#else
		if (!System.IsConsoleApp())
			return;
		#endif
		//Faction found no longer needed.
		if (m_sFactionKey != "")
			return;

		//Radio is in the inventory of a player.
		IEntity carrier = GetCarrier();
		if (!SCR_ChimeraCharacter.Cast(carrier))
			return;

		FactionAffiliationComponent factionComp = FactionAffiliationComponent.Cast(carrier.FindComponent(FactionAffiliationComponent));
		if (!factionComp)
			return;

		InitializeForFaction(factionComp.GetAffiliatedFactionKey());
	}

	//The radio's "key fill": a faction's encryption key, the preset group frequencies and then that
	//faction's group overrides, without duplicates. Also used by vehicle radios.
	//==========================================================================================================================================================================
	void InitializeForFaction(string factionKey)
	{
		//No faction yet (e.g. during slotting): try again later rather than filling channels without a key
		if (factionKey == "")
			return;

		m_sFactionKey = factionKey;
		m_sOriginalFactionKey = factionKey;

		ref array<string> SRFrequencies = {};
		ref array<string> LRFrequencies = {};
		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode && gamemode.m_FreqConfig)
		{
			foreach (CVON_GroupFrequencyContainer groupContainer: gamemode.m_FreqConfig.m_aPresetGroupFrequencyContainers)
				AddGroupFrequencies(groupContainer, SRFrequencies, LRFrequencies);

			SCR_Faction faction = SCR_Faction.Cast(GetGame().GetFactionManager().GetFactionByKey(factionKey));
			if (faction && faction.GetCallsignInfo())
			{
				foreach (CVON_GroupFrequencyContainer overrideContainer: faction.GetCallsignInfo().m_aGroupFrequencyOverrides)
					AddGroupFrequencies(overrideContainer, SRFrequencies, LRFrequencies);
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
		BumpAndNotify();
	}

	protected void AddGroupFrequencies(CVON_GroupFrequencyContainer container, array<string> SRFrequencies, array<string> LRFrequencies)
	{
		if (container.m_aSRFrequencies)
		{
			foreach (string freq: container.m_aSRFrequencies)
			{
				if (freq != "" && !IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies))
					SRFrequencies.Insert(freq);
			}
		}
		if (container.m_aLRFrequencies)
		{
			foreach (string freq: container.m_aLRFrequencies)
			{
				if (freq != "" && !IsAlreadyAChannel(freq, LRFrequencies, SRFrequencies))
					LRFrequencies.Insert(freq);
			}
		}
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
		WriteRadioData(entity);
	}

	//Static so it can run with no radio to call it on (leaving a vehicle with its radio and none of your own)
	static void WriteRadioData(IEntity entity)
	{
		if (!GetGame().GetPlayerController())
			return;
		if (entity != SCR_PlayerController.GetLocalControlledEntity())
			return;
		JsonSaveContext VONSave = new JsonSaveContext();
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gamemode)
			return;
		
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		
		ref array<RplId> radios = {};
		if (pc.GetHeadsetLoweredState())
			radios = {};
		else 
			radios = CVON_VONGameModeComponent.GetInstance().GetRadios(entity);
		
		if (!radios)
			return;
		
		string radioFreqs = "";
		string factionKeys = "";
		foreach (RplId radio: radios)
		{
			RplComponent radioRpl = RplComponent.Cast(Replication.FindItem(radio));
			if (!radioRpl || !radioRpl.GetEntity())
				continue;
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radioRpl.GetEntity().FindComponent(CVON_RadioComponent));
			if (!radioComp || !radioComp.m_bPower)
				continue;
			VONSave.StartObject(radio.ToString());
			VONSave.WriteValue("Freq", radioComp.m_sFrequency);
			VONSave.WriteValue("TimeDeviation", radioComp.m_iTimeDeviation);
			VONSave.WriteValue("Volume", radioComp.m_iVolume);
			VONSave.WriteValue("Stereo", radioComp.m_eStereo);
			if (gamemode.IsEncryptionEnabled())
				VONSave.WriteValue("FactionKey", radioComp.m_sFactionKey);
			else
				VONSave.WriteValue("FactionKey", "");
			VONSave.EndObject();
			radioFreqs += radioComp.m_sFrequency + "|";
			factionKeys += radioComp.m_sFactionKey + "|";
		}
		SCR_PlayerController.Cast(GetGame().GetPlayerController()).UpdateFreqArray(radioFreqs, factionKeys);
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
