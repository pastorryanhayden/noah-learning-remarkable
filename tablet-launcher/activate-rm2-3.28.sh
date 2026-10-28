#!/bin/bash
set -euo pipefail
umask 077
# This resource map and private app rendering were verified only on this release.
if ! grep -q '^IMG_VERSION="3.28.0.172"$' /etc/os-release; then
 echo 'Launcher activation requires the verified rm2 firmware 3.28.0.172; revalidate after updates.' >&2
 exit 1
fi
for required in /home/root/xovi/start /home/root/xovi/stock /home/root/xovi/rebuild_hashtable; do
 test -r "$required" || { echo 'Reviewed XOVI installation is missing.' >&2; exit 1; }
done
masked_here=0
success=0
cleanup() {
 result=$?
 trap - EXIT HUP INT TERM
 if [[ $success != 1 ]]; then
  bash /home/root/xovi/stock >/home/root/noah-launcher-rollback.log 2>&1 || true
 fi
 systemctl reset-failed xochitl || true
 systemctl start xochitl || result=1
 if [[ $masked_here == 1 ]]; then systemctl unmask --runtime remarkable-fail.service || result=1; fi
 exit "$result"
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
if [[ $(systemctl show remarkable-fail.service -p LoadState --value) != masked ]]; then
 systemctl mask --runtime remarkable-fail.service
 masked_here=1
fi
if [[ ! -s /home/root/xovi/exthome/qt-resource-rebuilder/hashtab ]]; then
 printf '\n' | bash /home/root/xovi/rebuild_hashtable >/home/root/noah-launcher-hashtab.log 2>&1
fi
test -s /home/root/xovi/exthome/qt-resource-rebuilder/hashtab
bash /home/root/xovi/start >/home/root/noah-launcher-activation.log 2>&1
sleep 3
systemctl is-active --quiet xochitl
success=1
printf 'Practice launcher active for this boot.\n'
