//"Turn Speaker On" / "Turn Speaker Off" at a station (CVON_StationaryRadioComponent): the set plays its net out
//loud for the people around it. Anyone at the set can switch it, within SPEAKER_SWITCH_REACH; the first player
//of a military faction to use an unkeyed set keys it, as picking up its handset does.
class CVON_StationSpeakerAction: SCR_ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	protected CVON_StationaryRadioComponent GetStation()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return null;
		return CVON_StationaryRadioComponent.Cast(owner.FindComponent(CVON_StationaryRadioComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		CVON_StationaryRadioComponent station = GetStation();
		if (pc && station)
			pc.ToggleVehicleSpeaker(station.GetRplId());
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
			return false;

		CVON_StationaryRadioComponent station = GetStation();
		return station && station.IsWithinReach(user, CVON_StationaryRadioComponent.SPEAKER_SWITCH_REACH);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		CVON_StationaryRadioComponent station = GetStation();
		if (station && station.IsSpeakerOn())
			outName = "Turn Speaker Off";
		else
			outName = "Turn Speaker On";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
