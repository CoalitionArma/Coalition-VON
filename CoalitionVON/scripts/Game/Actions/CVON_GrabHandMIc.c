//"Grab Handset" / "Return Handset" on someone's back (Character_Base "Back" context): take the handset of the
//manpack radio they carry and talk and listen on it (see CVON_Manpack). Only one person holds it at a time,
//and the cord pulls it out of your hand past CVON_Manpack.HANDSET_CORD_LENGTH.
//On a station (CVON_StationaryRadioComponent) it is "Pick Up Handset" / "Return Handset", within the set's
//m_fHandsetReach.
class CVON_GrabHandMic: SCR_ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	//The manpack the owner carries, or the station the owner is
	protected CVON_RadioComponent GetRadio()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return null;

		CVON_RadioComponent radio = CVON_Manpack.GetManpackRadio(owner);
		if (!radio && !ChimeraCharacter.Cast(owner))
			radio = CVON_StationaryRadioComponent.Cast(owner.FindComponent(CVON_StationaryRadioComponent));
		return radio;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		RplComponent carrierRpl = RplComponent.Cast(pOwnerEntity.FindComponent(RplComponent));
		if (carrierRpl)
			pc.ToggleHandset(carrierRpl.Id());
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!CVON_VONGameModeComponent.GetInstance())
			return false;

		IEntity carrier = GetOwner();
		if (!carrier || carrier == user)
			return false;

		CVON_RadioComponent radio = GetRadio();
		if (!radio)
			return false;

		//Someone else is holding it
		if (radio.GetHandsetHolder() && !radio.IsHandsetHeldBy(user))
			return false;

		CVON_StationaryRadioComponent station = CVON_StationaryRadioComponent.Cast(radio);
		return !station || station.IsHandsetHeldBy(user) || station.IsWithinReach(user, station.m_fHandsetReach);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		CVON_RadioComponent radio = GetRadio();
		if (radio && radio.IsHandsetHeldBy(SCR_PlayerController.GetLocalControlledEntity()))
			outName = "Return Handset";
		else if (CVON_StationaryRadioComponent.Cast(radio))
			outName = "Pick Up Handset";
		else
			outName = "Grab Handset";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}
}
