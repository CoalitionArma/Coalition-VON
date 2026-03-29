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
		
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!pc)
			return;
		
		// Insert maintaining [SR..., LR...] order so m_aRadios[0] is always the SR radio,
		// matching the sort done in InitializeRadios. Without this, if the LR radio's
		// inventory event arrives before the SR radio's (common due to replication timing),
		// the array ends up [LR, SR] and Caps Lock transmits on the wrong radio until the
		// player rotates with VONRotateActive.
		CVON_RadioComponent newRadioComp = CVON_RadioComponent.Cast(item.FindComponent(CVON_RadioComponent));
		if (newRadioComp && newRadioComp.m_eRadioType == CVON_ERadioType.SHORT)
		{
			int insertIdx = 0;
			for (int i = 0; i < pc.m_aRadios.Count(); i++)
			{
				CVON_RadioComponent rc = CVON_RadioComponent.Cast(pc.m_aRadios[i].FindComponent(CVON_RadioComponent));
				if (rc && rc.m_eRadioType == CVON_ERadioType.LONG)
					break;
				insertIdx++;
			}
			pc.m_aRadios.InsertAt(item, insertIdx);
		}
		else
			pc.m_aRadios.Insert(item);
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
		
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!pc)
			return;
		pc.m_aRadios.RemoveItemOrdered(item);
	}
}