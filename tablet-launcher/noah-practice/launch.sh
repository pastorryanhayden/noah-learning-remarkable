#!/bin/sh
# AppLoad entry: hand off to PID 1 before the practice wrapper stops Xochitl.
set -eu
wrapper=/home/root/run-rm2-practice.sh
if [ ! -r "$wrapper" ] || [ ! -x /home/root/remarkable_learning ]; then
    echo 'Practice application or launcher is missing.' >&2
    exit 1
fi
state=$(systemctl show noah-practice.service --property=ActiveState --value 2>/dev/null || true)
case "$state" in
    active|activating|reloading|deactivating) exit 0 ;;
esac
# A fixed unit name also makes concurrent taps fail safely rather than fork
# another application. No credential is passed in arguments or printed.
exec systemd-run --no-block --unit=noah-practice --collect \
    --property=KillMode=mixed --property=TimeoutStopSec=20 \
    /bin/sh "$wrapper"
