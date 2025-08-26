class CVON_VONContainer
{
	//==========================================================================================================================================================================
	//Some of this is stored locally on the client, and some is gather from the sender.if
	//This all gets written into a JSON file in CVON_SCR_VONController
	//Theres a more indepth explanatation there of what these values mean and how it works.
	//==========================================================================================================================================================================
	
	
	//Server values sent initially
	//If its a radio or direct VONEntry
	CVON_EVONType m_eVonType;
	
	//Who sent this container
	RplId m_SenderRplId;
	
	//The frequency this entry is on
	string m_sFrequency;
	
	//How loud their voice is
	int m_iVolume;
	
	//Their teamspeak client Id
	int m_iClientId;
	
	//Faction key for the gamemode check
	string m_sFactionKey;
	
	//RplId of their radio
	int m_iRadioId;
	
	//Sender location for distant radio calls
	vector m_vSenderLocation;
	
	//The max distance we can send a radio signal
	int m_iMaxDistance;
	
	
	
	// Values processed by the client itself
	//The distance from the receiver to the sender
	float m_fDistanceToSender;
	
	//How strong the connection quality is
	float m_fConnectionQuality;
	
	//Where the source of this entry is if we can find it.
	IEntity m_SoundSource;
	
	static bool Extract(CVON_VONContainer instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{	
		snapshot.SerializeInt(instance.m_eVonType);
		snapshot.SerializeInt(instance.m_SenderRplId);
		snapshot.SerializeString(instance.m_sFrequency);
		snapshot.SerializeInt(instance.m_iVolume);
		snapshot.SerializeInt(instance.m_iClientId);
		snapshot.SerializeString(instance.m_sFactionKey);
		
		return true;
	}
	
	static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, CVON_VONContainer instance)
	{
		snapshot.SerializeInt(instance.m_eVonType);
		snapshot.SerializeInt(instance.m_SenderRplId);
		snapshot.SerializeString(instance.m_sFrequency);
		snapshot.SerializeInt(instance.m_iVolume);
		snapshot.SerializeInt(instance.m_iClientId);
		snapshot.SerializeString(instance.m_sFactionKey);
		
		return true;
	}
	
	static void Encode(SSnapSerializerBase snapshot, ScriptCtx ctx, ScriptBitSerializer packet)
	{
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeString(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
		snapshot.EncodeString(packet);
	}
	
	static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeString(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeString(packet);
		
		return true;
	}
	
	static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
	{
		return lhs.CompareSnapshots(rhs, 4) 
		&& lhs.CompareSnapshots(rhs, 4) 
		&& lhs.CompareStringSnapshots(rhs) 
		&& lhs.CompareSnapshots(rhs, 4)
		&& lhs.CompareSnapshots(rhs, 4)
		&& lhs.CompareStringSnapshots(rhs);
	}
	
	static bool PropCompare(CVON_VONContainer instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
	{
		return snapshot.CompareInt(instance.m_eVonType)
		&& snapshot.CompareInt(instance.m_SenderRplId)
		&& snapshot.CompareString(instance.m_sFrequency)
		&& snapshot.CompareInt(instance.m_iVolume)
		&& snapshot.CompareInt(instance.m_iClientId)
		&& snapshot.CompareString(instance.m_sFactionKey);
	}
}