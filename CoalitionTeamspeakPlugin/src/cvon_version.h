/*
 * The plugin's version, used for ts3plugin_version() (what players see in TeamSpeak's info panel and what
 * the game compares against CVON_VONGameModeComponent.m_sTeamspeakPluginVersion) and for the DLL's file
 * version (plugin.rc). Bump all three numbers here, the game's m_sTeamspeakPluginVersion, and the
 * Version in the .ts3_plugin package.ini together.
 */
#ifndef CVON_VERSION_H
#define CVON_VERSION_H

#define CVON_VERSION_MAJOR 3
#define CVON_VERSION_MINOR 2
#define CVON_VERSION_PATCH 1

#define CVON_STRINGIFY2(x) #x
#define CVON_STRINGIFY(x) CVON_STRINGIFY2(x)
#define CVON_VERSION_STRING CVON_STRINGIFY(CVON_VERSION_MAJOR) "." CVON_STRINGIFY(CVON_VERSION_MINOR) "." CVON_STRINGIFY(CVON_VERSION_PATCH)

#endif
