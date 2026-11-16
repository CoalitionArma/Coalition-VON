//"Pick Up Infantry Phone" / "Hang Up Infantry Phone" at the back of an armoured vehicle: while you hold it
//you're on the crew intercom (CVON_VehicleRadioComponent infantry phone). The action's context covers the
//whole vehicle (like vanilla's Unflipper), so the action itself only shows to someone standing at the rear.
class CVON_InfantryPhoneAction: SCR_ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	protected CVON_VehicleRadioComponent GetComms()
	{
		IEntity vehicle = GetOwner();
		if (!vehicle)
			return null;
		return CVON_VehicleRadioComponent.Cast(vehicle.FindComponent(CVON_VehicleRadioComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		CVON_VehicleRadioComponent comms = GetComms();
		if (pc && comms)
			pc.ToggleInfantryPhone(comms.GetRplId());
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
			return false;

		CVON_VehicleRadioComponent comms = GetComms();
		if (!comms || !comms.HasInfantryPhone())
			return false;

		if (comms.IsPhoneHeldBy(user))
			return true;

		//Someone else is holding it
		if (comms.GetPhoneHolder())
			return false;

		return comms.CanUsePhone(user, CVON_VehicleRadioComponent.PHONE_REACH);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		CVON_VehicleRadioComponent comms = GetComms();
		if (comms && comms.IsPhoneHeldBy(SCR_PlayerController.GetLocalControlledEntity()))
			outName = "Hang Up Infantry Phone";
		else
			outName = "Pick Up Infantry Phone";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
