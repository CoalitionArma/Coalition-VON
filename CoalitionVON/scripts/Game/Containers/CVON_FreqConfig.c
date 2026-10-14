[BaseContainerProps(configRoot: true)]
class CVON_FreqConfig
{
	[Attribute()] ref array<ref CVON_GroupFrequencyContainer> m_aPresetGroupFrequencyContainers;

	[Attribute("", desc: "Frequency vehicle radios start on, and return to after a key fill. Empty: the first long-range frequency above. A faction's own (SCR_FactionCallsignInfo) or a vehicle's (CVON_VehicleRadioComponent) takes priority.")]
	string m_sVehicleFrequency;
}