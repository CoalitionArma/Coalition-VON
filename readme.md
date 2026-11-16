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
        "m_bTerrainBlocksRadio": true,
        "m_fTerrainBlockStrength": 1.0,
        "m_bRadioCollisions": true,
        "m_bSquelchTones": true,
        "m_bTransmissionCutOff": true
    }
}
```

| Field | Type, default | What it does |
|---|---|---|
| `m_eRadioGameAudio` | int, `0` | Mixing game audio into radio transmissions. `0`: each player's own setting (settings menu, **Coalition VON** tab). `1`: on for everyone. `2`: off for everyone. Players see the server's choice, greyed out, on that tab. |
| `m_bTerrainBlocksRadio` | bool, `true` | Hills and mountains between sender and receiver weaken radio signals. |
| `m_fTerrainBlockStrength` | float 0–2, `1.0` | How much terrain weakens them: `0` none, `1` normal, `2` double. |
| `m_bRadioCollisions` | bool, `true` | Two players transmitting on the same net at once step on each other. The stronger signal is heard with a squeal, and the weaker is lost. |
| `m_bSquelchTones` | bool, `true` | Listeners hear a squelch burst when a transmission opens and a squelch tail when it closes. |
| `m_bTransmissionCutOff` | bool, `true` | A radio operator killed or knocked out mid-transmission is cut off with a burst of static. |

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
| `VONPluginInfo.json` | plugin, every 5 s; deleted on exit | What the plugin supports (`bin1`, `talking1`, `heartbeat1`, `comms1`, `gamemix1`, `immersion1`). The game only writes `VONData.bin` while this lists `bin1`. It only adds `RadioMix` / `GameAudio` to `VONData.json` while it lists `gamemix1`, and the radio immersion keys while it lists `immersion1`. |
| `VONTalking.json` | plugin, while in game | TeamSpeak client IDs currently talking; the game refreshes players who aren't talking less often. |

`VONData.bin` is the same data as `VONData.json` packed into fixed records. Its layout is documented in `load_von_bin_into` (plugin.c) and written by `WriteVONDataBinary` (CVON_SCR_VONController.c); change both together and bump the format version.

Each entry in `VONData` has a `VONType`:

| VONType | Meaning | Plugin sound |
|---|---|---|
| 0 Direct | Proximity voice | Positioned by the game's left/right gains; muffle and behind-the-head filters |
| 1 Radio | Radio transmission | Radio chain if one of the player's radios matches, otherwise direct |
| 2 Intercom | Talker on the player's vehicle intercom (the listener's game decides this) | Clear, centred, telephone band |
| 3 Speaker | Radio traffic heard through a nearby vehicle's speaker | Radio chain, then positioned at the vehicle like direct |

Plugins before 3.0 play types 2 and 3 as direct speech.

# Game audio on radio

While a player talks on a radio, the plugin mixes what their game is playing (gunfire, explosions, vehicles, everything the game outputs) into their outgoing voice, so the net hears the sender's surroundings. Players turn it on or off with "Mix Game Audio Into Radio" on the settings menu's **Coalition VON** tab (`CVON_GameSettings.m_bRadioGameAudio`, on by default). Direct speech never carries it.

- **Game → plugin:** `VONData.bin` header flags bit 1 `RadioMix` (talking on a radio with the setting on) and bit 2 `GameAudio` (the setting is on). In `VONData.json` they are the top-level `RadioMix` / `GameAudio` keys, written only for plugins that list `gamemix1`; older plugins would read any other top-level key as a player.
- **Capture** (`cvon_gameaudio.c`): Windows per-process loopback of the Arma Reforger process (`ArmaReforgerSteam*.exe`, or Workbench when playing in it), so TeamSpeak and other programs are never recorded. Needs Windows 10 version 2004 or later. Elsewhere the capture fails, the plugin retries with a growing delay, and transmissions are plain voice. The capture runs while in game with the setting on, so it is ready when the player keys up.
- **Mixing** (`ts3plugin_onEditCapturedVoiceDataEvent` in plugin.c): TeamSpeak calls it after its own noise suppression and voice detection, and only frames TeamSpeak is about to send are touched, so a muted mic or voice activation still decide what goes out. The first 250 ms after keying are skipped, so the game's own key-up beep isn't sent. The game audio then fades in, goes through a peak limiter (`GAMEMIX_CEILING`) and drops up to 6 dB while the player speaks. Levels are the `GAMEMIX_*` defines.
- **Listeners need nothing new:** the mix happens on the sender's side, and the transmission goes through each listener's radio chain as usual. Players close enough to hear the sender directly also get the game audio in their direct audio (TeamSpeak sends one stream to everyone), a little after their own game plays the same sound.

# Radio immersion

Each feature has a server setting (see "CVON server settings"). When the server turns one off, the game stops sending its data, so the plugin plays nothing for it.

- **Terrain** (`ComputeTerrainFactor` in CVON_SCR_VONController.c): the listener's game samples the terrain height every 25 m along the path between the two radios, about 1.6 m above each. The highest ridge above that line costs up to 60 % of the signal, each further ridge 15 %, and the share of the path in shadow up to 25 %, scaled by `m_fTerrainBlockStrength`. It multiplies the distance-based `ConnectionQuality`, which the plugin already turns into static. It uses height lookups, not traces, and each talker is recomputed once a second or when either end moves 25 m. A helicopter or a hilltop operator clears the hills on their own.
- **Collisions** (`UpdateCollisions`): the listener's game groups radio and speaker entries by frequency, time deviation and key. Where two or more transmit at once, the strongest signal is marked captured and the others lost. The plugin plays a captured transmission with a drifting heterodyne squeal and a quarter less quality, and drops a lost one to about a tenth of its voice in heavy static (`COLLISION_*` / `HETERODYNE_*` defines).
- **Squelch** (`update_squelch_events`, `squelch_render` in plugin.c): the plugin watches for radio talkers that one of the player's radios receives starting and stopping. It plays a short squelch burst on open and a squelch tail on close, longer and louder for weak signals, panned to that radio's ear at its volume. They're added in TeamSpeak's mixed playback callback, so a tail can play after the talker's own audio has ended.
- **Cut-off**: a sender whose character stops being alive mid-transmission sends a cut-off (`BroadcastCutOffLocalVONToServer`) instead of the normal release. The server tells each receiver (`CutOffLocalVONBroadcast`), then ends the transmission as usual, so `RemoveLocalVONBroadcasts` and anything hooked on it still run. Each receiver keeps the entry 0.5 s marked as cut off, and its plugin silences the talker and plays a burst of crackling static instead of the squelch tail.

Format, all in fields older plugins ignore:
- **`VONData.bin`:** header flags bit 3 `SquelchTones`; entry flags bit 1 collision captured, bit 2 collision lost, bit 3 cut off.
- **`VONData.json`:** top-level `SquelchTones`; per entry `Collision` (0 none, 1 captured, 2 lost) and `CutOff`.

# Vehicle radios and intercom

`CVON_VehicleRadioComponent` gives a vehicle a radio, a speaker and an intercom. The mod adds it to the vanilla `TrackedVehicle_Base`, `Wheeled_APC_Base`, `Helicopter_Base`, `Wheeled_Car_Base` and `Wheeled_Truck_Base` prefabs, so vehicles inheriting from those have comms; add it to any other vehicle prefab (or remove it) in Workbench. Settings on the component:

- `m_bMilitaryOnly`: comms only when the vehicle's default faction is a military one (`SCR_Faction.IsMilitary`). On for the car and truck bases, which civilian vehicles share; a vehicle with a civilian or no default faction gets nothing.

- `m_bHasRadio`, `m_sRadioName`, `m_iRadioRange`, `m_eRadioMenu`: the radio. Only the crew can use it: driver/pilot and turret seats (gunner, door gunner), plus commanders, including vanilla's commander seats that are passenger seats linked to a commander's sight (LAV-25) or labelled as the commander's. While seated it is added after their own radios, on the next free radio key; it opens and tunes like any radio. It starts on the vehicle's prefab faction key and keeps it, playing that traffic, until a crew member fills it with the "Fill Vehicle Radio With Your Encryption" keybind (Left Ctrl + Home): it then takes their faction key and the channel list of their own radio (or their faction's channels if they carry none). A radio with no key at all (a vehicle without a faction) is filled automatically by the first crew member.
- `m_sStartFrequency`: the frequency the radio starts on, and goes back to after every key fill (e.g. an air net on helicopters). When empty, the first of these that is set wins: the faction's `m_sVehicleFrequency` (on its callsign info, next to `m_aGroupFrequencyOverrides`), the frequency config's `m_sVehicleFrequency`, the first long-range frequency on the radio's channel list, channel 1. A start frequency that isn't on the channel list is added to it.
- `m_bHasSpeaker`, `m_iSpeakerRange` (25 m): the radio can play out loud. Players near the vehicle hear its traffic unless they have the frequency themselves or are close enough to hear the sender. The speaker starts off; the crew turn it on and off with the "Toggle Radio Speaker" keybind (Left Ctrl + End).
- `m_bHasIntercom`, `m_bPassengersOnIntercom` (off): crew hear each other clearly whenever they talk, at any distance between seats. Passenger seats join too when enabled.

# Manpack radios: handset and speaker

A manpack is a long-range CVON radio carried by someone wearing a radio backpack (any backpack with a radio component, e.g. the CRF's inert R-107M / AN/PRC-77 / PRC-351 backpacks). Set `m_bManpackNeedsRadioBackpack` off on `CVON_VONGameModeComponent` to make any carried long-range radio count.

- **Handset:** stand behind the radio operator and use "Grab Handset" (the "Back" interaction on characters). While you hold it, their radio is on your radio keys after your own radios: you hear its net and can transmit on it. Use "Return Handset" to give it back. Only one person holds it at a time; the server takes it back if you move more than 3 m from the operator (`CVON_Manpack.HANDSET_CORD_LENGTH`), die, or the radio stops being a manpack.
- **Speaker:** the operator turns the manpack's speaker on or off with the "Toggle Radio Speaker" keybind (Left Ctrl + End, when not in a vehicle crew seat). Its traffic then plays in 3D from the operator for anyone within `m_iSpeakerRange` (15 m by default) who doesn't have the frequency themselves.
