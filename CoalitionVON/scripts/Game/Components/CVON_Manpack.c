//Manpack radios: a long-range radio carried by someone wearing a radio backpack. Anyone standing next to
//them can grab its handset and talk and listen on it (the "Grab Handset" action on their back), and the
//carrier can turn its speaker on so everyone nearby hears the net (the Toggle Radio Speaker keybind).
//The CRF kits give a radio operator a radio backpack (e.g. CRF_Radio_R107M_Inert) plus a long-range CVON
//radio item; together they make the manpack. CVON_VONGameModeComponent.m_bManpackNeedsRadioBackpack off
//makes any carried long-range radio count.
//==========================================================================================================================================================================
class CVON_Manpack
{
	//How far the holder of a handset can be from the radio's carrier before the cord pulls it out of their hand
	static const float HANDSET_CORD_LENGTH = 3.0;

	//Wearing a radio backpack: vanilla's own test (a backpack with a radio component; the CRF's inert radio
	//backpacks only disable theirs)
	static bool IsWearingRadioBackpack(IEntity character)
	{
		if (!character)
			return false;

		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(character.FindComponent(EquipedLoadoutStorageComponent));
		if (!loadout)
			return false;

		IEntity backpack = loadout.GetClothFromArea(LoadoutBackpackArea);
		if (!backpack)
			return false;

		if (backpack.FindComponent(SCR_RadioComponent))
			return true;
		if (backpack.FindComponent(BaseRadioComponent))
			return true;
		if (backpack.FindComponent(CVON_RadioComponent))
			return true;
		return false;
	}

	static bool IsManpackRadio(CVON_RadioComponent radio)
	{
		if (!radio || CVON_VehicleRadioComponent.Cast(radio))
			return false;

		if (radio.m_eRadioType != CVON_ERadioType.LONG)
			return false;

		IEntity carrier = radio.GetCarrier();
		if (!ChimeraCharacter.Cast(carrier))
			return false;

		CVON_VONGameModeComponent gamemode = CVON_VONGameModeComponent.GetInstance();
		if (gamemode && !gamemode.m_bManpackNeedsRadioBackpack)
			return true;

		return IsWearingRadioBackpack(carrier);
	}

	//The manpack radio a character carries, or null. Walks the character's entity hierarchy rather than their
	//inventory manager so it also works on other clients' copies of the character (radios always replicate).
	static CVON_RadioComponent GetManpackRadio(IEntity character)
	{
		if (!character)
			return null;

		return FindManpackIn(character, 0);
	}

	protected static CVON_RadioComponent FindManpackIn(IEntity entity, int depth)
	{
		IEntity child = entity.GetChildren();
		while (child)
		{
			CVON_RadioComponent radio = CVON_RadioComponent.Cast(child.FindComponent(CVON_RadioComponent));
			if (radio && IsManpackRadio(radio))
				return radio;

			//Items sit inside clothing and backpacks; a few levels is plenty
			if (depth < 4)
			{
				CVON_RadioComponent nested = FindManpackIn(child, depth + 1);
				if (nested)
					return nested;
			}
			child = child.GetSibling();
		}
		return null;
	}
}
