[BaseContainerProps()]
modded class SCR_VONMenu
{
	//Ignore this for now, used for handmic shit that does not work.
	bool m_bOpeningHandMic = false;
	void OpenMenu(array<string> freqs, int amountOfRadios, array<string> radioNames)
	{
		
		m_RadialController.Control(SCR_PlayerController.GetLocalControlledEntity(), m_RadialMenu);
		SCR_HUDManagerComponent hud = GetGame().GetHUDManager();
		m_Display = SCR_VONRadialDisplay.Cast(hud.FindInfoDisplay(SCR_VONRadialDisplay));
		m_RadialMenu.SetMenuDisplay(m_Display);
		m_RadialController.SetEnableControl(true);		
		m_RadialMenu.ClearEntries();
		for (int i = 0; i < amountOfRadios; i++)
		{			
			SCR_VONEntryRadio radioEntry = new SCR_VONEntryRadio();
			radioEntry.SetRadioFrequency(freqs.Get(i));
			radioEntry.m_bIsNotLocal = true;
			radioEntry.m_sRadioName = radioNames.Get(i);
			AddRadialEntry(radioEntry);
		}
		
		if (m_RadialMenu.GetEntryCount() < 8 && m_RadialMenu.GetEntryCount() % 2 != 0)
		{
			SCR_VONEntry dummy = new SCR_VONEntry();
			dummy.Enable(false);
			dummy.SetIcon("{FDD5423E69D007F8}UI/Textures/Icons/icons_wrapperUI-128.imageset", "VON_radio");
			AddRadialEntry(dummy);
		}
		m_bOpeningHandMic = true;
		m_RadialController.OnInputOpen();
	}
	
	//Opens the menu of the radio we select.
	//==========================================================================================================================================================================
	override protected void OnEntryPerformed(SCR_SelectionMenu menu, SCR_SelectionMenuEntry entry)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.OnEntryPerformed(menu, entry);
			return;
		}
		SCR_VONEntryRadio entryRadioVON = SCR_VONEntryRadio.Cast(entry);
		IEntity radio = entryRadioVON.GetTransceiver().GetRadio().GetOwner();
		CVON_RadioComponent.Cast(radio.FindComponent(CVON_RadioComponent)).OpenMenu();
		
		m_RadialMenu.UpdateEntries();
	}
	
	//So we only have one entry per radio in the radial menu.
	//==========================================================================================================================================================================
	override protected void OnInputOpenMenu(SCR_RadialMenuController controller, bool hasControl)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.OnInputOpenMenu(controller, hasControl);
			return;
		}
		/*if (!m_RadialMenu.HasDisplay())	// TODO currently has to be called after control, sequencing needs adjusting
		{
			SCR_HUDManagerComponent hud = GetGame().GetHUDManager();
			m_Display = SCR_VONRadialDisplay.Cast(hud.FindInfoDisplay(SCR_VONRadialDisplay));
			m_RadialMenu.SetMenuDisplay(m_Display);
		}*/
		if (!hasControl && !m_bIsDisabled)	
		{
			m_RadialController.Control(GetGame().GetPlayerController(), m_RadialMenu);
			
			SCR_HUDManagerComponent hud = GetGame().GetHUDManager();
			m_Display = SCR_VONRadialDisplay.Cast(hud.FindInfoDisplay(SCR_VONRadialDisplay));
			m_RadialMenu.SetMenuDisplay(m_Display);
		}
		else if (m_bIsDisabled || m_RadialMenu.IsOpened())
		{
			m_RadialMenu.Close();
			m_RadialController.SetEnableControl(false);
			return;
		}
				
		m_RadialController.SetEnableControl(true);
		
		if (!m_VONController.GetVONComponent())
		{
			if (!m_VONController.AssignVONComponent())
			{
				m_RadialController.SetEnableControl(false);
				return;
			}
		}
		
		if (m_VONController.GetVONEntryCount() == 0)
		{
			m_RadialController.SetEnableControl(false);
			return;
		}
		
		if (m_bOpeningHandMic)
		{
			m_bOpeningHandMic = false;
			return;
		}
		
		m_RadialMenu.ClearEntries();
		
		array<ref SCR_VONEntry> entries = {};
		m_VONController.GetVONEntries(entries);
		
		//We do this so no matter the amount of trancievers on a vanilla radio it only adds one entry, just cause we only do SC for now.
		ref array<IEntity> itemsAdded = {};
		foreach (SCR_VONEntry entry : entries)
		{
			if (!SCR_VONEntryRadio.Cast(entry).GetTransceiver())
			{
				m_VONController.RemoveEntry(entry);
				continue;
			}
			
			if (!SCR_VONEntryRadio.Cast(entry).GetTransceiver().GetRadio())
			{
				m_VONController.RemoveEntry(entry);
				continue;
			}
			
			if (!SCR_VONEntryRadio.Cast(entry).GetTransceiver().GetRadio().GetOwner())
			{
				m_VONController.RemoveEntry(entry);
				continue;
			}
				
			if (itemsAdded.Contains(SCR_VONEntryRadio.Cast(entry).GetTransceiver().GetRadio().GetOwner()))
				continue;
			AddRadialEntry(entry);
			itemsAdded.Insert(SCR_VONEntryRadio.Cast(entry).GetTransceiver().GetRadio().GetOwner());
		}
		
		if (m_RadialMenu.GetEntryCount() < 8 && m_RadialMenu.GetEntryCount() % 2 != 0)
		{
			SCR_VONEntry dummy = new SCR_VONEntry();
			dummy.Enable(false);
			dummy.SetIcon("{FDD5423E69D007F8}UI/Textures/Icons/icons_wrapperUI-128.imageset", "VON_radio");
			AddRadialEntry(dummy);
		}
	}
}