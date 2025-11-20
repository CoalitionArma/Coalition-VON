modded class Vehicle
{
	SCR_BaseCompartmentManagerComponent m_CompartmentMan;
	
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		m_CompartmentMan = SCR_BaseCompartmentManagerComponent.Cast(owner.FindComponent(SCR_BaseCompartmentManagerComponent));
	}
}