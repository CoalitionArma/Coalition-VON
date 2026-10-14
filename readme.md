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

# For developers: how the game and the plugin talk

The game and the TeamSpeak plugin exchange small files in the client's profile folder (`Documents\My Games\ArmaReforger\profile`). The plugin wakes up whenever one of them changes.

| File | Written by | Purpose |
|---|---|---|
| `VONServerData.json` | both | In-game state, TeamSpeak client ID, VON channel and server |
| `VONData.json` / `VONData.bin` | game, ~20 times a second (immediately on push-to-talk) | Everyone the player can hear: stereo gains, muffling, radio frequency and quality, `IsTransmitting`. The plugin uses whichever was written last. |
| `RadioData.json` | game, when one of the player's radios changes | The player's radios: frequency, volume, ear, encryption key |
| `VONHeartbeat.json` | game, every second | Liveness. If it stops while in game (crash), the plugin closes the mic after 3 s and leaves the VON channel after 10 s. |
| `VONPluginInfo.json` | plugin, every 5 s; deleted on exit | What the plugin supports (`bin1`, `talking1`, `heartbeat1`, `comms1`). The game only writes `VONData.bin` while this lists `bin1`. |
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

# Vehicle radios and intercom

`CVON_VehicleRadioComponent` gives a vehicle a radio, a speaker and an intercom. The mod adds it to the vanilla `TrackedVehicle_Base`, `Wheeled_APC_Base`, `Helicopter_Base`, `Wheeled_Car_Base` and `Wheeled_Truck_Base` prefabs, so vehicles inheriting from those have comms; add it to any other vehicle prefab (or remove it) in Workbench. Settings on the component:

- `m_bMilitaryOnly`: comms only when the vehicle's default faction is a military one (`SCR_Faction.IsMilitary`). On for the car and truck bases, which civilian vehicles share; a vehicle with a civilian or no default faction gets nothing.

- `m_bHasRadio`, `m_sRadioName`, `m_iRadioRange`, `m_eRadioMenu`: the radio. Only the crew can use it: driver/pilot and turret seats (gunner, door gunner), plus commanders, including vanilla's commander seats that are passenger seats linked to a commander's sight (LAV-25) or labelled as the commander's. While seated it is added after their own radios, on the next free radio key; it opens and tunes like any radio. It starts on the vehicle's prefab faction key and keeps it, playing that traffic, until a crew member fills it with the "Fill Vehicle Radio With Your Encryption" keybind (Left Ctrl + Home): it then takes their faction key and the channel list of their own radio (or their faction's channels if they carry none). A radio with no key at all (a vehicle without a faction) is filled automatically by the first crew member.
- `m_sStartFrequency`: the frequency the radio starts on, and goes back to after every key fill (e.g. an air net on helicopters). When empty, the first of these that is set wins: the faction's `m_sVehicleFrequency` (on its callsign info, next to `m_aGroupFrequencyOverrides`), the frequency config's `m_sVehicleFrequency`, the first long-range frequency on the radio's channel list, channel 1. A start frequency that isn't on the channel list is added to it.
- `m_bHasSpeaker`, `m_iSpeakerRange` (25 m): the radio plays out loud. Players near the vehicle hear its traffic unless they have the frequency themselves or are close enough to hear the sender. The crew toggle it with the "Toggle Radio Speaker" keybind (Left Ctrl + End).
- `m_bHasIntercom`, `m_bPassengersOnIntercom` (off): crew hear each other clearly whenever they talk, at any distance between seats. Passenger seats join too when enabled.

# Manpack radios: handset and speaker

A manpack is a long-range CVON radio carried by someone wearing a radio backpack (any backpack with a radio component, e.g. the CRF's inert R-107M / AN/PRC-77 / PRC-351 backpacks). Set `m_bManpackNeedsRadioBackpack` off on `CVON_VONGameModeComponent` to make any carried long-range radio count.

- **Handset:** stand behind the radio operator and use "Grab Handset" (the "Back" interaction on characters). While you hold it, their radio is on your radio keys after your own radios: you hear its net and can transmit on it. Use "Return Handset" to give it back. Only one person holds it at a time; the server takes it back if you move more than 3 m from the operator (`CVON_Manpack.HANDSET_CORD_LENGTH`), die, or the radio stops being a manpack.
- **Speaker:** the operator turns the manpack's speaker on or off with the "Toggle Radio Speaker" keybind (Left Ctrl + End, when not in a vehicle crew seat). Its traffic then plays in 3D from the operator for anyone within `m_iSpeakerRange` (15 m by default) who doesn't have the frequency themselves.
