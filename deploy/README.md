# Approved hosted deployment

Dedicated Vultr Chicago `vhp-1c-1gb-amd`, Ubuntu 24.04, one vCPU, 1 GB RAM,
25 GB disk. Public catalog base price checked by the project lead: $6/month.
Daily Vultr automatic backups add 20% ($1.20), giving **$7.20/month before
OpenAI usage, taxes, or bandwidth overage**. The dedicated instance has been created with approval; provider daily backups
are enabled for 07:00 UTC. No scripts here create cloud resources or change DNS.

Vultr pricing source (checked 2026-10-27):
https://docs.vultr.com/support/platform/billing/how-much-does-it-cost-to-enable-automatic-backups
Provider backups supplement the daily consistent SQLite copies. They are whole
server backups, not a substitute for an integrity-checked SQLite export.

## Deployed state (2026-10-27)

- Instance `0ac1026d-db34-4315-b8e3-3df581625d5c`, region `ord`, public IPv4 `137.220.56.165`.
- DNSimple authoritative DNS; the user added `learning.haydenindustries.com` A → `137.220.56.165`. Public HTTPS health succeeds.
- Caddy fronts the non-root `noah-learning` service on loopback 8787. The service uses `--require-device-token`; no/wrong bearer returns 401 and valid bearer returns 200.
- UFW permits public TCP 80/443 and SSH TCP 22 only from `the setup computer’s public IP`. The image's default unrestricted SSH rule was removed. No other servers were changed.
- Twelve legacy attempts were copied with their bytes preserved. Two online SQLite backup copies were produced; restoring to `/tmp` passed integrity checks and showed twelve historical attempts and one timed run at that checkpoint. Later practice added data.
- Provider backups are configured daily at 07:00 UTC; the separate consistent SQLite timer is 08:00 UTC plus up to ten minutes jitter.

Local deployment administration uses private ignored files under `.runtime/vultr-admin`
(directory mode 700, private key and known-hosts files mode 600). Never print or
copy their contents. The provider API key is not on the tablet or server.

## DNS

Authoritative nameservers are `ns1.dnsimple-edge.com`, `ns2.dnsimple-edge.net`,
`ns3.dnsimple-edge.io`, and `ns4.dnsimple-edge.org`. Earlier NXDOMAIN checks
preceded the user's A-record change. No apex or unrelated DNS changes were
required. Add AAAA only if IPv6 is deliberately configured and reachable.

## Before running install.sh

Initial resource/deployment approval has been granted and used for the dedicated
host above. Run this installer only on that host after reviewing the release.
No existing production server is a target. Establish an administrator SSH key and an allowlist for its source
address; allow public TCP 80/443, restricted TCP 22, and no public 8787.
The installer does not alter firewall or SSH access. Confirm time synchronization.
Use the Caddy package provided by Ubuntu 24.04; public ports 80/443 and correct
DNS are required for certificate issuance/renewal.

**Verified deployment requirement:** the application router must enforce `DEVICE_TOKEN` bearer
authentication on every practice/legacy endpoint; `/health` must return only a
minimal healthy response. Verify missing/wrong credentials are rejected and
valid credentials work. Caddy does not implement this authentication. The
loopback bind in `server/app.py` must remain 127.0.0.1. Do not deploy the legacy
unauthenticated router.

On the dedicated host, create `/etc/noah-learning` as root, mode 700, then edit
`/etc/noah-learning/server.env` privately as root, mode 600. Set actual
`OPENAI_API_KEY` and a separately generated strong `DEVICE_TOKEN` using
systemd EnvironmentFile syntax. Never paste secrets into shell command
arguments, commit them, source this file, print it, or copy the OpenAI key to
the tablet. The service manager loads the file; the app receives environment
variables and uses `--env-file /dev/null`. No secret template is supplied.
The tablet receives only the HTTPS URL and device credential via its protected
configuration. Rotation requires updating the server file and tablet together.

After code review and local tests, copy the reviewed checkout to the dedicated
host without `.env*`, database files, or repository credentials. Run
`sudo bash deploy/install.sh` there. It installs dependencies, serializes
installations, builds a fresh immutable release, takes an online database
backup when a database exists, atomically changes the `current` symlink, then
restarts and checks loopback health. Failed health restores the previous code
symlink when available. No database is deleted, replaced, or downgraded.
Failed first installation requires inspection. Service unit changes are not
automatically rolled back. Dependencies currently follow the application's
requirements file; record installed versions for the deployed release.

Database: `/var/lib/noah-learning/attempts.sqlite`, owner `noah-learning`, outside
releases. Caddy handles public TLS and forwards to loopback port 8787. The API
runs as a dedicated non-login user, with a root-owned code directory and only
its state directory writable. Backups are mode 600 under its mode-700 state
directory. No request access logging is configured in Caddy. Do not log tokens,
request bodies, or OpenAI credentials in application errors.

Verify externally: certificate hostname/chain, `/health`, unauthenticated
rejection, authenticated v2 requests, restart persistence, timer state, and a
tablet request over Wi-Fi with USB unplugged. Check provider backup status and
`systemctl list-timers noah-learning-backup.timer`. Review service journal
privately if checks fail. Health alone does not prove authentication or v2
behavior.

## Consistent backup and retention

The daily timer runs at 08:00 UTC with up to ten minutes jitter. `backup.py`
uses SQLite's online backup API (including committed WAL data) and validates
`PRAGMA integrity_check` before publishing the copy. Installation takes the
same kind of backup before restarting. Never copy just the live `.sqlite`
file while WAL mode is active. Timer failure must be monitored by the operator.

Backups accumulate without automatic deletion. Review disk usage regularly;
retain at least seven daily copies and four weekly copies after validating an
off-host recovery copy. Transfer selected backups securely to an approved
independent destination; it may incur additional cost and is not configured by
this proposal. Local copies alone do not survive host loss. Provider backup
storage and retention follow Vultr's policy; verify them in the console.

## Restore rehearsal and recovery

First test a copy on a disposable local path, without replacing production:

```bash
python3 deploy/backup.py /path/to/saved-backup.sqlite /tmp/noah-restore-test.sqlite
python3 - /tmp/noah-restore-test.sqlite <<'PY'
import sqlite3, sys
with sqlite3.connect(sys.argv[1]) as db:
    assert db.execute('PRAGMA integrity_check').fetchall() == [('ok',)]
    print(db.execute("SELECT name FROM sqlite_master WHERE type='table'").fetchall())
PY
```

Use a fresh destination on repeated rehearsals. Confirm expected historical
attempt/run/completion counts and representative saved strokes using a
compatible application release against this disposable database.

For an approved production recovery, stop `noah-learning.service` and confirm
it is inactive. Copy the selected backup to a new staging database with
`backup.py`; check integrity and expected history before proceeding. Preserve
the **entire** current database and any `-wal`/`-shm` sidecars together in a
new timestamped recovery directory while the service remains stopped. Never
leave sidecars from the old database beside a replacement. Install the staged
database at the original path with owner `noah-learning:noah-learning` and mode
600, then start the compatible code release. Verify health, authentication,
historical attempts, completed dates and a new practice run. Keep the recovery
directory until acceptance. A restore intentionally returns data to the backup
point; obtain explicit approval for any loss of newer submissions. Do not
restore an older database simply because a code release failed.

A full provider-server restore also needs DNS/IP, firewall, credentials and
certificate checks; rebuilding a dedicated host plus restoring the SQLite copy
may be preferable. Do not restore over another production server.
