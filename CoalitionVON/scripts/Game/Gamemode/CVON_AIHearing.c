//AI hearing players talk (server setting m_fAIHearing, off by default). Each player's game tells the server
//when they start and stop talking (SCR_PlayerController.SetSpeakingForAI). Every half second, enemy AI within
//a talking player's voice range may hear them: the closer, the likelier, and less likely through walls. An AI
//that hears someone turns to look toward the voice, and its group learns roughly where they are, the same way
//it learns of a gunshot it heard but didn't see. Server only; owned by CVON_VONGameModeComponent.
class CVON_AIHearing
{
	static const int TICK_MS = 500;
	static const float HEARD_COOLDOWN = 15;		// seconds before the same AI can hear the same player again
	static const float OCCLUDED_FACTOR = 0.35;	// chance multiplier with something solid between mouth and ear, at strength near 0
	static const float LOOK_DURATION = 2;		// seconds the AI keeps looking toward the voice

	protected float m_fStrength;
	protected bool m_bTicking;
	protected ref set<int> m_aSpeakers = new set<int>();
	// Per talking player: when each AI last heard them (world time, seconds)
	protected ref map<int, ref map<IEntity, float>> m_mLastHeard = new map<int, ref map<IEntity, float>>();
	protected ref array<IEntity> m_aNearby = {};
	protected ref TraceParam m_Trace = new TraceParam();
	protected ref array<IEntity> m_aTraceExclude = {};

	//0 = off, 1 = AI in voice range hear you almost at once, walls or not
	//==========================================================================================================================================================================
	void SetStrength(float strength)
	{
		m_fStrength = strength;
		if (m_fStrength > 0)
			StartTicking();
	}

	//==========================================================================================================================================================================
	void SetSpeaking(int playerId, bool speaking)
	{
		if (speaking)
		{
			m_aSpeakers.Insert(playerId);
			StartTicking();
		}
		else
		{
			m_aSpeakers.RemoveItem(playerId);
		}
	}

	//==========================================================================================================================================================================
	void OnPlayerLeft(int playerId)
	{
		m_aSpeakers.RemoveItem(playerId);
		m_mLastHeard.Remove(playerId);
	}

	//==========================================================================================================================================================================
	protected void StartTicking()
	{
		if (m_bTicking || m_fStrength <= 0 || m_aSpeakers.IsEmpty())
			return;
		m_bTicking = true;
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS);
	}

	//Runs while anyone is talking; stops by itself when nobody is
	//==========================================================================================================================================================================
	protected void Tick()
	{
		if (m_fStrength <= 0 || m_aSpeakers.IsEmpty())
		{
			m_bTicking = false;
			return;
		}
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS);

		CVON_VONGameModeComponent gameMode = CVON_VONGameModeComponent.GetInstance();
		BaseWorld world = GetGame().GetWorld();
		if (!gameMode || !world)
			return;

		float now = world.GetWorldTime() / 1000;
		PlayerManager playerManager = GetGame().GetPlayerManager();
		for (int i = 0; i < m_aSpeakers.Count(); i++)
		{
			int playerId = m_aSpeakers.Get(i);
			ChimeraCharacter speaker = ChimeraCharacter.Cast(playerManager.GetPlayerControlledEntity(playerId));
			if (!speaker || !IsAlive(speaker))
				continue;
			HearSpeaker(playerId, speaker, gameMode.GetPlayerVolume(playerId), now);
		}
	}

	//Enemy AI within the speaker's voice range, each with a chance to hear them
	//==========================================================================================================================================================================
	protected void HearSpeaker(int playerId, ChimeraCharacter speaker, float range, float now)
	{
		if (range <= 0)
			return;
		Faction speakerFaction = SCR_AIFactionHandling.GetEntityPerceivedFaction(speaker);
		if (!speakerFaction)
			return;

		vector mouth = speaker.EyePosition();
		m_aNearby.Clear();
		speaker.GetWorld().QueryEntitiesBySphere(mouth, range, AddNearbyCharacter, null, EQueryEntitiesFlags.DYNAMIC);
		if (m_aNearby.IsEmpty())
			return;

		map<IEntity, float> lastHeard = m_mLastHeard.Get(playerId);
		if (!lastHeard)
		{
			lastHeard = new map<IEntity, float>();
			m_mLastHeard.Insert(playerId, lastHeard);
		}

		foreach (IEntity entity: m_aNearby)
		{
			ChimeraCharacter listener = ChimeraCharacter.Cast(entity);
			if (!listener || listener == speaker)
				continue;

			// Players' characters have their AI switched off
			AIControlComponent control = AIControlComponent.Cast(listener.FindComponent(AIControlComponent));
			if (!control || !control.IsAIActivated())
				continue;
			SCR_ChimeraAIAgent agent = SCR_ChimeraAIAgent.Cast(control.GetControlAIAgent());
			if (!agent || !agent.IsEnemy(speakerFaction) || !IsAlive(listener))
				continue;

			float heardAt;
			if (lastHeard.Find(listener, heardAt) && now - heardAt < HEARD_COOLDOWN)
				continue;

			vector ear = listener.EyePosition();
			float ratio = vector.Distance(mouth, ear) / range;
			if (ratio >= 1)
				continue;

			// Strength is the chance up close. Toward the edge of the voice's range it fades, less so the higher
			// the strength: at 1 the edge is as certain as up close
			float chance = m_fStrength * (1 - (1 - m_fStrength) * ratio * ratio);
			// Walls cut the chance less the higher the strength, and not at all at 1
			float occluded = OCCLUDED_FACTOR + (1 - OCCLUDED_FACTOR) * m_fStrength;
			if (occluded < 1 && IsBlocked(mouth, ear, speaker, listener))
				chance *= occluded;
			if (Math.RandomFloat01() >= chance)
				continue;

			lastHeard.Set(listener, now);
			OnHeard(agent, speaker, speakerFaction, mouth);
		}

		// Forget AI that heard them long enough ago, or are gone
		if (lastHeard.Count() > 32)
		{
			for (int heardIndex = lastHeard.Count() - 1; heardIndex >= 0; heardIndex--)
			{
				if (!lastHeard.GetKey(heardIndex) || now - lastHeard.GetElement(heardIndex) >= HEARD_COOLDOWN)
					lastHeard.RemoveElement(heardIndex);
			}
		}
	}

	//==========================================================================================================================================================================
	protected bool AddNearbyCharacter(IEntity entity)
	{
		if (ChimeraCharacter.Cast(entity))
			m_aNearby.Insert(entity);
		return true;
	}

	//Something solid (a wall, a vehicle hull, the ground) between mouth and ear
	//==========================================================================================================================================================================
	protected bool IsBlocked(vector from, vector to, IEntity speaker, IEntity listener)
	{
		m_aTraceExclude.Clear();
		m_aTraceExclude.Insert(speaker);
		m_aTraceExclude.Insert(listener);
		m_Trace.Start = from;
		m_Trace.End = to;
		m_Trace.Exclude = null;
		m_Trace.ExcludeArray = m_aTraceExclude;
		m_Trace.Flags = TraceFlags.DEFAULT | TraceFlags.ANY_CONTACT;
		m_Trace.LayerMask = EPhysicsLayerDefs.Projectile;
		m_Trace.TraceEnt = null;
		return GetGame().GetWorld().TraceMove(m_Trace, null) < 1;
	}

	//The AI turns toward the voice, and its group knows an enemy was heard there
	//==========================================================================================================================================================================
	protected void OnHeard(SCR_ChimeraAIAgent agent, ChimeraCharacter speaker, Faction speakerFaction, vector mouth)
	{
		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
		if (utility && utility.m_LookAction)
			utility.m_LookAction.LookAt(mouth, SCR_AILookAction.PRIO_DANGER_EVENT, LOOK_DURATION);

		AIGroup group = agent.GetParentGroup();
		PerceptionManager perception = GetGame().GetPerceptionManager();
		if (!group || !perception)
			return;
		SCR_AIGroupUtilityComponent groupUtility = SCR_AIGroupUtilityComponent.Cast(group.FindComponent(SCR_AIGroupUtilityComponent));
		if (groupUtility && groupUtility.m_Perception)
			groupUtility.m_Perception.AddOrUpdateGunshot(speaker, speaker.GetOrigin(), speakerFaction, perception.GetTime(), false);
	}

	//==========================================================================================================================================================================
	protected bool IsAlive(ChimeraCharacter character)
	{
		CharacterControllerComponent controller = character.GetCharacterController();
		return controller && controller.GetLifeState() == ECharacterLifeState.ALIVE;
	}
}
