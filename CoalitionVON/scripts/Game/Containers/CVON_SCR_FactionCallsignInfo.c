[BaseContainerProps()]
class CVON_GroupFrequencyContainer
{
	[Attribute()] ref array<string> m_aGroupNames;
	[Attribute()] ref array<string> m_aSRFrequencies;
	[Attribute()] ref array<string> m_aLRFrequencies;
}

[BaseContainerProps()]
modded class SCR_FactionCallsignInfo
{
	[Attribute()] ref array<ref CVON_GroupFrequencyContainer> m_aGroupFrequencyOverrides;

	[Attribute("", desc: "Frequency this faction's vehicle radios start on, and return to after a key fill. Empty: the frequency config's vehicle frequency.")]
	string m_sVehicleFrequency;
}