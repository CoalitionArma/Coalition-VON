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
	
	//PlayerId
	int m_iPlayerId;
	
	bool m_bIsSpectator = false;
	
	int m_iTimeDeviation;
	
	// Values processed by the client itself
	//The distance from the receiver to the sender
	float m_fDistanceToSender;
	
	//How strong the connection quality is
	float m_fConnectionQuality;
	
	//Where the source of this entry is if we can find it.
	IEntity m_SoundSource;
	
	//Is there any muffling
	int m_iLoweredDecibles;

	// Vehicle comms, worked out by the listener (not replicated)
	// The talker is on the same vehicle intercom as us: written as INTERCOM whatever m_eVonType is
	bool m_bOnIntercom = false;
	// SPEAKER: the vehicle whose speaker plays this transmission, and how far it carries
	RplId m_SpeakerRplId = RplId.Invalid();
	int m_iSpeakerRange;
	IEntity m_SpeakerEntity;

	// Last values written to VONData.json.
	// WriteJSON compares current computed values against these to skip
	// disk writes on frames where nothing has actually changed.
	float m_fCachedLeft 	= -999;
	float m_fCachedRight 	= -999;
	float m_fCachedBehind 	= -999;
	int   m_iCachedDecibels = -999;
	float m_fCachedConnQ 	= -999;
	bool  m_bCachedSameLang = true;
	string m_sCachedFreq    = "__unset__";
	int   m_iCachedClientId = 0;
	int   m_iCachedType     = -1; // VONType as written: m_eVonType, or INTERCOM

	// Update scheduling (see SCR_VONController): when this sender's muffling traces are next due.
	// Occlusion changes slowly, so it runs a few times a second rather than every frame.
	float m_fNextOcclusionTime = 0;

	static bool Extract(CVON_VONContainer instance, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{	
		snapshot.SerializeBytes(instance.m_eVonType, 4);
		snapshot.SerializeBytes(instance.m_SenderRplId, 4);
		snapshot.SerializeString(instance.m_sFrequency);
		snapshot.SerializeBytes(instance.m_iVolume, 4);
		snapshot.SerializeBytes(instance.m_iClientId, 4);
		snapshot.SerializeString(instance.m_sFactionKey);
		snapshot.SerializeBytes(instance.m_iPlayerId, 4);
		snapshot.SerializeBytes(instance.m_iTimeDeviation, 4);
		
		return true;
	}
	
	static bool Inject(SSnapSerializerBase snapshot, ScriptCtx ctx, CVON_VONContainer instance)
	{
		snapshot.SerializeBytes(instance.m_eVonType, 4);
		snapshot.SerializeBytes(instance.m_SenderRplId, 4);
		snapshot.SerializeString(instance.m_sFrequency);
		snapshot.SerializeBytes(instance.m_iVolume, 4);
		snapshot.SerializeBytes(instance.m_iClientId, 4);
		snapshot.SerializeString(instance.m_sFactionKey);
		snapshot.SerializeBytes(instance.m_iPlayerId, 4);
		snapshot.SerializeBytes(instance.m_iTimeDeviation, 4);
		
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
		snapshot.EncodeInt(packet);
		snapshot.EncodeInt(packet);
	}
	
	static bool Decode(ScriptBitSerializer packet, ScriptCtx ctx, SSnapSerializerBase snapshot)
	{
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeString(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeString(packet);
		snapshot.DecodeInt(packet);
		snapshot.DecodeInt(packet);
		
		return true;
	}
	
	static bool SnapCompare(SSnapSerializerBase lhs, SSnapSerializerBase rhs, ScriptCtx ctx)
	{
		return lhs.CompareSnapshots(rhs, 4) 
		&& lhs.CompareSnapshots(rhs, 4) 
		&& lhs.CompareStringSnapshots(rhs) 
		&& lhs.CompareSnapshots(rhs, 4)
		&& lhs.CompareSnapshots(rhs, 4)
		&& lhs.CompareStringSnapshots(rhs)
		&& lhs.CompareSnapshots(rhs, 4)
		&& lhs.CompareSnapshots(rhs, 4);
	}
	
	static bool PropCompare(CVON_VONContainer instance, SSnapSerializerBase snapshot, ScriptCtx ctx)
	{
		return snapshot.Compare(instance.m_eVonType, 4)
		&& snapshot.Compare(instance.m_SenderRplId, 4)
		&& snapshot.CompareString(instance.m_sFrequency)
		&& snapshot.Compare(instance.m_iVolume, 4)
		&& snapshot.Compare(instance.m_iClientId, 4)
		&& snapshot.CompareString(instance.m_sFactionKey)
		&& snapshot.Compare(instance.m_iPlayerId, 4)
		&& snapshot.Compare(instance.m_iTimeDeviation, 4);
	}
}