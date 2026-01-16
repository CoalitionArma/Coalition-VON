[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class CVON_EnableEspionage : SCR_BaseEditorAttribute
{	
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(item);
		if (!gamemode)
			return null;
		
		return SCR_BaseEditorAttributeVar.CreateBool(gamemode.IsEncryptionEnabled());
	}
	
	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var) 
			return;
		
		SCR_BaseGameMode gamemode = SCR_BaseGameMode.Cast(item);
		if (!gamemode)
			return;
		
		gamemode.SetEspionageEnabled(var.GetBool());
	}
}

modded class SCR_BaseGameMode 
{
	[RplProp()]
	protected bool m_bIsEncryptionEnabled = true;
	
	
	override void EOnInit(IEntity owner) 
	{
		super.EOnInit(owner);
		
		if (!Replication.IsServer())
			return;
		
		if (!CVON_VONGameModeComponent.GetInstance())
			return;
		
		m_bIsEncryptionEnabled = CVON_VONGameModeComponent.GetInstance().m_bUseFactionEcncryption;
		Replication.BumpMe();
	};
	
	void SetEspionageEnabled(bool input) 
	{
		m_bIsEncryptionEnabled = input;
		Replication.BumpMe();
	};
	
	bool IsEncryptionEnabled() 
	{
		return m_bIsEncryptionEnabled;
	};
}