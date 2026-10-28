#!/usr/bin/env bash
# Run only on the dedicated approved Ubuntu host, with this reviewed checkout.
set -euo pipefail
# Releases must be readable by the dedicated service user, even under a root 077 umask.
umask 022
[[ $EUID -eq 0 ]] || { echo 'Run as root on the dedicated host.' >&2; exit 1; }
source_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
exec 9>/run/noah-learning-install.lock
flock -n 9 || { echo 'Another installation is running.' >&2; exit 1; }
[[ -s /etc/noah-learning/server.env ]] || { echo 'Create the protected server environment file first; see deploy/README.md.' >&2; exit 1; }
chown root:root /etc/noah-learning/server.env
chmod 600 /etc/noah-learning/server.env
apt-get update
apt-get install -y python3 python3-venv caddy curl
id noah-learning >/dev/null 2>&1 || useradd --system --home-dir /var/lib/noah-learning --shell /usr/sbin/nologin noah-learning
install -d -m 755 /opt/noah-learning /opt/noah-learning/releases
install -d -o noah-learning -g noah-learning -m 700 /var/lib/noah-learning /var/lib/noah-learning/backups
release=$(mktemp -d /opt/noah-learning/releases/release-XXXXXXXX)
chmod 755 "$release"
mkdir "$release/server" "$release/deploy"
# Explicit allowlist: no credentials, local data, or repository metadata copied.
cp "$source_root"/server/*.py "$source_root/server/requirements.txt" "$release/server/"
cp "$source_root"/deploy/backup.py "$release/deploy/"
python3 -m venv "$release/venv"
"$release/venv/bin/pip" install -r "$release/server/requirements.txt"
"$release/venv/bin/python" -m compileall -q "$release/server"
caddy validate --config "$source_root/deploy/Caddyfile" --adapter caddyfile
if [[ -f /var/lib/noah-learning/attempts.sqlite ]]; then
    runuser -u noah-learning -- python3 "$release/deploy/backup.py" /var/lib/noah-learning/attempts.sqlite "/var/lib/noah-learning/backups/pre-deploy-$(date -u +%Y%m%dT%H%M%S)-$$.sqlite"
fi
previous=$(readlink -f /opt/noah-learning/current || true)
# Atomic same-filesystem symlink replacement; persistent data stays outside releases.
ln -s "$release" /opt/noah-learning/current.next
mv -Tf /opt/noah-learning/current.next /opt/noah-learning/current
install -m 644 "$source_root"/deploy/noah-learning*.service "$source_root"/deploy/noah-learning*.timer /etc/systemd/system/
systemctl daemon-reload
systemctl enable noah-learning.service noah-learning-backup.timer
systemctl restart noah-learning.service
healthy=false
for _ in {1..15}; do
    if curl --fail --silent --max-time 2 http://127.0.0.1:8787/health >/dev/null; then healthy=true; break; fi
    sleep 1
done
if [[ $healthy != true ]]; then
    if [[ -n $previous && -d $previous ]]; then
        ln -s "$previous" /opt/noah-learning/current.rollback
        mv -Tf /opt/noah-learning/current.rollback /opt/noah-learning/current
        systemctl restart noah-learning.service
    fi
    echo 'Health check failed; inspect service status. Database backup retained; no data rollback performed.' >&2
    exit 1
fi
# Dedicated host only: install this hostname after API health succeeds.
install -m 644 "$source_root/deploy/Caddyfile" /etc/caddy/Caddyfile
systemctl enable --now caddy noah-learning-backup.timer
systemctl reload caddy
printf '%s\n' 'Installed. Verify public HTTPS authentication before tablet enrollment.'
