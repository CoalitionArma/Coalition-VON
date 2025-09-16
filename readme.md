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
