[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class CVON_EnableBabbel : SCR_BaseEditorAttribute
{	
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(item);
		if (!gamemode)
			return null;
		
		return SCR_BaseEditorAttributeVar.CreateBool(gamemode.IsBabbelEnabled());
	}
	
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var) 
			return;
		
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(item);
		if (!gamemode)
			return;
		
		gamemode.SetBabbelEnabled(var.GetBool());
	}
}

modded class SCR_BaseGameMode 
{
	[RplProp()]
	protected bool m_bIsBabbelEnabled = true;
	
	
	override void EOnInit(IEntity owner) 
	{
		super.EOnInit(owner);
		
		if (!Replication.IsServer())
			return;
		
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		
		m_bIsBabbelEnabled = CVON_VONGameModeComponent.GetInstance().m_bUseBabbel;
		Replication.BumpMe();
	};
	
	void SetBabbelEnabled(bool input) 
	{
		m_bIsBabbelEnabled = input;
		Replication.BumpMe();
	};
	
	bool IsBabbelEnabled() 
	{
		return m_bIsBabbelEnabled;
	};
}