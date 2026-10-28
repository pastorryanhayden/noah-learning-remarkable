#!/bin/sh
# Run on the tablet after opening the USB SSH reverse tunnel from the host.
set -eu
export RM_SERVER_URL="${RM_SERVER_URL:-http://127.0.0.1:18787}"
exec sh "${RM_DEVICE_LAUNCHER:-/home/root/run-learning.sh}"
