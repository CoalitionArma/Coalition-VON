modded class SCR_VONEntryRadio
{
	string m_sRadioFrequency;
	string m_sRadioName;
	bool m_bIsNotLocal;
	void SetRadioFrequency(string freq)
	{
		m_sRadioFrequency = freq;
	}
	override void InitEntry()
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.InitEntry();
			return;
		}
		SetCustomLayout("{249E688354EAF457}UI/layouts/HUD/VON/CVONEntry.layout");
		if (m_bIsNotLocal)
		{
			SetUsable(true);
			AdjustEntryModif(0);	
			SetChannelText("55500");
			return;
		}
		SetUsable(m_RadioTransceiver.GetRadio().IsPowered());
		
		AdjustEntryModif(0);
				
		SetChannelText(SCR_VONMenu.GetKnownChannel(m_RadioTransceiver.GetFrequency()));
	}
	//Ripped code straight from vanilla, just modified it so its a different look when you open the radial menu and so it supports our radios.
	//==========================================================================================================================================================================
	override void Update()
	{
		if (!CVON_VONGameModeComponent.GetInstance())
		{
			super.Update();
			return;
		}
		
		SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);			
		if (!entryComp)	// first update procs when this is not fetchable yet
			return;
		
		if (m_bIsNotLocal)
		{
			entryComp.SetFrequencyText(m_sRadioFrequency);
			TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetText(m_sRadioName);
			TextWidget.Cast(entryComp.GetRootWidget().FindAnyWidget("Radio")).SetColor(Color.DarkGreen);
			entryComp.SetTransceiverOpacity(1);
			
			entryComp.SetFrequencyColor(Color.FromInt(Color.WHITE));
			return;
		}
		
		if (!m_RadioTransceiver)	// TODO can happen for unclear reasons, temp fix
		{
			SCR_VONController vonContr = SCR_VONController.Cast(GetGame().GetPlayerController().FindComponent(SCR_VONController));
			if (vonContr)
				vonContr.RemoveEntry(this);
			
			return;
		}
		
		BaseRadioComponent radio = m_RadioTransceiver.GetRadio();
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(radio.GetOwner().FindComponent(CVON_RadioComponent));
		
		if (!radioComp)
			return;

		entryComp.UpdateCVONRadioWidget(radioComp, false, radioComp.m_iCurrentChannel - 1);
	}
}