class CRF_AudioSpatialUtil
{
	static float Saturate(float x)
	{
		if (x < 0.0) return 0.0;
		if (x > 1.0) return 1.0;
		return x;
	}

	static float SmoothStep01(float t)
	{
		t = Saturate(t);
		return t * t * (3.0 - 2.0 * t);
	}

	// Gets a forward vector from the active camera world transform
	static vector GetCameraForward()
	{
		CameraBase cam = GetGame().GetCameraManager().CurrentCamera();
		if (!cam)
			return "0 0 1";

		vector tm[4];
		cam.GetWorldTransform(tm);

		// Typical Enfusion convention:
		// tm[3] = position
		// tm[2] = forward
		// If this feels wrong in testing, try tm[1] or negate once.
		vector fwd = tm[2];
		fwd.Normalize();
		return fwd;
	}

	static float ComputeBehindIntensity(vector listenerPosWS, vector speakerPosWS, float startRamp = 0.15)
	{
		vector toSpeaker = speakerPosWS - listenerPosWS;
		if (toSpeaker.LengthSq() < 0.01)
			return 0.0;

		toSpeaker.Normalize();

		vector forward = GetCameraForward();

		// +1 front, 0 side, -1 behind
		float frontDot = vector.Dot(forward, toSpeaker);

		float behindRaw = (-frontDot - startRamp) / (1.0 - startRamp);
		behindRaw = Saturate(behindRaw);

		return SmoothStep01(behindRaw);
	}
}
