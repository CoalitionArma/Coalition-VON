modded class SCR_CharacterInventoryStorageComponent
{

	//These just handle adding and removing radios from the radio array in the player controller so we know what each keybind talks to
	//==========================================================================================================================================================================
	override void HandleOnItemAddedToInventory( IEntity item, BaseInventoryStorageComponent storageOwner )
	{
		super.HandleOnItemAddedToInventory(item, storageOwner);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;

		if (!item)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;

		//Authority: give the radio its faction and channels as soon as it's on a character (the radio
		//component only polls for this as a fallback now)
		if (Replication.IsServer() && radioComp.m_sFactionKey == "")
			radioComp.InitializeRadios();

		#ifdef WORKBENCH
		#else
		if (Replication.IsServer())
			return;
		#endif

		//Only our own character's radios belong in our radio list and RadioData.json
		if (GetOwner() != SCR_PlayerController.GetLocalControlledEntity())
			return;

		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		//The vehicle radio and a held handset stay last, so inventory radios keep the same positions as in the server's list
		int insertAt = pc.GetAttachedRadiosStart();
		if (insertAt < pc.m_aRadios.Count())
			pc.m_aRadios.InsertAt(item, insertAt);
		else
			pc.m_aRadios.Insert(item);
	}

	override void HandleOnItemRemovedFromInventory( IEntity item, BaseInventoryStorageComponent storageOwner )
	{
		super.HandleOnItemRemovedFromInventory(item, storageOwner);
		#ifdef WORKBENCH
		#else
		if (Replication.IsServer())
			return;
		#endif
		if (!CVON_VONGameModeComponent.GetInstance())
			return;

		if (!item)
			return;

		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;

		//Only our own character's radios
		if (GetOwner() != SCR_PlayerController.GetLocalControlledEntity())
			return;

		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		pc.m_aRadios.RemoveItemOrdered(item);
	}
}