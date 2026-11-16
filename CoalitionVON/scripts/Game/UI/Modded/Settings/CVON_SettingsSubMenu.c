//The "Coalition VON" tab of the settings menu (UI/layouts/Menus/SettingsSubMenus/CVON_SettingsSubMenu.layout).
//Each Yes/No spin box is named after the CVON_GameSettings option it shows. The tab drives them itself
//rather than through vanilla's settings bindings, as the options live in CVON's own file (CVON_GameSettings):
//a change is saved straight away.
//A setting the server enforces (CVON_ServerSettings) shows the server's value, greyed out, and the player's
//own choice is kept for servers that leave it to them.
class CVON_SettingsSubMenu : SCR_SettingsSubMenuBase
{
	protected static const string HINT_WIDGET = "RadioGameAudioHint";
	protected static const string SQUELCH_HINT_WIDGET = "SquelchTonesHint";

	protected SCR_SpinBoxComponent m_GameAudioSpin;
	protected SCR_SpinBoxComponent m_SquelchSpin;

	//==========================================================================================================================================================================
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		m_aSettingsBindings.Clear();

		CVON_ESettingEnforcement enforcement = CVON_GameSettings.GetRadioGameAudioEnforcement();
		if (enforcement == CVON_ESettingEnforcement.PLAYER_CHOICE)
		{
			m_GameAudioSpin = BindOption(CVON_GameSettings.RADIO_GAME_AUDIO, CVON_GameSettings.GetPlayerRadioGameAudio());
			if (m_GameAudioSpin)
				m_GameAudioSpin.m_OnChanged.Insert(OnGameAudioChanged);
		}
		else
		{
			ShowEnforced(CVON_GameSettings.RADIO_GAME_AUDIO, HINT_WIDGET, enforcement == CVON_ESettingEnforcement.FORCE_ON);
		}

		//Squelch: the player's choice, unless the server has turned it off for everyone
		if (CVON_GameSettings.IsServerSquelchOff())
		{
			ShowEnforced(CVON_GameSettings.SQUELCH_TONES, SQUELCH_HINT_WIDGET, false);
		}
		else
		{
			m_SquelchSpin = BindOption(CVON_GameSettings.SQUELCH_TONES, CVON_GameSettings.GetPlayerSquelchTones());
			if (m_SquelchSpin)
				m_SquelchSpin.m_OnChanged.Insert(OnSquelchChanged);
		}
	}

	//Shows the player's value in a Yes/No spin box; the caller listens for changes
	//==========================================================================================================================================================================
	protected SCR_SpinBoxComponent BindOption(string widgetName, bool value)
	{
		Widget widget = GetRootWidget().FindAnyWidget(widgetName);
		if (!widget)
		{
			Print("[CVON] Settings tab: no widget " + widgetName, LogLevel.WARNING);
			return null;
		}

		SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(widget.FindHandler(SCR_SpinBoxComponent));
		if (!spin)
		{
			Print("[CVON] Settings tab: " + widgetName + " is not a spin box", LogLevel.WARNING);
			return null;
		}

		spin.SetCurrentItem(YesNoItem(value), false, false, false);
		return spin;
	}

	protected int YesNoItem(bool value)
	{
		if (value)
			return 1; //"Yes"
		return 0; //"No"
	}

	//==========================================================================================================================================================================
	protected void OnGameAudioChanged(SCR_WLibComponentBase comp, int item)
	{
		CVON_GameSettings.SetPlayerRadioGameAudio(item == 1);
	}

	protected void OnSquelchChanged(SCR_WLibComponentBase comp, int item)
	{
		CVON_GameSettings.SetPlayerSquelchTones(item == 1);
	}

	//==========================================================================================================================================================================
	override void OnTabRemove()
	{
		if (m_GameAudioSpin)
			m_GameAudioSpin.m_OnChanged.Remove(OnGameAudioChanged);
		if (m_SquelchSpin)
			m_SquelchSpin.m_OnChanged.Remove(OnSquelchChanged);
		super.OnTabRemove();
	}

	//Shows the server's value in a Yes/No spin box, greyed out, so the player's own value isn't overwritten
	//==========================================================================================================================================================================
	protected void ShowEnforced(string widgetName, string hintWidgetName, bool value)
	{
		Widget widget = GetRootWidget().FindAnyWidget(widgetName);
		if (widget)
		{
			SCR_SpinBoxComponent spin = SCR_SpinBoxComponent.Cast(widget.FindHandler(SCR_SpinBoxComponent));
			if (spin)
				spin.SetCurrentItem(YesNoItem(value), false, false, false);
			widget.SetEnabled(false);
		}

		TextWidget hint = TextWidget.Cast(GetRootWidget().FindAnyWidget(hintWidgetName));
		if (hint)
		{
			string state = "off";
			if (value)
				state = "on";
			hint.SetText(string.Format("Set by the server: %1 for everyone. %2", state, hint.GetText()));
		}
	}
}
