class CVON_LowerHeadsetButton: SCR_ButtonTextComponent
{
	SCR_PlayerController m_PlayerController;
	
	override void HandlerAttached(Widget w)
	{
		super.HandlerAttached(w);
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	}
	
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (!m_PlayerController)
			return super.OnClick(w, x, y, button);
		
		m_PlayerController.ToggleHeadsetLoweredState();
		
		if (!m_PlayerController.GetHeadsetLoweredState())
			m_wText.SetText("Lower Headset");
		else
			m_wText.SetText("Raise Headset");
		return super.OnClick(w, x, y, button);
	}
}