modded class SCR_CharacterInventoryStorageComponent
{
	
	//These just handle adding and removing radios from the radio array in the player controller so we know what each keybind talks to
	//==========================================================================================================================================================================
	override void HandleOnItemAddedToInventory( IEntity item, BaseInventoryStorageComponent storageOwner )
	{
		super.HandleOnItemAddedToInventory(item, storageOwner);
		#ifdef WORKBENCH
		#else
		if (Replication.IsServer())
			return;
		#endif
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		
		if (!item)
			return;
		
		if (!item.FindComponent(CVON_RadioComponent))
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
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
		
		if (!item.FindComponent(CVON_RadioComponent))
			return;
		
		CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		if (!radioComp)
			return;
		
		radioComp.WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;
		pc.m_aRadios.RemoveItemOrdered(item);
	}
}