
// Not working as of rn, you cant view your own actions on your own character so this method of toggle headset state is canned for now.
class CVON_LowerHeadsetAction: ScriptedUserAction
{
	SCR_PlayerController m_PlayerController;
	IEntity m_Owner;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		m_Owner = pOwnerEntity;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return true;
		
		if (!m_PlayerController)
			m_PlayerController = SCR_PlayerController.Cast(GetGame().GetPlayerController());
	
		if (!m_PlayerController)
			return false;
	
		if (m_PlayerController.GetControlledEntity() != m_Owner)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_PlayerController.GetHeadsetLoweredState())
			outName = "Lower Headset";
		else
			outName = "Raise Headset";
	
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		m_PlayerController.ToggleHeadsetLoweredState();
	}
}