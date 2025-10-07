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
		
		if (!item.FindComponent(CVON_RadioComponent))
			return;
		
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(storageOwner.GetOwner().GetRootParent());
		if (playerId <= 0)
			return;
		
		if (GetGame().GetPlayerController())
			CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent)).WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		else
		{
			CVON_RadioComponent radioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
			radioComp.InitializeRadios();
		}
		
		SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).m_aRadios.Insert(item);
	}
	
	override void HandleOnItemRemovedFromInventory( IEntity item, BaseInventoryStorageComponent storageOwner )
	{
		super.HandleOnItemRemovedFromInventory(item, storageOwner);
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		
		if (!item)
			return;
		
		if (!item.FindComponent(CVON_RadioComponent))
			return;
		
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(storageOwner.GetOwner().GetRootParent());
		if (playerId <= 0)
			return;
		
		if (GetGame().GetPlayerController())
			CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent)).WriteJSON(SCR_PlayerController.GetLocalControlledEntity());
		SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId)).m_aRadios.RemoveItemOrdered(item);
	}
}