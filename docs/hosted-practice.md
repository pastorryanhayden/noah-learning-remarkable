# Hosted timed practice: operation and acceptance

The resource and deployment proposal was approved and deployed on 2026-10-27.
The dedicated Vultr Chicago (`ord`) server at `137.220.56.165` serves
https://learning.haydenindustries.com through Caddy. DNSimple hosts the zone;
the user added the hostname's A record. Cost is $6/month compute plus $1.20
provider backups, **$7.20/month before OpenAI usage, tax and overage**.
See [deployment and database recovery](../deploy/README.md).

## Practice and current status

Noah practices mixed 2–12 multiplication facts for four continuous minutes.
Writing gaps strictly greater than 30 seconds reset unfinished progress; Stop
also resets it. Network interruptions now pause and preserve earned progress with automatic resume and no offline credit. The server pauses time while grading and while the next
question is being displayed. Heartbeats alone do not count as writing. One
completion per America/Chicago date counts toward five days per Monday–Sunday
week. Previous strokes, grades and earned days remain preserved.

The tablet connected over Wi-Fi, with USB unplugged, and made nine real cloud
submissions; five were graded correct. This establishes independent launch,
Wi-Fi connectivity, TLS, authentication and real server grading. The initial mini recognizer made false rejections (`33`→`23`, `99`→`79`/`77`). The upgraded GPT-6.1 Sol recognizer passed16/16 saved-stroke API checks; fresh hosted answers22/84/77 were accepted, while deliberate wrong/unreadable answers stayed on the question. This establishes those examples, not universal handwriting accuracy.

The user confirmed the physical greater-than-30-second reset, with a matching server inactivity event and600000ms remaining. The original ten-minute test failed on a13-second transport gap, resetting earned progress. The user declined a repeat and changed the target to four minutes; network recovery has been changed accordingly. No full physical four-minute completion is claimed. Automated clocks do not replace physical acceptance.
The host and tablet passed five Qt suites, including eleven timed-client tests;
47 Python tests passed. The twelve historical legacy server attempts were
copied with their bytes preserved. Two consistent SQLite copies and a restore
to a disposable `/tmp` database passed integrity/history checks (twelve legacy
attempts and one timed run at that checkpoint). This was a restore rehearsal,
not replacement of the running production database.

## Tablet setup and independent launch

Validated device: reMarkable 2 firmware **3.28.0.172**. Use stock Wi-Fi settings
to join the household network; the application does not collect Wi-Fi passwords.
The observed address `192.168.1.68` is dynamic; use the stock settings to find a
new address if needed. TLS uses the tablet's stock network plugins and
certificate store. No provider API key is on the tablet; the OpenAI key remains
server-only. The tablet holds a separate device bearer credential in
`/home/root/.config/noah-learning/device-token`, owned by root, mode 600.
Do not print that file or include it in support output.

The reviewed launcher is [scripts/run-rm2-practice.sh](../scripts/run-rm2-practice.sh).
It selects timed mode, the HTTPS URL and the private credential-file path,
then invokes `/home/root/run-learning.sh`. The base launcher selects the tested
Pen refresh mode, preserves an existing runtime recovery mask, and restores
`xochitl` on exit. The app and private runtime must already be installed as
specified in the [device checklist](device-checklist.md).

For installation/update of the launcher over USB, with the stock UI available:

```sh
scp -F /dev/null scripts/run-rm2-practice.sh root@10.11.99.1:/home/root/run-rm2-practice.sh
```

Start from a USB SSH session using a transient system service so closing SSH
or unplugging USB does not kill practice:

```sh
ssh -F /dev/null root@10.11.99.1
systemd-run --unit=noah-practice --collect --property=KillMode=mixed --property=TimeoutStopSec=20 /bin/sh /home/root/run-rm2-practice.sh
systemctl is-active noah-practice
```

Use Start practice on the tablet, then unplug USB. No USB tunnel or host-side
local server is required. This is a manual transient launch, not a boot service.
Do not launch a second instance while `noah-practice` is running. Stop ends
practice while leaving the app open. Exit sends a best-effort Stop, briefly
waits, and quits so the wrapper restores the stock UI; server liveness expiry
resets unfinished progress if connectivity prevents delivery.

## Stop and stock-UI recovery

Prefer the tablet's Exit control. To stop remotely over USB:

```sh
ssh -F /dev/null root@10.11.99.1
systemctl stop noah-practice
systemctl is-active xochitl
```

`KillMode=mixed` gives the wrapper an opportunity to clean up; its trap stops
the child application, starts `xochitl`, and removes only a runtime recovery
mask it created. A timeout or power loss can interrupt cleanup. If the stock UI
is not active, inspect status and follow the firmware-specific recovery steps
in the device checklist. Do not remove a pre-existing mask indiscriminately.
The stock UI keeps Wi-Fi settings reachable. Firmware updates require repeating
pen, refresh, TLS and recovery validation.

## Server access and recovery

The production API runs as dedicated non-root `noah-learning`, binds only
`127.0.0.1:8787`, and requires the device token. Caddy terminates public TLS.
Public health succeeds; missing/wrong bearer requests returned 401 and valid
requests 200. SSH is permitted only from `the setup computer’s public IP`; ports 80/443 are public.
No other production servers were touched.

Data is `/var/lib/noah-learning/attempts.sqlite`; releases are separate under
`/opt/noah-learning`. The server environment file is root-owned mode 600 and
loaded by systemd, never sourced. Provider backups run daily at 07:00 UTC;
the consistent SQLite timer runs at 08:00 UTC with jitter. Follow
[backup and restore instructions](../deploy/README.md) before replacing data.
Keep old database sidecars together while stopped; never overwrite live SQLite
storage with a plain copy. A restore can discard newer work and needs deliberate
approval for that loss. Local backup copies alone do not protect against host loss.

## Recognition and timing follow-up

The original mini recognizer misread saved33/99 strokes. The deployed GPT-6.1 Sol recognizer with low reasoning and1024 output-token budget passed16/16 explicit API checks over eight saved stroke cases. Fresh hosted answers22,84,77 were accepted; deliberate5 and unreadable strokes were rejected. User confirmed physical idle reset; server recorded ready/600000ms. Full real ten-minute acceptance remains in progress.
