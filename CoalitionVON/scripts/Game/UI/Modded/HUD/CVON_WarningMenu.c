modded enum ChimeraMenuPreset
{
	CVON_WarningMenu
}

class CVON_WarningMenu: ChimeraMenuBase
{
	const string LINK_TO_COPY = "https://github.com/CoalitionArma/Coalition-VON/releases";
	SCR_ButtonTextComponent m_wCopyButton;
	Widget m_wRoot;
	SCR_PlayerController m_PlayerController;
	CVON_VONGameModeComponent m_VONGamemode;
	TextWidget m_wExplanation;
	
	//Used for exiting the menu
	protected bool m_bFocused = true;
	
	override void OnMenuOpen()
	{
		m_wRoot = GetRootWidget();
		m_wCopyButton = SCR_ButtonTextComponent.Cast(m_wRoot.FindAnyWidget("CopyButton").FindHandler(SCR_ButtonTextComponent));
		m_wCopyButton.m_OnClicked.Insert(CopyToClipboard);
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		m_wExplanation = TextWidget.Cast(m_wRoot.FindAnyWidget("Explanation"));
		m_VONGamemode = CVON_VONGameModeComponent.GetInstance();
	}
	
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);
		if (m_PlayerController.GetTeamspeakClientId() == 0)
		{
			m_wExplanation.SetText("Teamspeak Plugin Not Detected!");
		}
		else if (m_PlayerController.m_sTeamspeakPluginVersion != m_VONGamemode.m_sTeamspeakPluginVersion)
		{
			m_wExplanation.SetText(string.Format("Teamspeak Plugin Version Mismatch! Your Version: %1 || Latest Version: %2", m_PlayerController.m_sTeamspeakPluginVersion, m_VONGamemode.m_sTeamspeakPluginVersion));
		}
		if (m_PlayerController.GetTeamspeakClientId() != 0 && m_PlayerController.m_sTeamspeakPluginVersion == m_VONGamemode.m_sTeamspeakPluginVersion)
		{
			m_PlayerController.m_bHasConnectedToTeamspeakForFirstTime = true;
			Close();
		}
	}
	
	override void OnMenuClose()
	{
		if (m_PlayerController.GetTeamspeakClientId() == 0 && m_PlayerController.m_sTeamspeakPluginVersion != m_VONGamemode.m_sTeamspeakPluginVersion)
			GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.CVON_WarningMenu);
	}
	
	void CopyToClipboard()
	{
		System.ExportToClipboard(LINK_TO_COPY);
	}
	
	override void OnMenuFocusLost()
	{
		m_bFocused = false;
		GetGame().GetInputManager().RemoveActionListener(UIConstants.MENU_ACTION_OPEN, EActionTrigger.DOWN, OpenPauseMenu);
		#ifdef WORKBENCH
			GetGame().GetInputManager().RemoveActionListener(UIConstants.MENU_ACTION_OPEN_WB, EActionTrigger.DOWN, OpenPauseMenu);
		#endif
	}

	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	override void OnMenuFocusGained()
	{
		m_bFocused = true;
		GetGame().GetInputManager().AddActionListener(UIConstants.MENU_ACTION_OPEN, EActionTrigger.DOWN, OpenPauseMenu);
		#ifdef WORKBENCH
			GetGame().GetInputManager().AddActionListener(UIConstants.MENU_ACTION_OPEN_WB, EActionTrigger.DOWN, OpenPauseMenu);
		#endif
	}
	
	protected void OpenPauseMenu()
	{
		if (!GetGame().GetMenuManager().IsAnyDialogOpen() && IsFocused())
			ArmaReforgerScripted.OpenPauseMenu();
	}
}