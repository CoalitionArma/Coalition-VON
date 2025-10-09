//Menus to open
modded enum ChimeraMenuPreset
{
	ANPRC152,
	ANPRC150,
	ANPRC117,
	R168,
	R187
}

//The different menus we can see on the screen.
enum CVON_ERadioMenu
{
	FREQ,
	VOL,
	STEREO,
	TIME
}

//What ear to hear the radio transmission in.
enum CVON_EStereo
{
	BOTH,
	RIGHT,
	LEFT
}

class CVON_RadioMenu: MenuBase
{
	Widget m_wRoot;
	
	//Used to store frequency when you are editing it
	string m_sTempFrequency = "_____";
	
	//Same but with volume
	string m_sTempVolume = "_";
	
	//Top Line, says "Frequency" in frequency menu
	TextWidget m_wLine1;
	
	//Bottom Line, says the frequency in frequency menu
	TextWidget m_wLine2;
	
	//Left most number, used in the time menu to show + or - time.
	TextWidget m_wLine3;
	
	//Knob Image Widget
	ImageWidget m_wKnobWidget;
	
	//Whats the current menu.
	CVON_ERadioMenu m_ERadioMenu = CVON_ERadioMenu.FREQ;
	
	//Whats the next number we're inserting into the frequency
	int m_iToInsert = -1;
	
	//Am I editing the frequency
	bool m_bIsEditing = false;
	
	//The radio we used to open the menu from the radial von menu.
	IEntity m_RadioEntity;
	
	//Radio component from the above.
	CVON_RadioComponent m_RadioComponent;
	
	//Store the pointer cause we need this alot
	SCR_PlayerController m_PlayerController;
	
	//Used for exiting the menu
	protected bool m_bFocused = true;
	
	//More pointers
	InputManager m_InputManager;
	
	//==========================================================================================================================================================================
	override void OnMenuOpen()
	{
		m_wRoot = GetRootWidget();
		
		GetGame().GetInputManager().AddActionListener("Numpad1", EActionTrigger.DOWN, Button1);
		GetGame().GetInputManager().AddActionListener("Numpad2", EActionTrigger.DOWN, Button2);
		GetGame().GetInputManager().AddActionListener("Numpad3", EActionTrigger.DOWN, Button3);
		GetGame().GetInputManager().AddActionListener("Numpad4", EActionTrigger.DOWN, Button4);
		GetGame().GetInputManager().AddActionListener("Numpad5", EActionTrigger.DOWN, Button5);
		GetGame().GetInputManager().AddActionListener("Numpad6", EActionTrigger.DOWN, Button6);
		GetGame().GetInputManager().AddActionListener("Numpad7", EActionTrigger.DOWN, Button7);
		GetGame().GetInputManager().AddActionListener("Numpad8", EActionTrigger.DOWN, Button8);
		GetGame().GetInputManager().AddActionListener("Numpad9", EActionTrigger.DOWN, Button9);
		GetGame().GetInputManager().AddActionListener("Numpad0", EActionTrigger.DOWN, Button0);
		GetGame().GetInputManager().AddActionListener("Backspace", EActionTrigger.DOWN, Backspace);
		GetGame().GetInputManager().AddActionListener("VONMenu", EActionTrigger.DOWN, Close);
		
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Clear").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Clear);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Enter").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Enter);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button0").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button0);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button1").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button1);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button2").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button2);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button3").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button3);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button4").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button4);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button5").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button5);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button6").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button6);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button7").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button7);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button8").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button8);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Button9").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(Button9);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("MenuLeft").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(MenuLeft);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("MenuRight").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(MenuRight);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("MenuUp").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(MenuUp);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("MenuDown").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(MenuDown);
		SCR_ButtonTextComponent.Cast(m_wRoot.FindWidget("Power").FindHandler(SCR_ButtonTextComponent)).m_OnClicked.Insert(TogglePower);
		
		m_wLine1 = TextWidget.Cast(m_wRoot.FindWidget("RadioText"));
		m_wLine2 = TextWidget.Cast(m_wRoot.FindWidget("Frequency"));
		m_wLine3 = TextWidget.Cast(m_wRoot.FindWidget("Line3"));
		m_wLine3.SetVisible(false);
		m_wKnobWidget = ImageWidget.Cast(m_wRoot.FindAnyWidget("Knob"));
		
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		m_InputManager = GetGame().GetInputManager();
	}
	
	//==========================================================================================================================================================================
	override void OnMenuClose()
	{
		GetGame().GetInputManager().RemoveActionListener("Numpad1", EActionTrigger.DOWN, Button1);
		GetGame().GetInputManager().RemoveActionListener("Numpad2", EActionTrigger.DOWN, Button2);
		GetGame().GetInputManager().RemoveActionListener("Numpad3", EActionTrigger.DOWN, Button3);
		GetGame().GetInputManager().RemoveActionListener("Numpad4", EActionTrigger.DOWN, Button4);
		GetGame().GetInputManager().RemoveActionListener("Numpad5", EActionTrigger.DOWN, Button5);
		GetGame().GetInputManager().RemoveActionListener("Numpad6", EActionTrigger.DOWN, Button6);
		GetGame().GetInputManager().RemoveActionListener("Numpad7", EActionTrigger.DOWN, Button7);
		GetGame().GetInputManager().RemoveActionListener("Numpad8", EActionTrigger.DOWN, Button8);
		GetGame().GetInputManager().RemoveActionListener("Numpad9", EActionTrigger.DOWN, Button9);
		GetGame().GetInputManager().RemoveActionListener("Numpad0", EActionTrigger.DOWN, Button0);
		GetGame().GetInputManager().RemoveActionListener("Backspace", EActionTrigger.DOWN, Backspace);
		GetGame().GetInputManager().RemoveActionListener("VONMenu", EActionTrigger.DOWN, Close);
	}
	
	//==========================================================================================================================================================================
	override void OnMenuUpdate(float tDelta)
	{
		//Initialize the radios channels
		if (!m_RadioComponent)
		{
			m_RadioComponent = CVON_RadioComponent.Cast(m_RadioEntity.FindComponent(CVON_RadioComponent));
			if (m_RadioComponent.m_bPower)
				PowerOn();
			else
				PowerOff();
		}
			
		//Update the time if its the time menu
		if (m_ERadioMenu == CVON_ERadioMenu.TIME)
		{
			ChimeraWorld world = GetGame().GetWorld();
			int h, m, s;
			world.GetTimeAndWeatherManager().GetHoursMinutesSeconds(h, m, s);
			if (m_RadioComponent.m_iTimeDeviation < 0)
				m_wLine3.SetText(m_RadioComponent.m_iTimeDeviation.ToString());
			else if (m_RadioComponent.m_iTimeDeviation >= 0)
				m_wLine3.SetText("+" + m_RadioComponent.m_iTimeDeviation.ToString());
			m_wLine2.SetText(((h * 60 * 60) + ((m + m_RadioComponent.m_iTimeDeviation) * 60) + s).ToString());
		}
	}	
	//Shifts the menu down the enum
	//==========================================================================================================================================================================
	void MenuLeft()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu == 0)
		{
			m_ERadioMenu = 3;
			LoadNewMenu();
			return;
		}
		
		m_ERadioMenu -= 1;
		LoadNewMenu();
	}
	
	//Shifts it up the enum
	//==========================================================================================================================================================================
	void MenuRight()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu == 3)
		{
			m_ERadioMenu = 0;
			LoadNewMenu();
			return;
		}
		
		m_ERadioMenu += 1;
		LoadNewMenu();
	}
	
	//Handles what to do per menu when using the up and down keys
	//==========================================================================================================================================================================
	void MenuUp()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		switch (m_ERadioMenu)
		{
			case CVON_ERadioMenu.FREQ:
			{
				ChangeChannel(1);
				break;
			}
			case CVON_ERadioMenu.VOL:
			{
				ChangeVolume(1);
				break;
			}
			case CVON_ERadioMenu.STEREO:
			{
				ChangeStereo(1);
				break;
			}
			case CVON_ERadioMenu.TIME:
			{
				m_RadioComponent.m_iTimeDeviation += 1;
				m_RadioComponent.UpdateTimeDeviationClient(m_RadioComponent.m_iTimeDeviation);
				break;
			}
		}
	}
	
	//Same as above
	//==========================================================================================================================================================================
	void MenuDown()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		switch (m_ERadioMenu)
		{
			case CVON_ERadioMenu.FREQ:
			{
				ChangeChannel(-1);
				break;
			}
			case CVON_ERadioMenu.VOL:
			{
				ChangeVolume(-1);
				break;
			}
			case CVON_ERadioMenu.STEREO:
			{
				ChangeStereo(-1);
				break;
			}
			case CVON_ERadioMenu.TIME:
			{
				m_RadioComponent.m_iTimeDeviation -= 1;
				m_RadioComponent.UpdateTimeDeviationClient(m_RadioComponent.m_iTimeDeviation);
				break;
			}
		}
	}
	
	void PowerOff()
	{
		m_wLine1.SetVisible(false);
		m_wLine2.SetVisible(false);
		m_wLine3.SetVisible(false);
		m_wKnobWidget.LoadImageTexture(1, m_RadioComponent.m_sDialOff);
		m_wKnobWidget.SetImage(1);
		m_bIsEditing = false;
	}
	
	void PowerOn()
	{
		m_wLine1.SetVisible(true);
		m_wLine2.SetVisible(true);
		m_wLine3.SetVisible(true);
		UpdateChannelMenu(0);
		LoadFrequencyMenu(m_RadioComponent.m_iCurrentChannel);
		m_wKnobWidget.LoadImageTexture(1, m_RadioComponent.m_sDialOn);
		m_wKnobWidget.SetImage(1);
		m_bIsEditing = false;
	}
	
	void TogglePower()
	{
		m_RadioComponent.m_bPower = !m_RadioComponent.m_bPower;
		#ifdef WORKBENCH
		#else
		m_RadioComponent.TogglePowerClient();
		#endif
		if (m_RadioComponent.m_bPower)
			PowerOn();
		else
			PowerOff();
		
		AudioSystem.PlaySound("{E6153160DA26A03A}UI/sounds/RadioKnobClick.wav");
	}
	
	//Changes the channel based on the input from the MenuUp and Down methods
	//==========================================================================================================================================================================
	void ChangeChannel(int input)
	{
		if (m_RadioComponent.m_iCurrentChannel == 1 && input == -1)
			return;
		else if (m_RadioComponent.m_iCurrentChannel == 99 && input == 1)
		{
			m_RadioComponent.UpdateChannelClient(1);
			UpdateChannelMenu(9);
			LoadFrequencyMenu(1);
			return;
		}
		
		UpdateChannelMenu(input);
		//Have to compensate in multiplayer the delay between when we set the new channel on the server and it instantly happening on the client
		#ifdef WORKBENCH
		LoadFrequencyMenu(m_RadioComponent.m_iCurrentChannel);
		#else
		LoadFrequencyMenu(m_RadioComponent.m_iCurrentChannel + input);
		#endif
	}
	
	//Changes the stereo settings based on the input from the MenuUp and Down methods
	//==========================================================================================================================================================================
	void ChangeStereo(int input)
	{
		if (m_RadioComponent.m_eStereo == 0 && input == -1)
		{
			m_RadioComponent.m_eStereo = 2;
			m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
			LoadStereoMenu();
			return;
		}
		else if (m_RadioComponent.m_eStereo == 2 && input == 1)
		{
			m_RadioComponent.m_eStereo = 0;
			m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
			LoadStereoMenu();
			return;
		}
		
		m_RadioComponent.m_eStereo += input;
		m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		m_PlayerController.UpdateSettings();
		LoadStereoMenu();
	}
	
	//Changes the volume based on the input from the MenuUp and Down methods
	//==========================================================================================================================================================================
	void ChangeVolume(int input)
	{
		if (m_RadioComponent.m_iVolume == 0 && input == -1)
		{
			m_RadioComponent.m_iVolume = 9;
			m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
			LoadVolumeMenu();
			return;
		}
		else if (m_RadioComponent.m_iVolume == 9 && input == 1)
		{
			m_RadioComponent.m_iVolume = 0;
			m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
			LoadVolumeMenu();
			return;
		}
		
		m_RadioComponent.m_iVolume += input;
		m_RadioComponent.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		m_PlayerController.UpdateSettings();
		LoadVolumeMenu();
	}
	
	
	//How we load a new menu, to make things simple
	//==========================================================================================================================================================================
	void LoadNewMenu()
	{
		switch (m_ERadioMenu)
		{
			case CVON_ERadioMenu.FREQ: {LoadFrequencyMenu(m_RadioComponent.m_iCurrentChannel); break;}
			case CVON_ERadioMenu.VOL: {LoadVolumeMenu(); break;}
			case CVON_ERadioMenu.STEREO: {LoadStereoMenu(); break;}
			case CVON_ERadioMenu.TIME: {LoadTimeMenu(); break;}
		}
	}
	
	//Used when we need to shift to a new channel
	//==========================================================================================================================================================================
	void UpdateChannelMenu(int input)
	{
		int channels = m_RadioComponent.m_aChannels.Count();
		if (channels < m_RadioComponent.m_iCurrentChannel + input)
		{
			m_RadioComponent.AddChannelClient();
			m_RadioComponent.UpdateChannelClient(m_RadioComponent.m_iCurrentChannel + input);
			m_RadioComponent.UpdateFrequencyClient("55500");
		}
		else
		{
			string freq = m_RadioComponent.m_aChannels.Get(m_RadioComponent.m_iCurrentChannel + input - 1);
			m_RadioComponent.UpdateChannelClient(m_RadioComponent.m_iCurrentChannel + input);
			m_RadioComponent.UpdateFrequencyClient(freq);
		}
	}
	
	//==========================================================================================================================================================================
	void LoadFrequencyMenu(int channel)
	{
		m_wLine1.SetText("Frequency");
		if(m_RadioComponent.m_aChannels.Count() < channel)
			m_wLine2.SetText(PrepFinalFreq("55500", channel));
		else
			m_wLine2.SetText(PrepFinalFreq(m_RadioComponent.m_aChannels.Get(channel - 1), channel));
		m_wLine3.SetVisible(false);
	}
	
	//==========================================================================================================================================================================
	void LoadVolumeMenu()
	{
		m_wLine1.SetText("Volume");
		m_wLine2.SetText(m_RadioComponent.m_iVolume.ToString());
		m_wLine3.SetVisible(false);
	}
	
	//==========================================================================================================================================================================
	void LoadStereoMenu()
	{
		m_wLine1.SetText("Stereo");
		switch (m_RadioComponent.m_eStereo)
		{
			case CVON_EStereo.BOTH: {m_wLine2.SetText("Both"); break;}
			case CVON_EStereo.RIGHT: {m_wLine2.SetText("Right"); break;}
			case CVON_EStereo.LEFT: {m_wLine2.SetText("Left"); break;}
		}
		m_wLine3.SetVisible(false);
		
	}
	
	//==========================================================================================================================================================================
	void LoadTimeMenu()
	{
		m_wLine1.SetText("Time");
		m_wLine2.SetText("00:00");
		m_wLine3.SetVisible(true);
	}
	
	//Preps what the frequency looks like on the screen
	//==========================================================================================================================================================================
	string PrepFinalFreq(string input, int channel)
	{
		return "0" + channel.ToString() + ": " + input;
	}
	
	//Used when we start editing
	//==========================================================================================================================================================================
	void Clear()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		m_sTempFrequency = "_____";
		m_wLine2.SetText(PrepFinalFreq(m_sTempFrequency, m_RadioComponent.m_iCurrentChannel));
		m_bIsEditing = true;
	}
	
	//Checks if the new freq is valid, if it is logs it and handles the networking
	//==========================================================================================================================================================================
	void Enter()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		int tempFreq = m_sTempFrequency.ToInt();
		if (tempFreq < 30000 || tempFreq > 87975 || tempFreq % 25 != 0)
		{
			m_wLine2.SetText(PrepFinalFreq(tempFreq.ToString(), m_RadioComponent.m_iCurrentChannel));
			m_sTempFrequency = "_____";
			return;
		}
		m_RadioComponent.UpdateFrequencyClient(tempFreq.ToString());
		m_wLine2.SetText(PrepFinalFreq(tempFreq.ToString(), m_RadioComponent.m_iCurrentChannel));
		m_sTempFrequency = "_____";
		m_bIsEditing = false;
	}
	
	//Used to update the frequency when another key is pressed
	//==========================================================================================================================================================================
	void UpdateScreen()
	{
		if (!m_sTempFrequency.Contains("_"))
			return;
		 
		if (m_iToInsert == -1)
			return;
		
		string newFreq = m_sTempFrequency + m_iToInsert.ToString();
		m_iToInsert = -1;
		newFreq = newFreq.Substring(1, 5);
		m_sTempFrequency = newFreq;
		m_wLine2.SetText(PrepFinalFreq(m_sTempFrequency, m_RadioComponent.m_iCurrentChannel));
		m_bIsEditing = true;
	}
	
	//Deletes a character on the screen when editing
	//==========================================================================================================================================================================
	void Backspace()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (!m_bIsEditing)
			return;

		string newFreq = "_" + m_sTempFrequency;
		newFreq = newFreq.Substring(0, 5);
		m_sTempFrequency = newFreq;
		m_wLine2.SetText(PrepFinalFreq(m_sTempFrequency, m_RadioComponent.m_iCurrentChannel));
	}
	
	//Im not marking all these below but it's how you type
	//==========================================================================================================================================================================
	void Button0()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 0;
		UpdateScreen();
	}
	
	void Button1()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 1;
		UpdateScreen();
	}
	
	void Button2()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 2;
		UpdateScreen();
	}
	
	void Button3()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 3;
		UpdateScreen();
	}
	
	void Button4()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 4;
		UpdateScreen();
	}
	
	void Button5()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 5;
		UpdateScreen();
	}
	
	void Button6()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 6;
		UpdateScreen();
	}
	
	void Button7()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 7;
		UpdateScreen();
	}
	
	void Button8()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 8;
		UpdateScreen();
	}
	
	void Button9()
	{
		if (!m_RadioComponent.m_bPower)
			return;
		
		if (m_ERadioMenu != CVON_ERadioMenu.FREQ)
			return;
		
		m_iToInsert = 9;
		UpdateScreen();
	}
	
	override void OnMenuFocusLost()
	{
		m_bFocused = false;
		m_InputManager.RemoveActionListener(UIConstants.MENU_ACTION_OPEN, EActionTrigger.DOWN, Close);
		#ifdef WORKBENCH
			m_InputManager.RemoveActionListener(UIConstants.MENU_ACTION_OPEN_WB, EActionTrigger.DOWN, Close);
		#endif
	}

	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	override void OnMenuFocusGained()
	{
		m_bFocused = true;
		m_InputManager.AddActionListener(UIConstants.MENU_ACTION_OPEN, EActionTrigger.DOWN, Close);
		#ifdef WORKBENCH
			m_InputManager.AddActionListener(UIConstants.MENU_ACTION_OPEN_WB, EActionTrigger.DOWN, Close);
		#endif
	}
}