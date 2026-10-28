#!/bin/sh
# Manual timed practice. The provider API keys never belong on the tablet.
set -eu
export RM_TIMED_PRACTICE=1
export RM_SERVER_URL=https://learning.haydenindustries.com
export RM_DEVICE_TOKEN_FILE=/home/root/.config/noah-learning/device-token
exec sh /home/root/run-learning.sh
