//What type of transmission we are trying to send so we can send the right data to the other clients.
enum CVON_EVONTransmitType
{
	NONE,
	DIRECT,
	SR,
	LR,
	LR2
}

modded class SCR_VONController
{
	static const int CVON_DB_ATTEN_VEHICLE  = -18; // speaker inside vehicle
	static const int CVON_DB_ATTEN_BUILDING = -12; // speaker inside building
	
	//MMMM POINTER
	SCR_PlayerController m_PlayerController;
	
	SCR_BaseGameMode m_BaseGamemode;
	
	IEntity m_Player;
	
	RplComponent m_PlayerRplComponent;
	
	CameraManager m_CameraManager;
	
	MenuManager m_MenuManager;
	
	CameraBase m_Camera;
	
	//MMM... stores the gamemode
	CVON_VONGameModeComponent m_VONGameModeComponent;
	PlayerManager m_PlayerManager;
	
	//Who we are currently broadcasting to, this is how we know who we have to send remove calls to
	ref array<int> m_aPlayerIdsBroadcastedTo = {};
	
	//What is our current broadcast
	ref CVON_VONContainer m_CurrentVONContainer = null;
	
	//Am I broadcasting
	bool m_bIsBroadcasting = false;
	
	//Have I already broadcasted my current von container
	bool m_bHasBroadcasted = false;
	
	//Both are used so we can toggle our direct voice and use a radio at the same time
	bool m_bToggleBuffer = false;
	bool m_bToggleTurnedOffByRadio = false;
	
	//Stores the HUD so we can deactivate it after a transmissions end
	CVON_HUD m_VONHud;
	
	//Used to check life state for VON
	SCR_CharacterControllerComponent m_CharacterController;
	
	//Used if we are warning the player their VON is not connected after initial connection
	bool m_bShowingSecondWarning = false;
	
	SCR_FactionManager m_FactionManager;
	
	
	//All these below are just how we assign keybinds to activate certain VON Transmissions
	//==========================================================================================================================================================================
	void ActivateCVONSR()
	{	
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCVON(CVON_EVONTransmitType.SR);
	}
	
	//==========================================================================================================================================================================
	void ActivateCVONLR()
	{
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCVON(CVON_EVONTransmitType.LR);
	}
	
	//==========================================================================================================================================================================
	void ActivateCVONLR2()
	{
		if (m_bToggleBuffer)
		{
			m_bToggleBuffer = false;
			DeactivateCVON();
			m_VONHud.ShowDirectToggle();
			m_bToggleTurnedOffByRadio = true;
		}
		ActivateCVON(CVON_EVONTransmitType.LR2);
	}
	
	//How we rotate what radio is assigned to caps-lock, aka active.
	//==========================================================================================================================================================================
	void RotateActiveRadio()
	{
		int count = m_PlayerController.m_aRadios.Count();
		if (count < 2) return;
	
		IEntity last = m_PlayerController.m_aRadios[count - 1];
	
	    for (int i = count - 1; i > 0; i--)
	    {
	        m_PlayerController.m_aRadios[i] = m_PlayerController.m_aRadios[i - 1];
	    }
	    m_PlayerController.m_aRadios[0] = last;
	
	    m_PlayerController.RotateActiveChannelServer();
	}
	
	//! Initialize component, done once per controller
	//==========================================================================================================================================================================
	override protected void Init(IEntity owner)
	{	
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.Init(owner);
			return;
		}
		if (s_bIsInit || System.IsConsoleApp())	// hosted server will have multiple controllers, init just the first one // dont init on dedicated server
		{
			Deactivate(owner);
			return;
		}
		
		UpdateUnconsciousVONPermitted();

		m_InputManager = GetGame().GetInputManager();
		if (m_InputManager)
		{
			m_InputManager.AddActionListener(ACTION_CHANNEL, EActionTrigger.DOWN, ActivateCVONSR);
			m_InputManager.AddActionListener(ACTION_CHANNEL, EActionTrigger.UP, DeactivateCVON);
			m_InputManager.AddActionListener(ACTION_TRANSCEIVER_CYCLE, EActionTrigger.DOWN, ActionVONTransceiverCycle);
			m_InputManager.AddActionListener("VONLongRange", EActionTrigger.DOWN, ActivateCVONLR);
			m_InputManager.AddActionListener("VONLongRange", EActionTrigger.UP, DeactivateCVON);
			m_InputManager.AddActionListener("VONMediumRange", EActionTrigger.DOWN, ActivateCVONLR2);
			m_InputManager.AddActionListener("VONMediumRange", EActionTrigger.UP, DeactivateCVON);
			m_InputManager.AddActionListener("VONRotateActive", EActionTrigger.DOWN, RotateActiveRadio);
			m_InputManager.AddActionListener("VONChannelUp", EActionTrigger.DOWN, ChannelUp);
			m_InputManager.AddActionListener("VONChannelDown", EActionTrigger.DOWN, ChannelDown);
			m_InputManager.AddActionListener("VONOpenActive", EActionTrigger.DOWN, OpenActiveRadio);
			m_InputManager.AddActionListener("VONRadioEarRight", EActionTrigger.DOWN, SetActiveEarRight);
			m_InputManager.AddActionListener("VONRadioEarLeft", EActionTrigger.DOWN, SetActiveEarLeft);
			m_InputManager.AddActionListener("VONRadioEarBoth", EActionTrigger.DOWN, SetActiveEarBoth);
		}

		SCR_PlayerController playerController = SCR_PlayerController.Cast(GetOwner());
		if (playerController)
		{
			OnControlledEntityChanged(null, playerController.GetControlledEntity());
			playerController.m_OnControlledEntityChanged.Insert(OnControlledEntityChanged);
		}

		PauseMenuUI.m_OnPauseMenuOpened.Insert(OnPauseMenuOpened);
		PauseMenuUI.m_OnPauseMenuClosed.Insert(OnPauseMenuClosed);
		
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
			gameMode.GetOnPlayerDeleted().Insert(OnPlayerDeleted);
		
		m_DirectSpeechEntry = new SCR_VONEntry(); // Init direct speech entry
		
		ConnectToHandleUpdateVONControllersSystem();
		
		s_bIsInit = true;
		
		if (m_VONMenu)
			m_VONMenu.Init(this);
		
		m_FactionManager = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		
		UpdateSystemState();
		
		GetGame().GetCallqueue().CallLater(GetHud, 500, false);
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarRight()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		radioComp.m_eStereo = CVON_EStereo.RIGHT;
		radioComp.WriteJSON(m_Player);
		m_VONHud.ShowVONChange(radioComp.m_iCurrentChannel - 1);
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarLeft()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		radioComp.m_eStereo = CVON_EStereo.LEFT;
		radioComp.WriteJSON(m_Player);
		m_VONHud.ShowVONChange(radioComp.m_iCurrentChannel - 1);
	}
	
	//Change ear keybind
	//==========================================================================================================================================================================
	void SetActiveEarBoth()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		radioComp.m_eStereo = CVON_EStereo.BOTH;
		radioComp.WriteJSON(m_Player);
		m_VONHud.ShowVONChange(radioComp.m_iCurrentChannel - 1);
	}
	
	//Keybind to open active radio
	//==========================================================================================================================================================================
	void OpenActiveRadio()
	{
		if (m_PlayerController.m_aRadios.Count() == 0)
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(m_PlayerController.m_aRadios.Get(0).FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		radioComp.OpenMenu();
	}
	
	//Used as input
	//==========================================================================================================================================================================
	void ChannelUp()
	{
		ChangeChannel(1);
	}
	
	//used as input
	//==========================================================================================================================================================================
	void ChannelDown()
	{
		ChangeChannel(-1);
	}
	
	//Used for changing channel keybind to rotate through channels without opening radio
	//==========================================================================================================================================================================
	void ChangeChannel(int input)
	{
		array<IEntity> radios = m_PlayerController.m_aRadios;
		if (radios.Count() == 0)
			return;
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radios.Get(0).FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		int channelCount = radioComp.m_aChannels.Count();
		if (channelCount < 2)
			return;

		if (radioComp.m_iCurrentChannel == 1 && input == -1)
		{
			return;
		}
		else if (radioComp.m_iCurrentChannel == 99 && input == 1)
		{
			radioComp.UpdateChannelClient(1);
			string freq = radioComp.m_aChannels.Get(0);
			radioComp.UpdateFrequencyClient(freq);
			m_VONHud.ShowVONChange(0);
			return;
		}
		else if (radioComp.m_iCurrentChannel + input > channelCount)
		{
			radioComp.UpdateChannelClient(1);
			string freq = radioComp.m_aChannels.Get(0);
			radioComp.UpdateFrequencyClient(freq);
			m_VONHud.ShowVONChange(0);
			return;
		}	
		else
		{
			radioComp.UpdateChannelClient(radioComp.m_iCurrentChannel + input);
		}
		#ifdef WORKBENCH
		string freq = radioComp.m_aChannels.Get(radioComp.m_iCurrentChannel - 1);
		#else
		string freq = radioComp.m_aChannels.Get(radioComp.m_iCurrentChannel - 1 + input);
		#endif
		radioComp.UpdateFrequencyClient(freq);
		#ifdef WORKBENCH
		m_VONHud.ShowVONChange(radioComp.m_iCurrentChannel - 1);
		#else
		m_VONHud.ShowVONChange(radioComp.m_iCurrentChannel - 1 + input);
		#endif
	}
	
	//Fetches the VON HUD, delay is necessary.
	//==========================================================================================================================================================================
	void GetHud()
	{
		ref array<BaseInfoDisplay> displays = {};
		GetGame().GetHUDManager().GetInfoDisplays(displays);
		foreach (BaseInfoDisplay display: displays)
		{
			if (!CVON_HUD.Cast(display))
				continue;
			
			m_VONHud = CVON_HUD.Cast(display);
		}
	}
	
	//No more base game VON
	//==========================================================================================================================================================================
	override protected bool ActivateVON(notnull SCR_VONEntry entry, EVONTransmitType transmitType = EVONTransmitType.NONE)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			return super.ActivateVON(entry, transmitType);
		}
		return false;
	}
	
	//This builds our VON Container with all the data we need to send to other clients based on the data sent out. This starts the talking process.
	//==========================================================================================================================================================================
	void ActivateCVON(CVON_EVONTransmitType transmitType = CVON_EVONTransmitType.NONE)
	{
		#ifdef WORKBENCH
		#else
		if (m_PlayerController.GetTeamspeakClientId() == 0 && m_VONGameModeComponent.m_bTeamspeakChecks)
			return;
		#endif
		if (!m_Player)
			return;
		if (m_CharacterController.GetLifeState() != ECharacterLifeState.ALIVE)
			return;
		if (m_CurrentVONContainer)
			DeactivateCVON();
		CVON_VONContainer container = new CVON_VONContainer();
		if (transmitType == CVON_EVONTransmitType.NONE)
			return;
		if (transmitType == CVON_EVONTransmitType.DIRECT)
			container.m_eVonType = CVON_EVONType.DIRECT;
		else if (transmitType == CVON_EVONTransmitType.SR || transmitType == CVON_EVONTransmitType.LR || transmitType == CVON_EVONTransmitType.LR2)
			container.m_eVonType = CVON_EVONType.RADIO;
		
		container.m_iVolume = m_PlayerController.ReturnLocalVoiceVolume();
		container.m_SenderRplId = RplComponent.Cast(m_Player.FindComponent(RplComponent)).Id();
		container.m_iClientId = m_PlayerController.GetTeamspeakClientId();
		container.m_iPlayerId = m_PlayerController.GetLocalPlayerId();
		if (container.m_eVonType == CVON_EVONType.RADIO)
		{
			switch (transmitType)
			{
				case CVON_EVONTransmitType.SR:
				{
					if (m_PlayerController.m_aRadios.Count() < 1)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(0);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					if (!radioComp)
						return;
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					container.m_iTimeDeviation = radioComp.m_iTimeDeviation;
					switch (radioComp.m_eStereo)
					{
						case CVON_EStereo.BOTH:  {AudioSystem.PlaySound("{E3B4231783ABA914}UI/sounds/beepstart.wav"); break;}
						case CVON_EStereo.LEFT:  {AudioSystem.PlaySound("{3B2D6B4BBEA1CE72}UI/sounds/beepstartleft.wav"); break;}
						case CVON_EStereo.RIGHT: {AudioSystem.PlaySound("{18F289DB8B5F38D1}UI/sounds/beepstartright.wav"); break;}
					}
					break;
				}
				case CVON_EVONTransmitType.LR:
				{
					if (m_PlayerController.m_aRadios.Count() < 2)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(1);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					if (!radioComp)
						return;
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					switch (radioComp.m_eStereo)
					{
						case CVON_EStereo.BOTH:  {AudioSystem.PlaySound("{E3B4231783ABA914}UI/sounds/beepstart.wav"); break;}
						case CVON_EStereo.LEFT:  {AudioSystem.PlaySound("{3B2D6B4BBEA1CE72}UI/sounds/beepstartleft.wav"); break;}
						case CVON_EStereo.RIGHT: {AudioSystem.PlaySound("{18F289DB8B5F38D1}UI/sounds/beepstartright.wav"); break;}
					}
					break;
				}
				case CVON_EVONTransmitType.LR2:
				{
					if (m_PlayerController.m_aRadios.Count() < 3)
						return;
					IEntity radio = m_PlayerController.m_aRadios.Get(2);
					if (radio == null)
						return;
					
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					if (!radioComp)
						return;
					container.m_sFrequency = radioComp.m_sFrequency;
					container.m_iRadioId = RplComponent.Cast(radio.FindComponent(RplComponent)).Id();
					container.m_sFactionKey = radioComp.m_sFactionKey;
					switch (radioComp.m_eStereo)
					{
						case CVON_EStereo.BOTH:  {AudioSystem.PlaySound("{E3B4231783ABA914}UI/sounds/beepstart.wav"); break;}
						case CVON_EStereo.LEFT:  {AudioSystem.PlaySound("{3B2D6B4BBEA1CE72}UI/sounds/beepstartleft.wav"); break;}
						case CVON_EStereo.RIGHT: {AudioSystem.PlaySound("{18F289DB8B5F38D1}UI/sounds/beepstartright.wav"); break;}
					}
					break;
				}
			}
		}
		m_CurrentVONContainer = container;
		m_bIsBroadcasting = true;
		m_bHasBroadcasted = false;
	}
	
	//No more base game VON
	//==========================================================================================================================================================================
	override protected void DeactivateVON(EVONTransmitType transmitType = EVONTransmitType.NONE)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.DeactivateVON(transmitType);
			return;
		}
		return;
	}
	
	//Stops talking and removes our VON entry from all others players that we where broadcasting to.
	//==========================================================================================================================================================================
	void DeactivateCVON()
	{
		if (m_bToggleBuffer)
			return;
		if (!m_VONGameModeComponent)
			return;
		if (!m_CurrentVONContainer)
			return;
		if (m_CurrentVONContainer.m_eVonType == CVON_EVONType.RADIO)
		{
			RplComponent rplComp = RplComponent.Cast(Replication.FindItem(m_CurrentVONContainer.m_iRadioId));
			if (rplComp)
			{
				IEntity radio = rplComp.GetEntity();
				if (radio)
				{
					CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent));
					if (radioComp)
					{
						switch (radioComp.m_eStereo)
						{
							case CVON_EStereo.BOTH:  {AudioSystem.PlaySound("{B826EAACD5F6B6BB}UI/sounds/beepend.wav"); break;}
							case CVON_EStereo.LEFT:  {AudioSystem.PlaySound("{ABDEAEC2D5718124}UI/sounds/beependleft.wav"); break;}
							case CVON_EStereo.RIGHT: {AudioSystem.PlaySound("{7BF09D8FB6C39FF3}UI/sounds/beependright.wav"); break;}
						}
					}
				}
			}
			
			m_VONHud.HideVON();
		}
		else
			m_VONHud.HideDirect();
			
		m_bIsBroadcasting = false;
		m_bHasBroadcasted = true;
		m_CurrentVONContainer = null;
		

		m_PlayerController.BroadcastRemoveLocalVONToServer(m_PlayerController.GetPlayerId());
	}
	
	//==========================================================================================================================================================================
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		if (System.IsConsoleApp())
			return;
		SetEventMask(owner, EntityEvent.FIXEDFRAME | EntityEvent.INIT);
	}
	
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		if (!m_VONGameModeComponent)
			m_VONGameModeComponent = CVON_VONGameModeComponent.GetInstance();
		
		if (!m_PlayerManager)
			m_PlayerManager = GetGame().GetPlayerManager();
		
		if (!m_CameraManager)
			m_CameraManager = GetGame().GetCameraManager();
		
		if (!m_MenuManager)
			m_MenuManager = GetGame().GetMenuManager();
	}
	//The meat, this is where we determine who we send a VONEntry too and if we've already sent one.
	//Differentiates behavior for Direct and Radio in here as well. If a player is more than 200m away and you try to use direct he will not receive that direct VONEntry.
	//==========================================================================================================================================================================
	float m_fWriteTeamspeakClientIdCooldown = 0;
	ref array<int> m_PlayerIdTemp = {};
	float m_fHeadCacheBuffer = 0;
	// VONServerData.json only needs to be checked at ~1 s; reading it every 50 ms
	// was 20 unnecessary file reads per second just to detect a TSClientID update.
	float m_fServerDataBuffer = 0;
	// Dirty-flag tracking: last value of IsTransmitting written to VONData.json.
	bool m_bLastWrittenTransmitting = false;
	// Track how many entries were in m_aLocalEntries the last time we wrote to disk.
	// If the count changes (e.g. the last speaker left range), we must force a write.
	int m_iLastWrittenEntryCount = 0;
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		super.EOnFixedFrame(owner, timeSlice);
		if (m_fWriteTeamspeakClientIdCooldown > 0)
			m_fWriteTeamspeakClientIdCooldown -= timeSlice;
		else
			m_fWriteTeamspeakClientIdCooldown = 0;
		//Just in case....
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		if (!m_PlayerController)
			m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		
		if (!m_BaseGamemode)
			m_BaseGamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		
		m_Player = m_PlayerController.GetControlledEntity();
		
		if (!m_CharacterController)
			if (m_Player)
				m_CharacterController = SCR_CharacterControllerComponent.Cast(m_Player.FindComponent(SCR_CharacterControllerComponent));
		
		if (!m_PlayerRplComponent)
			if (m_Player)
				m_PlayerRplComponent = RplComponent.Cast(m_Player.FindComponent(RplComponent));
		
		if (!m_PlayerRplComponent || !m_CharacterController || !m_Player)
			return;
		
		m_Camera = m_CameraManager.CurrentCamera();
		if (!m_Camera)
			return;
		
		m_PlayerIdTemp.Clear();
		m_PlayerManager.GetPlayers(m_PlayerIdTemp);
		
		if (m_fHeadCacheBuffer >= 0.2)
		{
			UpdateHeadCache();
			m_fHeadCacheBuffer = 0;
		}
		else
			m_fHeadCacheBuffer += timeSlice;
		
		//When a player disconnects, they are no longer in the players array, so it just leaves an empty container.
		//This removes that container as when they reconnect they will no longer be heard.
		//Also sound updating for maximum optimizations
		foreach (int playerId, CVON_VONContainer container: m_PlayerController.m_aLocalEntries)
		{
			if (!m_PlayerIdTemp.Contains(playerId))
			{
				m_PlayerController.m_aLocalEntries.Remove(playerId);
				continue;
			}
		
			if (container.m_SoundSource)
			{
			int volume = m_VONGameModeComponent.GetPlayerVolume(playerId);
			int maxDistance = volume;
			maxDistance *= maxDistance;
			container.m_iVolume = volume;
			
			float distance = vector.DistanceSq(container.m_SoundSource.GetOrigin(), m_Camera.GetOrigin());
			if (distance < maxDistance)
				container.m_fDistanceToSender = distance;
			else
				container.m_fDistanceToSender = -1;
		}
		} // end foreach m_aLocalEntries
		
		foreach (int playerId: m_PlayerIdTemp)
		{
			if (!m_Player)
				continue;
			
			if (playerId == m_PlayerController.GetPlayerId())
				continue;
			
			IEntity player = m_PlayerManager.GetPlayerControlledEntity(playerId);
			if (!player)
			{
				if (m_PlayerController.m_aLocalEntries.Contains(playerId))
				{
					//If this VON Transmission is radio, don't do shit
					
					if (m_PlayerController.m_aLocalEntries.Get(playerId).m_eVonType == CVON_EVONType.RADIO)
						continue;
					m_PlayerController.m_aLocalEntries.Remove(playerId);
					continue;
				}
				else
					continue;
			}
			else
			{
				ChimeraCharacter chimeraChar = ChimeraCharacter.Cast(player);
				if (!chimeraChar)
					continue;
				SCR_CharacterControllerComponent charCont = SCR_CharacterControllerComponent.Cast(chimeraChar.GetCharacterController());
				if (!charCont || charCont.IsDead() || charCont.IsUnconscious())
					if (m_PlayerController.m_aLocalEntries.Contains(playerId))
					{
						m_PlayerController.m_aLocalEntries.Remove(playerId);
						continue;
					}
					else
						continue;
				
				int maxDistance = m_VONGameModeComponent.GetPlayerVolume(playerId);
				maxDistance *= maxDistance;
				float distance = vector.DistanceSq(player.GetOrigin(), m_Camera.GetOrigin());
				if (distance > maxDistance)
				{
					if (m_PlayerController.m_aLocalEntries.Contains(playerId))
					{
						//If this VON Transmission is radio, don't do shit
						if (m_PlayerController.m_aLocalEntries.Get(playerId).m_eVonType == CVON_EVONType.RADIO)
							continue;
						m_PlayerController.m_aLocalEntries.Remove(playerId);
						continue;
					}
					else
						continue;
				}
				else
				{
					if (m_PlayerController.m_aLocalEntries.Contains(playerId))
						continue;
					else
					{
						CVON_VONContainer container = new CVON_VONContainer();
						container.m_eVonType = CVON_EVONType.DIRECT;
						container.m_iVolume = m_VONGameModeComponent.GetPlayerVolume(playerId);
						container.m_SenderRplId = RplComponent.Cast(player.FindComponent(RplComponent)).Id();
						container.m_iClientId = m_PlayerController.GetPlayersTeamspeakClientId(playerId);
						container.m_iPlayerId = playerId;
						m_PlayerController.m_aLocalEntries.Insert(playerId, container);
					}
					
				}
			}
		}
		
				
		//Handles broadcasting to other players
		if (m_bIsBroadcasting)
		{
			if (m_CharacterController.GetLifeState() != ECharacterLifeState.ALIVE)
			{
				if (m_bToggleBuffer)
				{
					m_bToggleBuffer = false;
					DeactivateCVON();
					m_VONHud.DirectToggleDelay();
				}
				else
					DeactivateCVON();
				return;
			}	
		}
		
		if (!m_bHasBroadcasted && m_bIsBroadcasting)
		{
			m_PlayerController.BroadcastLocalVONToServer(m_CurrentVONContainer, m_PlayerController.GetPlayerId(), m_CurrentVONContainer.m_iRadioId);
			m_bHasBroadcasted = true;
		}
		
		// WriteJSON runs every tick; the dirty flag inside skips SaveToFile when nothing changed.
		m_fServerDataBuffer += timeSlice;
		bool checkServerData = (m_fServerDataBuffer >= 1.0);
		if (checkServerData)
			m_fServerDataBuffer = 0;
		WriteJSON(checkServerData);
	}
	
	//Thank god for CHATGPT
	//Computes how much of the direct voice you hear in your left ear vs your right ear.
	// Stereo spatializer (no distance falloff)
	// Geometry only: pan + rear shadow + elevation + bleed.
	// Multiply the returned L/R by your own plugin volume afterward.
	//==========================================================================================================================================================================
	static const float MAX_OUT_GAIN = 1.3;    // safety cap; raise or set -1 for no cap
	
	// 0 dB at d=0, −45 dB at d=inaudible_m (volume_m).
	static float AttenuationDb(float d_m, float inaudible_m, float shapeExp = 1.6)
	{
	    if (inaudible_m <= 0.01) inaudible_m = 0.01;
	    if (d_m <= 0.0)          return 0.0;
	
	    float x = d_m / inaudible_m;        // 0..1
	    if (x >= 1.0) return -45.0;
	    float db = -45.0 * Math.Pow(x, shapeExp);
	
	    if (db >  0.0)  db = 0.0;
	    if (db < -45.0) db = -45.0;
	    return db;
	}
	
	static float GainFromDb(float db)
	{
	    return Math.Pow(10.0, db / 20.0);
	}
		
	// --- Stereo with front/back cues + −45 dB distance law + LoudnessIntensity ---
	void ComputeStereoLR(
	    IEntity listener,
	    vector  sourcePos,
	    float   volume_m,    
		int 	playerId,    
		out float outBehindIntensity,    
	    out float outLeft,
	    out float outRight,
	    out int  silencedDecibels = 0,
	    float   rearPanBoost   = 0.55,
	    float   rearShadow     = 0.12,
	    float   elevNarrow     = 0.25,
	    float   bleed          = 0.0,
	    bool    normalizePeak  = true
	)
	{
	    // ---- Listener pose ----
	    vector Lpos  = listener.GetOrigin();
	    vector Right = listener.GetTransformAxis(0); // +X
	    vector Up    = listener.GetTransformAxis(1); // +Y
	    vector Fwd   = listener.GetTransformAxis(2); // +Z
	
	    // ---- Direction ----
	    vector toSrc = sourcePos - Lpos;
	    float  dist  = toSrc.Length();
		if (dist > volume_m)
		{
			outLeft = 0;
			outRight = 0;
			silencedDecibels = 0;
			return;
		}
	    if (dist < 0.0001) dist = 0.0001;
	    vector dir   = toSrc / dist;
	
	    // ---- Azimuth (project out elevation) ----
	    vector horiz = dir - Up * vector.Dot(dir, Up);
	    float  hlen  = horiz.Length();
	    if (hlen < 0.0001) { horiz = Fwd; hlen = 1.0; }
	    horiz /= hlen;
	
	    // ---- Pan & front/back cues ----
	    float pan    = Math.Clamp(vector.Dot(horiz, Right), -1.0, 1.0);
	    float front  = Math.Clamp(vector.Dot(horiz, Fwd),   -1.0, 1.0);
	    float back01 = Math.Pow(Math.Max(0.0, -front), 1.4);
	
	    // Boost panning behind to help "behindness"
	    float panScale = 1.0 + rearPanBoost * back01;
	    pan = Math.Clamp(pan * panScale, -1.0, 1.0);
	
	    // Narrow width when high/low
	    float elevAbs  = Math.AbsFloat(vector.Dot(dir, Up)); // 0..1
	    float width    = 1.0 - elevNarrow * elevAbs;
	    pan = Math.Clamp(pan * width, -1.0, 1.0);
	
	    // ---- Equal-power pan ----
	    float L = Math.Sqrt(0.5 * (1.0 - pan));
	    float R = Math.Sqrt(0.5 * (1.0 + pan));
	
	    // ---- Cross-feed bleed ----
	    float Lb = L, Rb = R;
	    L = (1.0 - bleed) * Lb + bleed * Rb;
	    R = (1.0 - bleed) * Rb + bleed * Lb;
	
	    // ---- Peak-normalize AFTER bleed (so center ≈ 1/1 per ear) ----
	    if (normalizePeak) {
	        float peak = Math.Max(L, R);
	        if (peak > 0.0001) {
	            float s = 1.0 / peak;
	            L *= s;
	            R *= s;
	        }
	    }
	
	    // ---- Rear shadow (linear softening behind) ----
	    float rearAtt = 1.0 - rearShadow * back01;
	
	    // ---- Baseline distance law (−45 dB at volume_m)
	    float distDb   = AttenuationDb(dist, volume_m, 1.6);
	
	    // If you have occlusion, subtract it here in dB BEFORE converting to linear:
	    // distDb -= silencedDecibels; // set silencedDecibels elsewhere
	
	    float distGain = GainFromDb(distDb);
	
	    // ---- Final per-ear gains
	    float gain = rearAtt * distGain;
	    outLeft  = L * gain;
	    outRight = R * gain;
	
	    if (MAX_OUT_GAIN > 0.0) {
	        outLeft  = Math.Clamp(outLeft,  0.0, MAX_OUT_GAIN);
	        outRight = Math.Clamp(outRight, 0.0, MAX_OUT_GAIN);
	    }
		
		outBehindIntensity = CRF_AudioSpatialUtil.ComputeBehindIntensity(Lpos, sourcePos);
	}
	
	// Convert attenuation in dB → linear (treats +/−dB the same)
	static float AttenDbToLin(float dB)
	{
	    float a = Math.AbsFloat(dB);
	    return Math.Pow(10.0, -a / 20.0);
	}
	
	//Also bless ChatGPT, handles the arcade signal calulations.
	// distance: current distance from transmitter
	// maxDist: effective range
	// Returns: signal strength (0.0–1.0)
	//==========================================================================================================================================================================
	float GetSignalStrength(float d, float maxRangeMeters)
	{
	    if (d <= 0)                return 1.0;
	    if (d >= maxRangeMeters)   return 0.0;
	
	    float x = d / maxRangeMeters; // 0..1
	    float res;
	
	    if (x <= 0.2) {
	        float t = x / 0.2;                // 0..1 over [0.0, 0.2]
	        res = 1.0 + t * (0.9 - 1.0);      // 1.0 -> 0.9
	    }
	    else if (x <= 0.4) {
	        float t = (x - 0.2) / 0.2;        // 0..1 over (0.2, 0.4]
	        res = 0.9 + t * (0.8 - 0.9);      // 0.9 -> 0.8
	    }
	    else if (x <= 0.6) {
	        float t = (x - 0.4) / 0.2;        // 0..1 over (0.4, 0.6]
	        res = 0.8 + t * (0.5 - 0.8);      // 0.8 -> 0.5
	    }
	    else if (x <= 0.8) {
	        float t = (x - 0.6) / 0.2;        // 0..1 over (0.6, 0.8]
	        res = 0.5 + t * (0.3 - 0.5);      // 0.5 -> 0.3
	    }
	    else {
	        float t = (x - 0.8) / 0.2;        // 0..1 over (0.8, 1.0]
	        res = 0.3 + t * (0.0 - 0.3);      // 0.3 -> 0.0
	    }
	
	    // clamp to [0,1]
	    if (res < 0.0) res = 0.0;
	    if (res > 1.0) res = 1.0;
	    return res;
	}
	
		
	ref map<IEntity, vector> m_HeadCache = new map<IEntity, vector>;
	void UpdateHeadCache()
	{
		m_HeadCache.Clear();
		foreach (int playerId, CVON_VONContainer container: m_PlayerController.m_aLocalEntries)
		{
			if (!container.m_SoundSource)
				continue;
			
			if (container.m_fDistanceToSender == -1)
				continue;
			
			m_HeadCache.Insert(container.m_SoundSource, ComputeHeadHeight(container.m_SoundSource));
		}
		
		if (m_Player)
			m_HeadCache.Insert(m_Player, ComputeHeadHeight(m_Player));
	}
	
	vector GetHeadHeight(IEntity entity)
	{
		return m_HeadCache.Get(entity);
	}
	
	vector ComputeHeadHeight(IEntity entity)
	{
		Animation anim = entity.GetAnimation();
		TNodeId headIndex = anim.GetBoneIndex("Head");
		vector matPos[4];
		anim.GetBoneMatrix(headIndex, matPos);
		return entity.CoordToParent(matPos[3]);
		
	}
	
	bool IsInBuildingOrVehicle(IEntity senderEntity, out IEntity building = null, out bool isVehicle = false)
	{
		bool found = false;
		ref array<IEntity> excludeEntities = {};
		IEntity foundEnitity;
		excludeEntities.Insert(senderEntity);
		while (!found)
		{
			autoptr TraceParam p = new TraceParam;
			vector end = GetHeadHeight(senderEntity);
			end[1] = end[1] + 10;
			p.ExcludeArray = excludeEntities;
			p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
			p.LayerMask = EPhysicsLayerDefs.Projectile;
			p.Start = GetHeadHeight(senderEntity);
			p.End = end;
			float distance = GetGame().GetWorld().TraceMove(p, null);
			if (p.TraceEnt == null)
			{
				found = true;
				break;
			}
				
			
			if (!p.TraceEnt.FindComponent(BaseLoadoutClothComponent))
				if (Vehicle.Cast(p.TraceEnt.GetRootParent()))
				{
					foundEnitity = p.TraceEnt;
					isVehicle = true;
					found = true;
					break;
				}
				else if (Building.Cast(p.TraceEnt))
				{
					foundEnitity = p.TraceEnt;
					found = true;
					break;
				}
			
			
			excludeEntities.Insert(p.TraceEnt);
		}
		
		if (foundEnitity)
			building = foundEnitity;
		if (!building)
			return false;
		return true;
	}
	
	bool CanPlayerSeeSender(IEntity senderEntity, IEntity player)
	{
		autoptr TraceParam p = new TraceParam;
		vector end = senderEntity.GetOrigin();
		p.Exclude = player;
		p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		p.LayerMask = EPhysicsLayerDefs.Projectile;
		p.Start = GetHeadHeight(senderEntity);
		p.End = GetHeadHeight(player);
		float distance = GetGame().GetWorld().TraceMove(p, null);
		if (distance == 1)
			return true;
		return false;
	}
	
	void DetermineHearingWindow(IEntity entity, out float top, out float bottom)
	{
		autoptr TraceParam p = new TraceParam;
		vector end = entity.GetOrigin();
		end[1] = end[1] + 100;
		p.Exclude = entity;
		p.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		p.LayerMask = EPhysicsLayerDefs.Projectile;
		p.Start = GetHeadHeight(entity);
		p.End = end;
		float distanceUp = GetGame().GetWorld().TraceMove(p, null) * 100;
		
		end[1] = end[1] + -200;
		p.End = end;
		float distanceDown = GetGame().GetWorld().TraceMove(p, null) * 100;
		vector origin = GetHeadHeight(entity);
		top = origin[1] + distanceUp;
		bottom = origin[1] - distanceDown;
	}
	
	bool CheckIfInSameVehicle(IEntity senderEntity, IEntity player)
	{
		if (!player.GetRootParent())
			return false;
		
		if (!Vehicle.Cast(player.GetRootParent()))
			return false;
		
		SCR_BaseCompartmentManagerComponent compMan = Vehicle.Cast(player.GetRootParent()).m_CompartmentMan;
		array<BaseCompartmentSlot> compartments = {};
		compMan.GetCompartments(compartments);
		foreach (BaseCompartmentSlot compartment: compartments)
		{
			if (!compartment.IsOccupied())
				continue;
			
			if (compartment.GetOccupant() == senderEntity)
				return true;
		}
		return false;
	}
	
	bool ShouldMuffleAudio(IEntity senderEntity, int playerId = 0, out int loweredDecibles = 0)
	{
		IEntity player = m_PlayerController.GetLocalControlledEntity();
		if (!player)
			return false;
		
		if (!senderEntity)
			return false;
		
		if (CanPlayerSeeSender(senderEntity, player))
			return false;
		
		IEntity receiverBuilding;
		IEntity senderBuilding;
		bool isSenderInVehicle;
		bool isPlayerInVehicle;
		bool isSenderInBuilding = IsInBuildingOrVehicle(senderEntity, senderBuilding, isSenderInVehicle);
		bool isPlayerInBuilding = IsInBuildingOrVehicle(player, receiverBuilding, isPlayerInVehicle);
		//quick sanity check as above can sometimes be off if the players hatch is open, god I gotta rewrite this
		if (CheckIfInSameVehicle(senderEntity, player))
			return false;
		
		if (!isSenderInBuilding && !isPlayerInBuilding)
			return false;
		
		if (isPlayerInBuilding != isSenderInBuilding)
		{
			if (isPlayerInVehicle || isSenderInVehicle)
				loweredDecibles = CVON_DB_ATTEN_VEHICLE;
			else
				loweredDecibles = CVON_DB_ATTEN_BUILDING;
			return true;
		}
		float top;
		float bottom;
		
		DetermineHearingWindow(player, top, bottom);
		vector senderOrigin = GetHeadHeight(senderEntity);
		if (senderOrigin[1] > top || senderOrigin[1] < bottom)
		{
			loweredDecibles = CVON_DB_ATTEN_BUILDING;
			return true;
		}
		return false;
	}
	
	void ComputeSpectatorLR(int playerId, out float outLeft, out float outRight, out int silencedDecibels = 0)
	{
		//Fill in your spectator logic here
	}

	
	//VONServerData.JSON
	//InGame, this is how teamspeak knows you are in game and need to be moved to the VON Channel.
	//This JSON handles everything we get from the server and teamspeak, so our TSCLientID we use to send to other clients so they know its us talking in TS.
	//VONChannelName and password so we know which channel to connect to and how to join if it has a password.
	//VONServerData
	//This iterates over all the entries in m_aLocalActiveVONEntries in our player controlller
	//ISTransmitting is how we communicate to TS that we need to open our mic in teamspeak.
	//VONType is how teamspeak knows what to do with our VONEntry, if its direct it just uses the data we give it to simualte 3D sound.
	//If its radio, it checks to see if we are on the frequency, if so applies the filters. If not then if distance is not -1 it plays it as a direct entry.
	//Frequency, this is how Teamspeak checks if a radio entry is heard on radio by you by comparing it to the data in RadioData.json.
	//Left gain and Right gain is how teamspeak determines how much each ear hears during a direct broadcast.
	//Volume is in meters your voice should travel to be heard at a decent volume, so 40 at 40m you can still hear the voice really well.
	//Distance, teamspeak uses this and volume to build a sound fall off curve and adjust the voice coming in.
	//ConnectionQuality, this handles how well your signal is to another radio, the lower the value the more distorted and quiet your voice is, as well as increasing the noise.
	//FactionKey, this always gets set but it just determines if encryption is enabled that we can hear the radio broadcast coming in.
	
	//Chain from broadcast is
	//You broadcast to selected players -> They receive it in their player controller and add it locally -> 
	//This WriteJson() method is being constantly called it will then write the data to the json for teamspeak to interpret ->
	//Teamspeak reads the JSON, parses the data and compares it using the clientId to see if they should hear this baffoon ->
	//The entry is removed from the local array and is no longer written to the JSON, there for teamspeak just mutes that client as he has no data in the JSON.
	//==========================================================================================================================================================================
	// checkServerData: when false, skip the VONServerData.json read entirely.
	// Called every 50ms but checkServerData is only true once per second to avoid
	// opening and parsing a file 20 times per second for data that rarely changes.
	void WriteJSON(bool checkServerData = true)
	{
		if (!GetGame().GetPlayerController())
			return;
		if (checkServerData)
		{
			SCR_JsonLoadContext VONLoad = new SCR_JsonLoadContext();
			if (!VONLoad.LoadFromFile("$profile:/VONServerData.json"))
			{
				SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
				VONServerData.StartObject("ServerData");
				VONServerData.SetMaxDecimalPlaces(1);
				VONServerData.WriteValue("InGame", true);
				VONServerData.WriteValue("InGameName", m_PlayerManager.GetPlayerName(m_PlayerController.GetPlayerId()));
				VONServerData.WriteValue("TSClientID", m_PlayerController.GetTeamspeakClientId());
				VONServerData.WriteValue("TSPluginVersion", m_PlayerController.m_sTeamspeakPluginVersion);
				VONServerData.WriteValue("VONChannelName", m_VONGameModeComponent.m_sTeamSpeakChannelName);
				VONServerData.WriteValue("VONChannelPassword", m_VONGameModeComponent.m_sTeamSpeakChannelPassword);
				VONServerData.WriteValue("TSServerIp", m_VONGameModeComponent.m_sTeamSpeakServerIP);
				VONServerData.WriteValue("TSServerPassword", m_VONGameModeComponent.m_sTeamSpeakServerPassword);
				VONServerData.EndObject();
				VONServerData.SaveToFile("$profile:/VONServerData.json");
			}
			else
			{
				string ChannelName;
				string ChannelPassword;
				int TSClientId = 0;
				bool InGame;
				string gameName;
				VONLoad.StartObject("ServerData");
				VONLoad.ReadValue("InGame", InGame);
				VONLoad.ReadValue("VONChannelName", ChannelName);
				VONLoad.ReadValue("VONChannelPassword", ChannelPassword);
				VONLoad.ReadValue("TSPluginVersion", m_PlayerController.m_sTeamspeakPluginVersion);
				VONLoad.ReadValue("TSClientID", TSClientId);
				VONLoad.ReadValue("InGameName", gameName);
				if (m_PlayerController.GetTeamspeakClientId() != TSClientId && m_fWriteTeamspeakClientIdCooldown <= 0)
				{
					m_fWriteTeamspeakClientIdCooldown = 1;
					m_PlayerController.SetTeamspeakClientId(TSClientId);
				}
				
				VONLoad.EndObject();
				if (gameName == "" || ChannelName != m_VONGameModeComponent.m_sTeamSpeakChannelName || ChannelPassword != m_VONGameModeComponent.m_sTeamSpeakChannelPassword || m_PlayerController.m_sTeamspeakPluginVersion != m_VONGameModeComponent.m_sTeamspeakPluginVersion || InGame != true)
				{
					SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
					VONServerData.StartObject("ServerData");
					VONServerData.SetMaxDecimalPlaces(1);
					VONServerData.WriteValue("InGame", true);
					VONServerData.WriteValue("InGameName", m_PlayerManager.GetPlayerName(m_PlayerController.GetPlayerId()));
					VONServerData.WriteValue("TSClientID", m_PlayerController.GetTeamspeakClientId());
					VONServerData.WriteValue("TSPluginVersion", m_PlayerController.m_sTeamspeakPluginVersion);
					VONServerData.WriteValue("VONChannelName", m_VONGameModeComponent.m_sTeamSpeakChannelName);
					VONServerData.WriteValue("VONChannelPassword", m_VONGameModeComponent.m_sTeamSpeakChannelPassword);
					VONServerData.WriteValue("TSServerIp", m_VONGameModeComponent.m_sTeamSpeakServerIP);
					VONServerData.WriteValue("TSServerPassword", m_VONGameModeComponent.m_sTeamSpeakServerPassword);
					VONServerData.EndObject();
					VONServerData.SaveToFile("$profile:/VONServerData.json");
				}
			}
		}
		#ifdef ENABLE_DIAG
		#else
		//Hijack this whole process to load the initial warning menu
		if (m_VONGameModeComponent.m_bTeamspeakChecks)
		{	
			if (m_PlayerController.GetTeamspeakClientId() == 0 && !m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime)
			{
				if (!m_MenuManager.GetTopMenu())
					m_MenuManager.OpenMenu(ChimeraMenuPreset.CVON_WarningMenu);
				else if (!m_MenuManager.GetTopMenu().IsInherited(CVON_WarningMenu) && !m_MenuManager.GetTopMenu().IsInherited(PauseMenuUI))
					m_MenuManager.OpenMenu(ChimeraMenuPreset.CVON_WarningMenu);
			}
				
		}
		#endif
		SCR_JsonSaveContext VONSave = new SCR_JsonSaveContext();
		VONSave.WriteValue("IsTransmitting", m_bIsBroadcasting);
		IEntity localEntity = m_Camera;
		if (!localEntity)
			return;
		// Dirty flag: if nothing changed since the last write, skip SaveToFile entirely.
		// JSON serialization still happens in memory (cheap); the disk write is what we avoid.
		// Also dirty when the entry count changes — this catches the case where the last
		// speaker leaves range and is removed from m_aLocalEntries entirely: the foreach
		// below never runs, so only this count comparison can trigger the flush that removes
		// their entry from VONData.json and lets TeamSpeak mute them.
		int currentEntryCount = m_PlayerController.m_aLocalEntries.Count();
		bool dirty = (m_bIsBroadcasting != m_bLastWrittenTransmitting) ||
					 (currentEntryCount != m_iLastWrittenEntryCount);
		foreach (int playerId, CVON_VONContainer container: m_PlayerController.m_aLocalEntries)
		{
			IEntity soundSource;
			float left = 0;
			float right = 0;
			float behindIntensity = 0;
			int loweredDecibels = 0;
			string frequency = container.m_sFrequency;
			if (!m_CharacterController)
			{
				//Cuts off all incoming audio, cause we're dead.
				left = 0;
				right = 0;
				frequency = "";
			}
			else if (m_CharacterController.GetLifeState() == ECharacterLifeState.DEAD)
			{
				//Cuts off all incoming audio, cause we're dead.
				left = 0;
				right = 0;
				frequency = "";
			}
			else if (container.m_bIsSpectator)
			{
				ComputeSpectatorLR(playerId, left, right)
			}
			else if (Replication.FindItem(container.m_SenderRplId) && !container.m_SoundSource && container.m_fDistanceToSender != -1)
			{
				soundSource = RplComponent.Cast(Replication.FindItem(container.m_SenderRplId)).GetEntity();
				container.m_SoundSource = soundSource;
				ShouldMuffleAudio(container.m_SoundSource, playerId, loweredDecibels);
				if (loweredDecibels < 0)
					ComputeStereoLR(localEntity, GetHeadHeight(soundSource), container.m_iVolume/1.25, playerId, behindIntensity, left, right);
				else
					ComputeStereoLR(localEntity, GetHeadHeight(soundSource), container.m_iVolume, playerId, behindIntensity, left, right);
			}
			else if (container.m_SoundSource && container.m_fDistanceToSender != -1)
			{
				ShouldMuffleAudio(container.m_SoundSource, playerId, loweredDecibels);
				if (loweredDecibels < 0)
					ComputeStereoLR(localEntity, GetHeadHeight(container.m_SoundSource), container.m_iVolume/1.25, playerId, behindIntensity, left, right);
				else
					ComputeStereoLR(localEntity, GetHeadHeight(container.m_SoundSource), container.m_iVolume, playerId, behindIntensity, left, right);
			}
			
			
			if (container.m_eVonType == CVON_EVONType.RADIO)
			{
				if (container.m_SoundSource)
				{
					container.m_fConnectionQuality = GetSignalStrength(vector.Distance(localEntity.GetOrigin(), container.m_SoundSource.GetOrigin()), container.m_iMaxDistance);
				}
				else
					container.m_fConnectionQuality = GetSignalStrength(vector.Distance(localEntity.GetOrigin(), container.m_vSenderLocation), container.m_iMaxDistance);
			}
			
			if (!m_FactionManager)
				m_FactionManager = SCR_FactionManager.Cast(GetGame().GetFactionManager());
			
			if (!m_BaseGamemode)
				m_BaseGamemode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
			
			bool sameLanguage = true;
			if (m_FactionManager)
			{
				if (m_FactionManager.GetPlayerFaction(m_PlayerController.GetPlayerId()) != m_FactionManager.GetPlayerFaction(container.m_iPlayerId) && m_BaseGamemode.IsBabbelEnabled())
					sameLanguage = false;
			}

			// Check if this entry differs enough from what was last written (epsilon ~0.5%).
			const float EPS = 0.005;
			if (!dirty)
			{
				if (Math.AbsFloat(left  - container.m_fCachedLeft)   > EPS ||
					Math.AbsFloat(right - container.m_fCachedRight)  > EPS ||
					Math.AbsFloat(behindIntensity - container.m_fCachedBehind) > EPS ||
					Math.AbsFloat(container.m_fConnectionQuality - container.m_fCachedConnQ) > EPS ||
					loweredDecibels  != container.m_iCachedDecibels ||
					sameLanguage     != container.m_bCachedSameLang ||
					frequency        != container.m_sCachedFreq)
					dirty = true;
			}
				
			VONSave.StartObject(m_PlayerController.GetPlayersTeamspeakClientId(playerId).ToString());
			VONSave.SetMaxDecimalPlaces(3);
			VONSave.WriteValue("VONType", container.m_eVonType);
			VONSave.WriteValue("Frequency", frequency);
			VONSave.WriteValue("LeftGain", left);
			VONSave.WriteValue("RightGain", right);
			VONSave.WriteValue("MuffledDecibels", loweredDecibels);
			VONSave.WriteValue("ConnectionQuality", container.m_fConnectionQuality);
			VONSave.WriteValue("FactionKey", container.m_sFactionKey);
			VONSave.WriteValue("PlayerId", playerId);
			VONSave.WriteValue("BehindIntensity", behindIntensity);
			VONSave.WriteValue("SameLanguage", sameLanguage);
			VONSave.EndObject();

			// Update the per-entry cache so we can detect the next change.
			container.m_fCachedLeft    = left;
			container.m_fCachedRight   = right;
			container.m_fCachedBehind  = behindIntensity;
			container.m_iCachedDecibels = loweredDecibels;
			container.m_fCachedConnQ   = container.m_fConnectionQuality;
			container.m_bCachedSameLang = sameLanguage;
			container.m_sCachedFreq    = frequency;
		}
		if (dirty)
		{
			VONSave.SaveToFile("$profile:/VONData.json");
			m_bLastWrittenTransmitting = m_bIsBroadcasting;
			m_iLastWrittenEntryCount = currentEntryCount;
		}
	}
	
	
	//Resets these values so we can leave the channel on teamspeak
	//It checks if the bool InGame is true, and if so moves you to the voip channel
	//==========================================================================================================================================================================
	void ~SCR_VONController()
	{
		if (m_aPlayerIdsBroadcastedTo.Count() > 0)
		{
			foreach (int playerId: m_aPlayerIdsBroadcastedTo)
			{
				m_PlayerController.BroadcastRemoveLocalVONToServer(m_PlayerController.GetPlayerId());
			}
			m_aPlayerIdsBroadcastedTo.Clear();
		}
		
		SCR_JsonSaveContext VONServerData = new SCR_JsonSaveContext();
		VONServerData.StartObject("ServerData");
		VONServerData.WriteValue("InGame", false);
		VONServerData.WriteValue("InGameName", "");
		VONServerData.WriteValue("TSClientID", 0);
		VONServerData.WriteValue("TSPluginVersion", 0);
		VONServerData.WriteValue("VONChannelName", "");
		VONServerData.WriteValue("VONChannelPassword", "");
		VONServerData.WriteValue("TSServerIp", "");
		VONServerData.WriteValue("TSServerPassword", "");
		VONServerData.EndObject();
		VONServerData.SaveToFile("$profile:/VONServerData.json");
	}
}