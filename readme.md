# Coalition TeamSpeak Plugin – Server Admin Quickstart Guide

Arma Reforger writes a `VONServerSettings.json` file that tells the Coalition TeamSpeak Plugin how to connect and synchronize with your TeamSpeak server. This file is the single source of truth for connection details and channel management.

## Example

{
  "Server Settings": {
    "VONChannelName": "Reforger In-game (CRF VON)",
    "VONChannelPassword": "password",
    "TeamspeakServerIP": "ts.coalitiongroup.net",
    "TeamspeakServerPassword": ""
  }
}

## Fields
- VONChannelName
Type: string
The name of the TeamSpeak channel used for in-game voice. The plugin will attempt to move players into this channel automatically when InGame = true.

- VONChannelPassword
Type: string (optional)
Password for the VON channel. If left empty (""), the plugin assumes the channel is not password-protected. If set, the plugin automatically applies this password when moving clients.

- TeamspeakServerIP
Type: string
The TeamSpeak server address or IP. Can be a hostname (ts.coalitiongroup.net) or an IP address (192.168.1.10). Used by the plugin to auto-connect when the game session starts.

- TeamspeakServerPassword
Type: string (optional)
The server password, if your TeamSpeak server requires one. Leave empty if your server does not use a global password.

## Behavior
- Game Start
Arma generates VONServerSettings.json.
The plugin detects the file and reads the server + channel info.

- TeamSpeak Auto-Connect
If the current TeamSpeak tab is not connected to the given IP, the plugin will auto-connect.
If a password is provided, it will be used.

- Channel Auto-Move
Once connected, the plugin moves the client into the VONChannelName channel.
If a channel password is set, it is applied automatically.

- In-Game Sync
While InGame = true, players remain in the VON channel.
When leaving the game, the plugin returns the player to their previous channel.

## Notes for Admins

- Always ensure the VONChannelName matches exactly with the TeamSpeak channel name.

- Channel passwords must be kept consistent between TeamSpeak and this file.

- If the TeamSpeak server changes IP or domain, update TeamspeakServerIP.

- This file is game-generated. Manual edits should only be made if troubleshooting or testing.

- Any changes to Server Settings only take effect after a server restart.

# CVON server settings (server config)

Server admins set CVON's gameplay settings in the Arma Reforger server config, the same way as ACE: in the `missionHeader` object under `game.gameProperties`. Next to ACE's `m_ACE_Settings`, add `m_CVON_Settings`:

```json
"missionHeader": {
    "m_CVON_Settings": {
        "m_eRadioGameAudio": 0,
        "m_bRadioVoicePickup": false,
        "m_eSignalModel": 0,
        "m_bTerrainInterference": true,
        "m_fTerrainInterferenceStrength": 0.5,
        "m_eRadioDuplex": 0,
        "m_bSquelchTones": true,
        "m_bTransmissionCutOff": true,
        "m_bHandheldSpeakers": true,
        "m_bDroppedRadiosTransmit": true,
        "m_fAIHearing": 0.0
    }
}
```

| Field | Type, default | What it does |
|---|---|---|
| `m_eRadioGameAudio` | int, `0` | Mixing game audio into radio transmissions. `0`: each player's own setting (settings menu, **Coalition VON** tab). `1`: on for everyone. `2`: off for everyone. Players see the server's choice, greyed out, on that tab. |
| `m_bRadioVoicePickup` | bool, `false` | A transmitting radio's mic picks up other players talking near the sender: the net hears them in the background, quieter the further they are from the sender and the softer they talk. Nobody hears their own voice back. Needs plugin 3.2.0 or later on the listeners' side. |
| `m_eSignalModel` | int, `0` | How radio signal quality is worked out (see "Signal models"). `0`: arcade, a quality curve over each radio's range with a terrain penalty. `1`: realistic, transmit power against path loss and terrain diffraction; out of reach means no signal at all. |
| `m_bTerrainInterference` | bool, `true` | Hills and mountains between sender and receiver weaken radio signals. |
| `m_fTerrainInterferenceStrength` | float 0–1, `0.5` | How much terrain weakens them: `0` none, `0.5` normal, `1` strongest (double the normal loss, and smaller hills count). |
| `m_eRadioDuplex` | int, `0` | `0`: half duplex. One transmission at a time: a radio that's receiving someone won't transmit (the player hears an error tone and presses again once the channel is clear), and while a player transmits they don't hear anything else on that frequency. People standing next to them are still heard directly. `1`: full duplex. Players can transmit over each other and hear the frequency while they transmit. |
| `m_bSquelchTones` | bool, `true` | Listeners hear a squelch burst when a transmission opens and a squelch tail when it closes. |
| `m_bTransmissionCutOff` | bool, `true` | A radio operator killed or knocked out mid-transmission is cut off with a burst of static. |
| `m_bHandheldSpeakers` | bool, `true` | Players can put a handheld radio they carry on speaker ("Toggle Radio Speaker" keybind), so people around them hear its traffic. Manpack and vehicle speakers work either way. |
| `m_bDroppedRadiosTransmit` | bool, `true` | A radio dropped on the ground, while switched on, keeps playing its traffic out loud for anyone nearby. |
| `m_fAIHearing` | float 0–1, `0.0` | Enemy AI can hear players talking. `0`: off. `1`: any enemy AI within your voice range hears you almost as soon as you speak, walls or not. In between, AI close by are likelier to hear you than AI near the edge of your voice range, and walls make it less likely; both matter less the higher the value. AI that hear you turn toward you, and their group knows roughly where you are. It covers all talking, including radio calls, since those come out of your mouth too. Has no effect in PvP. |

- **One source at a time:** CVON reads the whole object from one place, as ACE does. A field left out of the server config takes the default above, not the mission's value. Without `m_CVON_Settings` in the server config, the mission's own settings apply (`m_MissionSettings` on the world's `CVON_VONGameModeComponent`), and the defaults apply without either.
- **Read once:** the server reads it from the first mission it loads, as ACE does. Later scenario changes keep using it, because their mission headers don't carry the server config's overrides. Restart the server after changing it.
- **Clients need nothing:** the server sends the settings in force to every player.

# For developers: how the game and the plugin talk

The game and the TeamSpeak plugin exchange small files in the client's profile folder (`Documents\My Games\ArmaReforger\profile`). The plugin wakes up whenever one of them changes.

| File | Written by | Purpose |
|---|---|---|
| `VONServerData.json` | both | In-game state, TeamSpeak client ID, VON channel and server |
| `VONData.json` / `VONData.bin` | game, ~20 times a second (immediately on push-to-talk) | Everyone the player can hear: stereo gains, muffling, radio frequency and quality, `IsTransmitting`. The plugin uses whichever was written last. |
| `RadioData.json` | game, when one of the player's radios changes | The player's radios: frequency, volume, ear, encryption key |
| `VONHeartbeat.json` | game, every second | Liveness. If it stops while in game (crash), the plugin closes the mic after 3 s and leaves the VON channel after 10 s. |
| `VONPluginInfo.json` | plugin, every 5 s; deleted on exit | What the plugin supports (`bin1`, `talking1`, `heartbeat1`, `comms1`, `gamemix1`, `immersion1`, `pickup1`). The game only writes `VONData.bin` while this lists `bin1`. It only adds `RadioMix` / `GameAudio` to `VONData.json` while it lists `gamemix1`, the radio immersion keys while it lists `immersion1`, and voice pickup entries while it lists `pickup1`. |
| `VONTalking.json` | plugin, while in game | TeamSpeak client IDs currently talking; the game refreshes players who aren't talking less often. |

`VONData.bin` is the same data as `VONData.json` packed into fixed records. Its layout is documented in `load_von_bin_into` (plugin.c) and written by `WriteVONDataBinary` (CVON_SCR_VONController.c); change both together and bump the format version.

Each entry in `VONData` has a `VONType`:

| VONType | Meaning | Plugin sound |
|---|---|---|
| 0 Direct | Proximity voice | Positioned by the game's left/right gains; muffle and behind-the-head filters |
| 1 Radio | Radio transmission | Radio chain if one of the player's radios matches, otherwise direct |
| 2 Intercom | Talker on the player's vehicle intercom (the listener's game decides this) | Clear, centred, telephone band |
| 3 Speaker | Radio traffic heard through a nearby radio's speaker (a vehicle's, a carried radio on speaker, a dropped radio) | Radio chain, then positioned at that radio like direct |

Plugins before 3.0 play types 2 and 3 as direct speech.

# Game audio on radio

While a player talks on a radio, the plugin mixes what their game is playing (gunfire, explosions, vehicles, everything the game outputs) into their outgoing voice, so the net hears the sender's surroundings. Players turn it on or off with "Mix Game Audio Into Radio" on the settings menu's **Coalition VON** tab (on by default; saved as `RadioGameAudio` in `CVON_Settings.json` in the player's profile folder, see `CVON_GameSettings`). Direct speech never carries it.

- **Game → plugin:** `VONData.bin` header flags bit 1 `RadioMix` (talking on a radio with the setting on) and bit 2 `GameAudio` (the setting is on). In `VONData.json` they are the top-level `RadioMix` / `GameAudio` keys, written only for plugins that list `gamemix1`; older plugins would read any other top-level key as a player.
- **Capture** (`cvon_gameaudio.c`): Windows per-process loopback of the Arma Reforger process (`ArmaReforgerSteam*.exe`, or Workbench when playing in it), so TeamSpeak and other programs are never recorded. Needs Windows 10 version 2004 or later. Elsewhere the capture fails, the plugin retries with a growing delay, and transmissions are plain voice. The capture runs while in game with the setting on, so it is ready when the player keys up.
- **Mixing** (`ts3plugin_onEditCapturedVoiceDataEvent` in plugin.c): TeamSpeak calls it after its own noise suppression and voice detection, and only frames TeamSpeak is about to send are touched, so a muted mic or voice activation still decide what goes out. The first 250 ms after keying are skipped, so the game's own key-up beep isn't sent. The game audio then fades in, goes through a peak limiter (`GAMEMIX_CEILING`) and drops up to 6 dB while the player speaks. Levels are the `GAMEMIX_*` defines.
- **Listeners need nothing new:** the mix happens on the sender's side, and the transmission goes through each listener's radio chain as usual. Players close enough to hear the sender directly also get the game audio in their direct audio (TeamSpeak sends one stream to everyone), a little after their own game plays the same sound.

# Radio immersion

Each feature has a server setting (see "CVON server settings"). When the server turns one off, the game stops sending its data, so the plugin plays nothing for it.

- **Signal models** (`m_eSignalModel`, `LinkQuality` in CVON_SCR_VONController.c): the listener's game works out each radio link's quality (0–1, which the plugin turns into static, crackle and dropouts) one of two ways.
  - **Arcade** (default): the distance curve over the sending radio's `m_iRadioRange` (`GetSignalStrength`) times the terrain factor below. Every radio on the net is heard, however far; out of range just means maximum static.
  - **Realistic** (`GetRealisticQuality`), after ACRE2's `los_simple` link budget: received power = transmit power − path loss − terrain diffraction, in dBm.
    - Transmit power is the sending radio's `m_fTransmitPower` (5 W by default; 20 W on the AN/PRC-117 and both R-168s; 50 W on vehicle radios), sent with the transmission.
    - Path loss is the larger of free-space path loss (FSPL, `20 log f + 20 log d − 27.55`, f in MHz, d in m) and plane-earth loss (`40 log d − 20 log h_tx − 20 log h_rx`, antenna heights above the ground). Near the ground the reflection off the ground between the antennas costs far more than free space, which is what limits a handheld to some 12 km on flat ground; a helicopter high above it gets close to free space and carries much further.
    - The frequency is the net's if it's numeric (kHz, e.g. "55500"), else the receiving radio's `m_fBandMHz` (50 MHz by default), as most nets are named.
    - Terrain diffraction uses the Deygout method with the ITU-R P.526 knife-edge loss: the worst obstruction between the antennas plus the worst on each side of it, scaled by `m_fTerrainInterferenceStrength` (0.5 = as computed). ACRE2 adds a knife edge for every peak, which with antennas 1.6 m off the ground blocked nearly everything.
    - Quality is 0 at −118 dBm (the sensitivity ACRE2 gives the PRC-152 and PRC-117) and 1 from −95 dBm up. Below −118 dBm nothing is received: the radio entry loses its frequency, so the plugin only plays the sender if they're close enough to hear in person, and a speaker goes quiet.
    - The radios' `m_fInterference` (below) only applies to arcade; in the realistic model the long-range sets get their edge from power.
    - On Everon from Montignac, a PRC-152 reaches about 40 % of the island (arcade: 71 %), a PRC-117 about 57 % (arcade: all of it), with the far side of the big ridges dead.
- **Terrain** (`ComputeTerrainFactor` in CVON_SCR_VONController.c, arcade model): the listener's game samples the terrain height every 25 m along the path between the two radios, about 1.6 m above each. How far the terrain rises above that line sets how badly the path is blocked: nothing for the first 3 m (`TERRAIN_GRACE_DEPTH`, a hill that only grazes the line), fully at 40 m (`TERRAIN_FULL_BLOCK_DEPTH`). At full blockage the highest ridge costs 60 % of the signal, each further ridge 15 % and the share of the path in shadow up to 25 %. All three scale with the blockage, so a small hill between two radios costs little. The strength, twice `m_fTerrainInterferenceStrength` (so the default 0.5 is 1) times the link's radios (below), scales the loss and also the heights: at a strength of 0.5 a hill needs 6 m before it costs anything and 80 m for the full loss, at 2 (the setting at 1, handhelds) it needs 1.5 m and 20 m. So lowering it clears small hills first and softens big ones. The link's radios: each radio has `m_fInterference` on its `CVON_RadioComponent` (1 for handhelds, 0.4 on the long-range sets: the AN/PRC-117 and both R-168s), and a link uses the average of the sending radio's (sent with the transmission) and the receiving radio's (a speaker's radio for speaker traffic). Two PRC-117s get 0.4 of the terrain strength, a PRC-117 talking to a handheld 0.7. It multiplies the distance-based `ConnectionQuality`, which the plugin already turns into static. The weak-signal sound (`radio_weak_signal` in plugin.c, `STATIC_*` defines) has three parts, all driven by that quality: a steady band-limited hiss whose level follows the quality alone, so it never pumps with the audio and stays well under the voice and game audio; random crackle, rare on a good signal and up to 40 pops a second on the weakest; and below quality 0.4, dropouts where the voice cuts out and the hiss fills the gap: more often and longer the weaker the signal, up to 6 a second of up to 200 ms at the edge of range, so words go missing. The static fades in over 120 ms at the start of each transmission. It uses height lookups, not traces, and each talker is recomputed once a second or when either end moves 25 m. A helicopter or a hilltop operator clears the hills on their own.
- **Half duplex** (`IsBlockedByOwnTransmission` in CVON_SCR_VONController.c): while the player transmits on a radio, their game blanks the frequency of incoming radio entries on that frequency. The plugin then finds no radio for them and plays them as direct speech at their direct gains, so only a sender close by is heard. Speaker entries aren't affected: the vehicle's radio is a different radio. Busy channel lockout (`IsChannelBusy`): when the player keys a radio, their game looks for a radio entry on the same frequency, time deviation and key that reaches them (signal above zero after distance and terrain). If there is one, the radio doesn't key and the player hears vanilla's radio error sound; holding the key doesn't key it later, they press again. Two players keying within the same moment can both get through, since neither game has the other's transmission yet. Nothing changes in the plugin.
- **Squelch** (`update_squelch_events`, `squelch_render` in plugin.c): the plugin watches for radio talkers that one of the player's radios receives starting and stopping. It plays a short squelch burst on open and a squelch tail on close, longer and louder for weak signals, panned to that radio's ear at its volume. They're added in TeamSpeak's mixed playback callback, so a tail can play after the talker's own audio has ended. Besides the server setting, each player can turn them off for themselves with "Radio Squelch Tones" on the **Coalition VON** settings tab (on by default; `SquelchTones` in `CVON_Settings.json`); their game then writes `SquelchTones` off. When the server has squelch off, the option shows "off", greyed out.
- **Cut-off**: a sender whose character stops being alive mid-transmission sends a cut-off (`BroadcastCutOffLocalVONToServer`) instead of the normal release. The server tells each receiver (`CutOffLocalVONBroadcast`), then ends the transmission as usual, so `RemoveLocalVONBroadcasts` and anything hooked on it still run. Each receiver keeps the entry 0.5 s marked as cut off, and its plugin silences the talker and plays a burst of crackling static instead of the squelch tail.

- **AI hearing** (`CVON_AIHearing`, server only): each player's game watches the plugin's talking list, which includes the player's own TeamSpeak client, and tells the server when they start and stop talking (`SetSpeakingForAI`), only while `m_fAIHearing` is above 0. Every 0.5 s the server checks enemy AI within each talker's voice range (the same range players hear, from their voice volume setting). With `s` = `m_fAIHearing`, the chance of being heard each check is `s × (1 − (1 − s) × (distance / range)²)`, times `0.35 + 0.65 × s` with something solid in between, so at 1 it's certain anywhere in range. An AI that hears the talker looks toward them (`SCR_AILookAction`, danger priority), and its group gets them as a heard, not seen, target (`SCR_AIGroupPerception.AddOrUpdateGunshot`, not endangering). The same AI can't hear the same player again for 15 s. Players on plugins without `talking1` aren't heard by AI.
- **Voice pickup** (`m_bRadioVoicePickup`, off by default): mixing other voices into the sender's mic would make everyone standing next to the sender hear themselves echo back, since TeamSpeak sends one stream to everyone. So the sender's stream stays clean and each receiver plays the nearby players' own voice streams through the same radio instead:
  - **Server** (`CVON_VONGameModeComponent.UpdateVoicePickup`): when a transmission starts and once a second while it lasts, it lists the living players within half their own voice range of the sender, with a gain of `0.35 × (1 − distance / reach)²` rounded to 0.05. It sends each receiver that list without the receiver themselves (`SCR_PlayerController.SetVoicePickup`), only when it changed.
  - **Receiver's game** (`SCR_PlayerController.ReconcileVoicePickups`, `SCR_VONController.WriteVoicePickups`): adds a pickup entry for each listed player it doesn't already hear another way (in person, on their own transmission, on the intercom), through the loudest sender if several pick them up. The entry copies the sender's entry as written: frequency, quality, key, and for a speaker its position. It goes silent with the sender's (half duplex, cut-off, heard in person) and is left out of the file while silent, so TeamSpeak keeps that player muted. A picked-up player who walks into earshot becomes a normal direct entry.
  - **Plugin** (`pickup1`): plays a pickup through the matching radio like its sender, scaled by the pickup gain, with no static or squelch of its own (the sender's transmission carries both). It is also held at no more than half the sender's recent level on that frequency (`PICKUP_MAX_RATIO`, `pickup_cap`), so the sender is always the loudest voice on the net whatever the bystanders' mic gain; picked-up voices fade out if the sender goes quiet for more than a second or so.

Format, all in fields older plugins ignore:
- **`VONData.bin`:** header flags bit 3 `SquelchTones`; entry flags bit 3 cut off (bits 1–2 reserved); entry word e4's top byte the voice pickup gain (×255, 0 = a normal entry).
- **`VONData.json`:** top-level `SquelchTones`; per entry `CutOff`, and `PickupGain` on voice pickup entries.

# Vehicle radios and intercom

`CVON_VehicleRadioComponent` gives a vehicle a radio, a speaker and an intercom. The mod adds it to the vanilla `TrackedVehicle_Base`, `Wheeled_APC_Base`, `Helicopter_Base`, `Wheeled_Car_Base` and `Wheeled_Truck_Base` prefabs, so vehicles inheriting from those have comms; add it to any other vehicle prefab (or remove it) in Workbench. Settings on the component:

- `m_bMilitaryOnly`: comms only when the vehicle's default faction is a military one (`SCR_Faction.IsMilitary`). On for the car and truck bases, which civilian vehicles share; a vehicle with a civilian or no default faction gets nothing.

- `m_bHasRadio`, `m_sRadioName`, `m_iRadioRange`, `m_eRadioMenu`: the radio. Only the crew can use it: driver/pilot and turret seats (gunner, door gunner), plus commanders, including vanilla's commander seats that are passenger seats linked to a commander's sight (LAV-25) or labelled as the commander's. While seated it is added after their own radios, on the next free radio key; it opens and tunes like any radio. It starts on the vehicle's prefab faction key and keeps it, playing that traffic, until a crew member fills it with the "Fill Vehicle Radio With Your Encryption" keybind (Left Ctrl + Home): it then takes their faction key and the channel list of their own radio (or their faction's channels if they carry none). A radio with no key at all (a vehicle without a faction) is filled automatically by the first crew member.
- `m_sStartFrequency`: the frequency the radio starts on, and goes back to after every key fill (e.g. an air net on helicopters). When empty, the first of these that is set wins: the faction's `m_sVehicleFrequency` (on its callsign info, next to `m_aGroupFrequencyOverrides`), the frequency config's `m_sVehicleFrequency`, the first long-range frequency on the radio's channel list, channel 1. A start frequency that isn't on the channel list is added to it.
- `m_bHasSpeaker`, `m_iSpeakerRange` (25 m): the radio can play out loud. Players near the vehicle hear its traffic unless they have the frequency themselves or are close enough to hear the sender. The speaker starts off; the crew turn it on and off with the "Toggle Radio Speaker" keybind (Left Ctrl + End).
- `m_bHasIntercom`, `m_bPassengersOnIntercom` (off): crew hear each other clearly whenever they talk, at any distance between seats. Passenger seats join too when enabled.
- `m_bHasInfantryPhone` (on for `TrackedVehicle_Base` and `Wheeled_APC_Base`): an infantry phone at the back of the vehicle. A player on foot standing within 2 m of the middle of the rear uses "Pick Up Infantry Phone" and is on the crew intercom: they and the crew hear each other clearly while they hold it ("Hang Up Infantry Phone" to put it back). One person at a time, one phone per person. The server hangs it up if they move more than 4 m from it, die, are knocked out, get in a vehicle, or the vehicle is destroyed. The phone's position comes from the vehicle's bounding box, so it fits any vehicle on those bases. Another vehicle prefab needs `m_bHasInfantryPhone` plus the `CVON_InfantryPhone` action context (centred, radius 6, like vanilla's Unflipper) and `CVON_InfantryPhoneAction` on its `ActionsManagerComponent`; see the two base prefab overrides.

# Manpack radios: handset and speaker

A manpack is a long-range CVON radio carried by someone wearing a radio backpack (any backpack with a radio component, e.g. the CRF's inert R-107M / AN/PRC-77 / PRC-351 backpacks). Set `m_bManpackNeedsRadioBackpack` off on `CVON_VONGameModeComponent` to make any carried long-range radio count.

- **Handset:** stand behind the radio operator and use "Grab Handset" (the "Back" interaction on characters). While you hold it, their radio is on your radio keys after your own radios: you hear its net and can transmit on it. Use "Return Handset" to give it back. Only one person holds it at a time; the server takes it back if you move more than 3 m from the operator (`CVON_Manpack.HANDSET_CORD_LENGTH`), die, or the radio stops being a manpack.
- **Speaker:** the operator turns the manpack's speaker on or off with the "Toggle Radio Speaker" keybind (Left Ctrl + End, when not in a vehicle crew seat). Its traffic then plays in 3D from the operator for anyone within `m_iSpeakerRange` (15 m by default) who doesn't have the frequency themselves. With handheld speakers on (below), the keybind switches the active radio's speaker, so make the manpack the active radio first.

# Handheld speakers and dropped radios

- **Handheld speakers** (server setting `m_bHandheldSpeakers`): "Toggle Radio Speaker" (Left Ctrl + End, when not in a vehicle crew seat) puts the player's active radio on speaker, handheld or manpack. Its traffic then plays in 3D from the player for anyone nearby who doesn't have the frequency themselves; the player keeps hearing it in their headset. A handheld's speaker fades out over `CVON_RadioComponent.HANDHELD_SPEAKER_RANGE` (8 m), or the radio's `m_iSpeakerRange` if that's smaller. With the setting off, the keybind only reaches manpacks, as before.
- **Dropped radios** (server setting `m_bDroppedRadiosTransmit`): a switched-on radio lying on the ground plays its traffic out loud from where it lies, up to 8 m, whether or not its speaker was on. Turn it off before dropping it to keep it quiet. Only radios someone has carried count (they have a key fill), so spares that were never handed out stay silent, and a radio inside a backpack, crate or vehicle doesn't play.
- **How:** every radio registers with the gamemode when it gets its key fill (`InitializeForFaction`). `BroadcastToSpeakers` checks each one's frequency, then `HasActiveSpeaker` (`IsDropped`: no parent entity and a key fill), and sends nearby players the usual speaker entry. Listeners need no new plugin.
