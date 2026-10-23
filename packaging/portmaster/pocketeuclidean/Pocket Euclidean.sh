#!/bin/bash
# Pocket Euclidean - PortMaster launch script.
# Structure follows https://portmaster.games/packaging.html. Not yet tested on
# any device; see docs/PLATFORMS.md (NextUI validation gate).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=/$directory/ports/pocketeuclidean
CONFDIR="$GAMEDIR/conf/"

mkdir -p "$CONFDIR"
cd "$GAMEDIR"

> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$CONFDIR"
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

BINARY="pocketeuclidean.${DEVICE_ARCH}"
if [ ! -f "$BINARY" ]; then
  pm_message "Missing $BINARY for this device architecture."
  sleep 5
  pm_finish
  exit 1
fi
$ESUDO chmod +x "$GAMEDIR/$BINARY"

# The app reads SDL game controllers directly (no gptokeyb); Select+Start exits.
# Extra options, e.g. POCKET_ARGS="--buffer 512 --rate 44100", may be exported
# before launch to tune audio on a device.
pm_platform_helper "$GAMEDIR/$BINARY"
./"$BINARY" --fullscreen --data "$CONFDIR" $POCKET_ARGS

pm_finish
