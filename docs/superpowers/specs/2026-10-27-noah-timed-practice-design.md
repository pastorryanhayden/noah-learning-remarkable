<!-- Updated after user changed the target to four minutes and rejected network-caused resets. Historical ten-minute runs keep their recorded targets. -->
# Noah’s timed times-table practice

Status: behavior approved by user on 2026-10-27. Vultr selected; domain learning.haydenindustries.com. Resource/deployment approved and dedicated Vultr host deployed; physical acceptance remains in progress. This replaces the three-question sequence as the first usable practice experience. The original sequence and captured attempts remain historical data.

## Confirmed requirements

- Noah practices multiplication facts from the 2–12 tables.
- The purpose is fast recall, not merely eventual correctness.
- A day’s target is four continuous minutes. More than 30 seconds without writing resets the current four-minute block to zero earned time / four minutes remaining.
- Stopping practice resets an unfinished block.
- The server tracks a goal of five completed days per week.
- Recognition and grading stay on the server. Previous attempts and practice runs must not be deleted when a new run starts.
- The tablet connects through Wi-Fi to a hosted internet server; USB is not required while practicing.

## Proposed behavior and defaults

These are implementation choices for approval, not additional requirements the user has already confirmed.

### Practice flow

A simple start screen shows today’s completion and this week’s completed days toward five. Start practice creates a new server-owned run and displays one mixed multiplication fact. Both factors range from 2 through 12. Avoid repeating the immediately previous fact. No other subjects, lessons, lesson catalog, dashboard, accounts for multiple children, or adaptive scheduling are included.

Noah writes with the Marker and taps Check. The server transcribes the ink without seeing the question or expected answer, then grades against its stored question. Wrong/unreadable answers stay on the same fact; correct answers briefly show feedback and automatically load the next fact. Practice continues until the four-minute block is completed, rather than ending after three questions.

### Timer

- The block starts on the first Marker contact, not while Noah waits on the start screen.
- During available practice time, the timer counts elapsed time between strokes too, provided no writing gap exceeds 30 seconds. This permits ordinary thinking and lifting the pen.
- At a writing gap strictly greater than 30 seconds, reset the unfinished block to four minutes remaining and wait for fresh writing before restarting it.
- A Stop control or exiting the app resets unfinished continuous-time progress. A network interruption or server restart pauses and preserves earned progress; reconnect resumes without offline credit. Stop delivery is durable across an offline exit. Submitted strokes and grades remain preserved.
- Waiting for server grading or the next question pauses both earned-time accumulation and the writing-gap deadline. When the next writable question is displayed, resume with a fresh 30-second writing window. The server also enforces this waiting state; a client cannot claim arbitrary grading pauses.
- Four minutes of counted practice completes the block. A completed day remains earned even if Noah subsequently stops or starts another run.
- The server is authoritative. The tablet displays an interpolated countdown, reports actual Marker activity plus foreground/connectivity state, and reconciles with server state. Heartbeats alone must not count as writing. Request/event IDs prevent duplicated credit.
- No offline timing credit is invented or backfilled. Reconnect preserves submissions and reloads current server timing state.

### Daily and weekly tracking

Use America/Chicago for calendar dates, independent of the hosted machine’s system timezone. Count one completion per distinct date toward a Monday–Sunday week; any five days qualify. A block crossing midnight counts on its completion date. Store completed blocks and inactivity/stop resets for review; do not erase earlier runs. Once a day is earned, additional runs do not count as extra days.

### Recall speed

Record question-to-answer duration separately from recognition/network time, plus the recognized answer and correctness. Show a simple end-of-practice accuracy/speed summary. Do not enforce a guessed “instant” cutoff or add a speed penalty in this first version; use the measurements to establish Noah’s baseline. Timing excludes waits for server processing. Exact physical panel latency is not implied by software timestamps.

## Wi-Fi and hosted connection

Use reMarkable’s stock Wi-Fi settings for joining the household network; do not collect Wi-Fi passwords in this app. The practice app uses a configured HTTPS server URL and a limited device credential. The OpenAI key remains server-only. Validate tablet TLS/certificate support on the actual firmware.

The app must remain running independently of the USB setup/SSH connection. Provide a deliberate exit back to the stock UI so Wi-Fi settings remain reachable. Initial installation/configuration may use USB, but acceptance testing must unplug USB and complete real server requests over Wi-Fi.

The user selected Vultr and offered an API key. Store it locally as VULTR_API_KEY in the ignored .env.local; never print it or copy it to the tablet/server. Read-only account checks may use it once supplied. No instance has been created.

Proposed host: Chicago, high-performance AMD plan vhp-1c-1gb-amd, one vCPU / 1 GB RAM / 25 GB disk, Ubuntu 24.04, with Caddy for public HTTPS and the Python service bound to loopback under a dedicated non-root service account. Vultr’s public plan catalog returned a base price of $6/month on 2026-10-27; availability and OS ID must be checked at provisioning time. OpenAI usage and any paid backup option are separate.

Use persistent SQLite storage outside the release directory, preserve existing data through migrations, and provide scheduled consistent backups plus a tested restore procedure. Require a device-scoped bearer credential over HTTPS; it is not the OpenAI or Vultr key. Only web ports are public; restrict administrative SSH. Keep deployment scripts/configuration reviewable and avoid logging secrets.

The selected hostname is learning.haydenindustries.com; DNS provider/access must be verified. Prepare the final deployment and exact cost before requesting resource approval. No paid resources, DNS changes or public deployment are authorized by this draft alone.

## Preservation and acceptance

Keep all historical strokes, grades and completed runs. A new run has a new identity; repeated start requests are idempotent. Late grades must not attach to a later run/question. An interrupted submission remains retryable without duplicate grading or lost ink.

Automated tests must cover the timer at 30 seconds and just beyond it, resets after prior progress, waiting exclusions, four-minute completion, once-per-date credit, week/date boundaries, duplicate/reordered activity, restart/reconnect, stale run submissions and idempotent run creation. Use injected clocks to test four minutes without four-minute test sleeps.

Hardware acceptance must include Wi-Fi with USB unplugged, responsive Marker writing, server-graded right/wrong/unreadable answers, inactivity reset, a full real four-minute completed block, hosted-server restart/persistence and recovery without losing saved work. Restore the stock UI after testing. Existing firmware-specific E Ink/pen compatibility boundaries still apply.
