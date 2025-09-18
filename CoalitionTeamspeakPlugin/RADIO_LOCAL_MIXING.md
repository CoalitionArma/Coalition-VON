# Simultaneous Radio + Local Voice Implementation

## Overview
This update enhances the Coalition TeamSpeak Plugin to support simultaneous radio and local voice communication. When a player is speaking on the radio and is also within local proximity range, other players will hear them both through the radio (with radio effects) and locally (with proximity-based spatialization and effects).

## New Functionality

### Key Features Added:

1. **Simultaneous Communication**: Players speaking on radio can now also be heard locally if they are within proximity range
2. **Smart Audio Mixing**: Radio and local audio are intelligently mixed to provide realistic communication
3. **Proximity Detection**: Uses existing gain values to determine if local voice should be mixed with radio
4. **Proper Spatialization**: Local voice maintains directional audio and muffle effects when mixed with radio

## Technical Implementation

### New Constants
```c
#define LOCAL_AUDIBLE_THRESHOLD 0.01f  // Threshold for local voice audibility
```

### New Functions

#### `has_local_proximity(const VonEntry* entry)`
- Determines if a radio user is also within local speaking range
- Uses LOCAL_AUDIBLE_THRESHOLD to determine audibility
- Returns 1 if user should also be heard locally, 0 if radio-only

### Enhanced Radio Processing Logic

The radio processing logic now follows this behavior:

1. **Radio Match + Local Proximity**: 
   - Process radio audio with full Acre-like effects (distortion, static, filters)
   - Process local audio with proximity gains, muffle effects, and spatialization  
   - Mix both signals together with appropriate balance
   - Apply radio stereo settings to radio portion
   - Apply proximity-based L/R panning to local portion

2. **Radio Match + No Local Proximity**:
   - Process radio-only (original behavior)
   - No local audio mixed in

3. **No Radio Match**:
   - Fallback to direct communication only (original behavior)

### Audio Processing Details

#### Radio Audio Path:
- Converts to mono for radio processing
- Applies radio effects (boost, ring modulation, distortion, bandpass filtering)
- Applies radio volume and quality settings
- Generates appropriate static/noise based on connection quality
- Applies radio stereo settings (left/right/both)

#### Local Audio Path (when mixed):
- Uses original stereo/spatial information from game
- Applies proximity-based gain scaling
- Applies muffle/occlusion effects if present
- Applies yell boost for close-range communication
- Maintains directional spatialization (L/R panning)

#### Mixing Logic:
- Radio audio is reduced slightly when mixed to prevent overwhelming
- Local audio is scaled appropriately to maintain intelligibility
- Proper headroom management to prevent clipping
- Maintains overall volume balance

## Benefits

### Realistic Communication:
- **Natural Behavior**: Mimics real-world communication where you hear both radio and the person speaking nearby
- **Situational Awareness**: Players can determine if radio speaker is nearby based on mixed audio
- **Immersive Experience**: More realistic military/tactical communication

### Enhanced Gameplay:
- **Better Coordination**: Teams can communicate more effectively when in proximity
- **Tactical Advantage**: Knowing when radio users are nearby provides tactical information
- **Reduced Confusion**: Clear distinction between radio-only and local+radio communication

### Technical Benefits:
- **Maintains Compatibility**: Existing radio and direct communication still work as before
- **Performance Optimized**: Only processes additional audio when actually needed
- **Clean Implementation**: Reuses existing audio processing pipelines

## Configuration

The feature works automatically with existing configuration:
- **Radio Settings**: Uses existing RadioData.json for radio matching and settings
- **Proximity Settings**: Uses existing VON gain values for local proximity detection
- **No New Config Required**: Works with current setup without additional configuration

## Thresholds and Tuning

- **LOCAL_AUDIBLE_THRESHOLD (0.01f)**: Minimum gain required for local voice to be audible
  - Higher values = closer proximity required for local voice
  - Lower values = local voice audible at greater distances
- **Mixing Balance**: Radio audio at full strength, local audio at 60% to prevent overwhelming
- **Headroom**: 85% to prevent audio clipping during mixing

## Version Information

- **Plugin Version**: 1.11.0
- **New Feature**: Simultaneous radio + local voice communication
- **Backward Compatible**: All existing functionality preserved

This enhancement provides a more realistic and immersive communication experience while maintaining all existing functionality and performance characteristics.
