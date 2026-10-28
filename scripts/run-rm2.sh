#!/bin/sh
# Manual launch for the verified 3.28.0.172 rm2 runtime. No boot installation.
set -eu
runtime=/home/root/learning-runtime
app=/home/root/remarkable_learning
masked_here=0
app_pid=

cleanup() {
    result=$?
    trap - EXIT HUP INT TERM
    if [ -n "$app_pid" ]; then
        kill "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    systemctl reset-failed xochitl || true
    systemctl start xochitl || result=1
    if [ "$masked_here" = 1 ]; then
        systemctl unmask --runtime remarkable-fail.service || result=1
    fi
    exit "$result"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

# xochitl segfaulted on stop on this device; its OnFailure handler reboots.
# Preserve an existing mask; remove only a runtime mask created by this script.
case "$(systemctl show remarkable-fail.service -p LoadState --value)" in
    masked) ;;
    *) systemctl mask --runtime remarkable-fail.service; masked_here=1 ;;
esac
systemctl stop xochitl
export PATH="$runtime/bin:$PATH"
export TESSDATA_PREFIX="$runtime/share/tessdata"
export QT_PLUGIN_PATH="$runtime/plugins"
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="rotate=180:invertx"
# Portrait mapping confirmed by the user on the connected 3.28.0.172 rm2.
export RM_PEN_SWAP_XY=1
export RM_PEN_INVERT_Y=1
export RM_MONO_INK=1
export RM_EPAPER_UI_MODE="${RM_EPAPER_UI_MODE:-pen}"
export QT_QUICK_BACKEND=epaper
"$app" -platform epaper &
app_pid=$!
wait "$app_pid"
