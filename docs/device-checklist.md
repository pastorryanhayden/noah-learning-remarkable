# Device validation checklist

Run these checks on a reMarkable 2 connected over USB. The single-exercise flow is now verified on software 3.28.0.172; the subsequent Pen-mode test resolves the observed lag/outline behavior. Exact latency and long-session ghosting remain unmeasured. Entries below preserve the session history, including failures and superseded blockers.

## Session evidence — 2026-10-27

- Workspace `.git` is an empty read-only directory, not a usable repository. No Git operations were attempted.
- Default SSH invocation fails on host configuration permissions (`/etc/ssh/ssh_config.d/20-omarchy-keepalive.conf`). Passing `-F /dev/null` bypasses that configuration without changing it.
- The restricted execution environment rejects socket creation (`socket: Operation not permitted`). An approved execution outside that restriction reached the SSH server at `10.11.99.1:22`.
- With an isolated temporary known-hosts file and `StrictHostKeyChecking=accept-new`, SSH recorded an ED25519 host key, then returned `Permission denied (publickey,password)` for root in batch mode. Reachability is established; authenticated access is not. The key was accepted on first use, not independently fingerprint-verified.
- Next prerequisite: authorize this host's SSH key on the tablet or supply the path to an already authorized key. Do not put passwords in project files or chat. The temporary host-key file is `/tmp/remarkable-learning-known-hosts`; it is not a permanent credential setup.
- Fresh host validation: CMake configure and build succeeded; CTest passed both `ink_test` and `pen_test` (2/2). The ink suite exercises local Tesseract recognition of a synthetic stroke-built `56`, grading, and SQLite storage of strokes. The pen suite exercises synthetic event mapping. These are host results only.
- Still blocked by authentication: firmware and input inventory, exact official rm2 SDK selection, cross compilation/deployment, physical Marker coordinate mapping, E Ink rendering/ghosting, tablet Qt SQLite driver, tablet Tesseract/English data, real handwritten submissions and persistence across restart.

## Authenticated follow-up — 2026-10-27

Authentication succeeded using the user-provided password interactively. No password was saved in the workspace. Earlier authentication blockers above are historical.

### Device inventory

- Hardware: `/sys/devices/soc0/machine` reports `reMarkable 2.0`; architecture `armv7l`.
- `/etc/os-release`: Codex Linux `5.8.203 (scarthgap)`, `IMG_VERSION="3.28.0.172"`. The Settings screen version has not been independently observed.
- Official SDK selected from https://developer.remarkable.com/links: `remarkable-production-image-5.8.203-rm2-public-x86_64-toolchain.sh`, release 3.28.0.172. URL: https://storage.googleapis.com/remarkable-codex-toolchain/3.28.0.172/remarkable-production-image-5.8.203-rm2-public-x86_64-toolchain.sh . Installed at `/tmp/remarkable-rm2-sdk` on this host.
- SDK installer SHA-256: `e2daf17a86d375aae6d290af993060f3dfb488568ca83c401eaa47869e975179` (locally computed, not independently publisher-verified).
- `event0`: `30370000.snvs:snvs-powerkey`; `event1`: `Wacom I2C Digitizer`; `event2`: `pt_mt` capacitive touch.
- `evtest /dev/input/event1` confirms `BTN_TOOL_PEN`, `BTN_TOOL_RUBBER`, `BTN_TOUCH`, stylus buttons; ABS_X 0–20966, ABS_Y 0–15725, ABS_PRESSURE 0–4095. Axis orientation is still unverified.
- Qt runtime is 6.10.3. `/usr/lib/plugins/platforms/libepaper.so` and `/usr/lib/plugins/scenegraph/libqsgepaper.so` exist. No 3.17 plugin replacement was applied.

### Build and runtime findings

The matched SDK failed to compile the original `event.time` access with `_TIME_BITS=64` on ARM32. `src/PenInput.cpp` now uses the Linux header's portable `input_event_sec` and `input_event_usec` accessors. The cross build and fresh host build succeed afterward.

The stock tablet has `libQt6Sql`, but no usable QSQLITE driver. The initial tablet ink suite reproduced the failure (`available drivers:` empty). No Tesseract executable or `eng.traineddata` was found in the full filesystem search. Built privately:

- Qt 6.10.3 SQLite plugin from the matching Qt source, linked to system SQLite.
- Tesseract 5.5.1 and static Leptonica 1.85.0. Leptonica WebP/OpenJPEG/GIF support disabled after a first deployment exposed missing `libwebp.so.7`; PNG input remains enabled. No network OCR.
- English data from https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/eng.traineddata (SHA-256 `7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2`).

Deployed runtime: `/home/root/learning-runtime/{bin/tesseract,plugins/sqldrivers/libqsqlite.so,share/tessdata/eng.traineddata}`. Host copies are in `build-rm2/runtime/`. Executables: `/home/root/remarkable_learning`, `/home/root/pen_test`, `/home/root/learning-ink-test`.

With `PATH=/home/root/learning-runtime/bin:$PATH`, `TESSDATA_PREFIX=/home/root/learning-runtime/share/tessdata`, and `QT_PLUGIN_PATH=/home/root/learning-runtime/plugins`:

- `tesseract --version`: 5.5.1; `--list-langs`: `eng`.
- Tablet `QT_QPA_PLATFORM=offscreen /home/root/learning-ink-test`: 6 passes, zero failures, including setup/cleanup. This verifies synthetic strokes, grading, SQLite writes, and local synthetic `56` OCR.
- Tablet `QT_QPA_PLATFORM=offscreen /home/root/pen_test`: 3 passes, zero failures, including setup/cleanup. This verifies synthetic event mapping only.
- Host CTest after the source fix: 2/2 suites pass.

### Display launch and recovery

First launch followed the official `systemctl stop xochitl` sequence. The stock UI segfaulted during shutdown (`status=11/SEGV`), causing `remarkable-fail.service` to reboot the tablet. Previous-boot journal confirmed this causal chain; it does not prove the learning app crashed. Stock UI was active after reboot.

A temporary drop-in with empty `OnFailure=` did not clear the dependency on this system and was removed. For the second launch, `systemctl mask --runtime remarkable-fail.service` prevented the shutdown failure handler from rebooting during testing. This modifies `/run` only. The test shell restores the stock UI and unmasks the handler on exit:

```sh
systemctl mask --runtime remarkable-fail.service
trap 'systemctl reset-failed xochitl; systemctl start xochitl; systemctl unmask --runtime remarkable-fail.service' EXIT HUP INT TERM
systemctl stop xochitl
export PATH=/home/root/learning-runtime/bin:$PATH
export TESSDATA_PREFIX=/home/root/learning-runtime/share/tessdata
export QT_PLUGIN_PATH=/home/root/learning-runtime/plugins
export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=rotate=180:invertx
QT_QUICK_BACKEND=epaper /home/root/remarkable_learning -platform epaper
```

Second launch remains running at the latest inspection. `/proc/<pid>/fd` confirms the app opened `/dev/fb0`, E Ink locks, `/dev/input/event1`, and touch `event2`. It emitted three `Could not open bin file for reading` messages; their effect on the physical display remains unverified. Opening devices is not proof of correct rendering or mapping.

Still awaiting physical observation: question and controls rendering, Marker alignment/orientation, touch controls without finger drawing, redraw latency/ghosting, handwritten `56` feedback, wrong/unreadable submissions, database contents after those submissions, persistence after an app restart, and final stock-UI restoration. A later readback found `/home/root/.local/share/remarkable_learning/attempts.sqlite` with three real submissions: IDs 1–3, empty recognized text, correct=0, each preserving four strokes / 608 points with coordinates, pressure and timestamps. These repeat submissions contain the same stroke sequence. Reconstructing the stored ink shows sideways digits and two small additional marks. This establishes physical evdev capture and persisted submissions, but does not establish correct coordinate mapping or successful real OCR. A copy for inspection is `/tmp/remarkable-hardware-attempts.sqlite`; the original is retained on the tablet.

## Hardware checks

1. Record `Settings → Software` version and `cat /etc/os-release`; pick the exact `rm2` SDK from the [official SDK list](https://developer.remarkable.com/links).
2. Run `cat /proc/bus/input/devices` and identify the Marker event device by name and capabilities. Confirm `ABS_X`, `ABS_Y`, `ABS_PRESSURE`, `BTN_TOUCH`, and axis min/max via an event inspection tool or a small evdev probe. Record the event path and orientation.
3. Cross compile and launch the app with the [official Qt Quick instructions](https://developer.remarkable.com/documentation/qt_epaper). Confirm the question, writing region, buttons, and feedback render on E Ink. Keep a shell available so `systemctl start xochitl` can restore the stock UI after testing.
4. Write `56` with the Marker. Confirm two or more strokes are visible and a finger does not draw. Check if the pen appears where it touches; adjust the documented mapping environment variables if needed. Check redraw speed and ghosting.
5. Check `command -v tesseract`, `tesseract --list-langs`, and the Qt SQLite driver on the tablet. Submit `56`, then a wrong answer and an unreadable mark. Verify feedback and query the local SQLite `attempts` table; confirm distinct rows preserve raw point arrays, pressure, times, and OCR output.
6. Exit and restart `xochitl`. Reopen the app and verify previous attempts remain in SQLite.

If pen input fails, the official guide does not specify a Marker API. Record the device events and Qt behavior before changing the adapter. If OCR fails, retain the strokes and investigate a tablet compatible Tesseract package or a later sync recognition service. Do not add learning features until the `56` flow is verified on hardware.

### Portrait calibration follow-up

User confirms the question/answer box is visible on the physical panel, the reboot occurred without manual intervention, and writing upright is rotated 90 degrees. Relaunched with `RM_PEN_SWAP_XY=1 RM_PEN_INVERT_Y=1`, pending physical confirmation. These are per-device launch settings, not a universal default for other firmware or hardware.

A manual launcher is provided at `scripts/run-rm2.sh`. It selects the private runtime, applies the current portrait calibration candidate, temporarily masks the reboot handler, and restores the stock UI and any mask it created on normal/signal exit. Its syntax was checked with `sh -n`; its full cleanup path has not yet been exercised as a script. The equivalent commands have restored xochitl and removed the runtime mask between display runs. An SSH disconnect or power loss still requires checking the device state; do not assume shell traps ran.

To build the app again with the installed SDK:

```sh
. /tmp/remarkable-rm2-sdk/environment-setup-cortexa7hf-neon-remarkable-linux-gnueabi
cmake -S . -B build-rm2 -G Ninja
cmake --build build-rm2
```

Sources used for the private dependencies:

- https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz
- https://github.com/tesseract-ocr/tesseract/tree/5.5.1
- https://github.com/DanBloomberg/leptonica/tree/1.85.0

With the SDK environment sourced, build Qt's `src/plugins/sqldrivers` separately with CMake and `-DFEATURE_system_sqlite=ON`. Build Leptonica with `BUILD_SHARED_LIBS=OFF`, `BUILD_PROG=OFF`, `ENABLE_WEBP=OFF`, `ENABLE_OPENJPEG=OFF`, `ENABLE_GIF=OFF`, then install to a host staging directory. Point Tesseract's `Leptonica_DIR` to that directory's `lib/cmake/leptonica`, set `BUILD_SHARED_LIBS=OFF`, `BUILD_TRAINING_TOOLS=OFF`, `BUILD_TESTS=OFF`, `DISABLE_ARCHIVE=ON`, `DISABLE_CURL=ON`, `OPENMP_BUILD=OFF`, and `LEPT_TIFF_RESULT=0` (the configured Leptonica does include TIFF; this satisfies its cross-build run check). Copy only the resulting OCR executable, SQLite plugin, and English data to the private runtime paths above.

### Confirmed rotation and first handwriting failure

The user confirms that `RM_PEN_SWAP_XY=1 RM_PEN_INVERT_Y=1` makes ink follow the Marker tip. The visible ink delay was estimated by the user at about 1.5 seconds (not instrumented). Check displayed unreadable feedback. Database attempt 7 reconstructed as upright `56` plus a third stray stroke around canvas y=1017–1024, outside the answer area; this expanded OCR bounds and shrank the actual digits.

Added a regression test proving contacts begun outside the canvas must be ignored even if they move into it while still down; it failed before the fix. The pen adapter now only starts strokes inside the answer box and ends capture when a stroke exits it, requiring a new contact before resuming. The canvas now invalidates only a new point/segment's bounding rectangle (with antialiasing margin), rather than the entire canvas on every point or pen-up. This is a latency experiment, not a measured speed claim.

After these changes: host CTest 2/2 suites pass; cross build succeeds; tablet InkTest 6/6 and PenTest 4/4 (including setup/cleanup) pass. The revised app was launched with `/home/root/run-learning.sh`; physical retest of latency and fresh handwriting is pending. Original failed attempts were preserved.

### OCR mode and monochrome rendering follow-up

After the boundary/partial-update build, the user reports shorter delay but an outline-then-fill E Ink effect. A fresh `56` produced wrong-answer feedback; attempt 9 stored two strokes / 430 points and recognized `1`. Preserved these actual Marker points as `tests/fixtures/marker-56.json`. The new replay regression failed before changing Tesseract page segmentation from line mode 7 to raw-line mode 13.

Mode 13 passed the host synthetic and captured fixtures. On the tablet, the initially deployed `tessdata_fast` English data passed the captured fixture but misread the synthetic fixture as `6`. Replaced it with the exact fuller English trained data already installed on the host (`/usr/share/tessdata/eng.traineddata`, about 23 MiB). SHA-256: `daa0c97d651c19fba3b25e81317cd697e9908c8208090c94c3905381c23fc047`. This supersedes the smaller model recorded earlier. Both test suites now pass on the tablet: InkTest 7/7, PenTest 4/4, including setup/cleanup. Host CTest 2/2 also passes after the final source changes.

The launcher sets `RM_MONO_INK=1`, which disables antialiased gray edges only for screen ink; the OCR raster remains antialiased. This is an experiment to reduce grayscale refresh artifacts, not a verified waveform/latency fix. The updated app is running for physical confirmation. A static code review found no critical/important issues; it identified missing test coverage for a stroke leaving and re-entering the canvas before lift.

### First successful physical 56 — 2026-10-27

User reports “Great work!” after writing a fresh `56` with the Marker and tapping Check. Device SQLite row **10**, timestamp **2026-10-27T23:03:41.508Z** (18:03:41 America/Chicago), independently confirms recognized=`56`, correct=`1`, two strokes, **457 points** with pressure and timestamps preserved. This is the first verified physical end-to-end success, not a replay or injected event sequence.

Compared database rows 1–9 after multiple app/stock-UI restarts: all values were unchanged. The user reports the outline-then-fill effect remains, with a slight improvement from monochrome ink. Partial repaint improved the original estimated 1.5-second lag, but final latency has not been measured and the artifact is unresolved. No claim of native writing latency or ghost-free rendering.

Intentional wrong-answer/unreadable-mark checks and final stock-UI restoration are in progress. Correct row 10 will be checked again after restart.

### Final grading, restart, and restoration

User intentionally wrote **54** (rather than the suggested 55) and saw retry feedback. Row **11** confirms recognized=`54`, correct=`0`, three strokes / **365 points**. An unreadable scribble showed unreadable feedback; row **12** confirms empty recognized text, correct=`0`, one stroke / **200 points**. These are physical submissions.

Stopped the app, restored xochitl, reopened the app using the manual launcher, read back SQLite, then stopped it again. All **12 rows**, including successful row 10 and incorrect/unreadable rows 11–12, were byte-for-byte equal at the SQL value level before and after the final app restart. This verifies persisted point arrays, pressure, times, OCR output and grading across restart.

Final cleanup check confirms xochitl active, no learning app process, `remarkable-fail.service` loaded/static rather than masked, xochitl's original OnFailure handler present, and both temporary runtime override paths absent. No persistent service changes, boot installation, document-store modifications, lessons, sync, or dashboard were added.

**Verified hardware outcome:** physical question rendering and touch controls, corrected Marker alignment, real local `56` recognition and correct feedback, intentional `54` incorrect feedback, unreadable feedback, SQLite preservation across app restart, and stock UI restoration.

**Remaining limitations:** writing still visibly lags and shows outline-then-fill; monochrome ink is only slightly better by user report. Final latency/ghosting were not instrumented. Finger-only drawing exclusion was not separately stress-tested beyond touch-button use and disabling the canvas mouse path. Eraser behavior is not validated. Settings-screen version was not independently read; firmware matching used IMG_VERSION and OS version. The SDK and build intermediates under `/tmp` are temporary, while deployable runtime copies remain under `build-rm2/runtime/` and on the tablet. No SSH key was installed; future sessions still require the tablet's existing authentication setup.

## Ink refresh investigation — 2026-10-27

The earlier latency/outline limitations above describe the original Content-mode renderer and are superseded by this test for the verified firmware.

### Baseline and cause investigation

Enabled the installed backend's own diagnostics with `QT_LOGGING_RULES="rm.framebuffer.updates.debug=true;rm.renderer.debug=true"`. With a short physical line, dirty rectangles were already small and renderer work was about 2–12 ms; repaint submission alone did not account for the visible delay. Startup's initial full-screen refresh took about 1.6 seconds and is distinct from live writing.

A read-only metadata probe of the installed library established:

- `EPScreenModeMap::uiScreenMode` is an exported four-byte integer, initially **4**.
- `EPScreenModeItem::Mode`: Pen=0, Mono=1, Animation=2, UI=3, Content=4, Sleep=5.
- The default is therefore Content, even for this black-and-white exercise. The earlier `RM_MONO_INK` drawing option only removes gray edge pixels; it does not select a panel refresh mode.

### Controlled tablet comparisons

1. Same drawing code, process-local default changed to **Mono**: user reports “Lag much better, but still seeing the outline.” This improved delay but did not resolve the artifact.
2. Same drawing code, mode changed to **Pen**: trace explicitly switches to `pen update` regions. User tested drawing, Try again, fresh `56`, and Check, then reports **“That looks perfect.”**
3. SQLite row **14** independently confirms Pen-mode recognized=`56`, correct=`1`, two strokes / **390 points**, timestamp `2026-10-27T23:12:42.660Z`. Mono-mode row 13 recognized `9` for that attempt; this work makes no claim that OCR is reliable for every handwriting sample.

Pen trace retained at `build-rm2/refresh-pen.log` on the host: 90 renderer frames, 90 pen-update entries; initial frame ~1589 ms. The other 89 frames were below 100 ms (median ~5.6 ms, maximum ~25.5 ms). These are renderer timings, **not** measurements of input-to-visible-ink latency; the physical comparisons used different handwriting and are not a benchmark.

### Implementation and boundary

`src/EpaperRefresh.cpp` resolves the installed backend's exported default-mode integer and Qt enum metadata before QML creates the scenegraph. It validates 32-bit architecture, hardware name `reMarkable 2.0`, exact `IMG_VERSION="3.28.0.172"`, exported symbols, and expected enum values before changing its own process's setting. The library remains loaded. No tablet library, waveform file, stock UI, boot service or persistent system configuration is modified.

This is a **private vendor ABI**, observed directly in the matching SDK/device library, not an API promised by the official Qt Quick guide. Other firmware must be investigated again. A failed compatibility check logs a warning and retains the backend default. The manual launcher now selects `RM_EPAPER_UI_MODE=pen`; `mono` and `content` remain explicit comparison/rollback options. Running the app without this variable leaves the backend unchanged.

### Verification and cleanup

- Added a real on-device regression that failed before implementation, then verifies Mono=1, Pen=0, Content=4 and restores the original value. Unknown mode is rejected. The backend-specific case skips on a host without the vendor library.
- Final host CTest: **3/3 suites pass**. Matching SDK cross build succeeds.
- Final tablet: InkTest **7/7**, PenTest **4/4**, EpaperTest **4/4** (counts include setup/cleanup), all passing.
- Reopened the final app via the default launcher, confirmed its log selects Pen, and compared SQLite: **all 14 rows unchanged** across restart.
- Stopped the app; xochitl confirmed active. Reboot handler is loaded/static, runtime mask removed, no learning app process remains.
- Static review found no important issues in initialization ordering, guards, process-local behavior, or launcher recovery.

Remaining validation boundaries: no instrumented pen-to-screen timing, no long-session ghosting/stress test, no eraser validation, no other firmware/device validation. Scope remains the one exercise.

## Repeated writing and clearing — 2026-10-27

Ran the unchanged single exercise on the same rm2 firmware 3.28.0.172 in Pen mode, with backend diagnostics. Asked the user to write a large `56`, tap the left-hand Try again control, and repeat ten cycles, alternating positions and leaving the final canvas clear. User reports **“No ghosting”** and confirms **“Yes, stayed responsive”** when asked whether responsiveness remained at its starting level and the outline-then-fill effect stayed absent.

The preserved trace is `build-rm2/refresh-repeat.log`: 623 renderer frames, all 623 classified as pen updates. It contains 12 large answer-area redraws (including setup/feedback-related clearing); these are not an independently counted set of physical cycles. Large answer redraws took about 27–51 ms. The other 610 redraws had a median of 6.55 ms and maximum 18.91 ms. First and last 50 such redraws had nearly identical medians, 7.057 ms and 7.0605 ms. This supports stable renderer work during this session; it does **not** measure pen-to-visible-ink latency.

During setup, the user tapped Check on a fresh handwritten `56` and reported retry feedback. SQLite rows 15 and 16 both contain recognized=`9`, correct=`0`; row 16 retains two strokes / 375 points. This is an observed OCR reliability limitation, separate from the passing ghosting/responsiveness check. No OCR tuning or product code changes were made during this testing task. Unsubmitted write/clear cycles are intentionally not inserted into attempts.

Finished by stopping the exercise. Confirmed xochitl active, reboot handler loaded/static, temporary runtime mask absent, and no remaining learning app process. This is a **ten-cycle visual check**, not proof of ghost-free operation for prolonged sessions or different temperatures/devices.

The submitted-attempts database SHA-256 matched the pre-cycle snapshot after the repeated clearing: `fdbe69ef0b01c15fd73bf531385450a2bbc56d2d311da07ebf2bca03f1fef654`. Clearing did not alter already submitted data.

## OCR 56-as-9 correction — 2026-10-27

Replayed the saved strokes from attempts 15/16 and reproduced the failure before changing production code. Those two submissions share the same stroke sequence; the shipped regression keeps one copy (attempt 16). Added earlier actual `56` samples (rows 10 and 14), actual wrong `54` (row 11), and an unreadable scribble (row 12), alongside the existing row-9 fixture and synthetic test. Fixture provenance is documented in `tests/fixtures/README.md`.

### Evidence and change

The original 600×240 raw-line image produced `9` with confidence **0** for the failed sample. A tight ink crop with a 20-pixel white border, recognized in normal single-line mode 7, produced `56` with confidence **63.93** on the host. The second view alone is not sufficient: it read earlier row 14 as `5` with confidence 0 while raw-line mode 13 read that row as `56` with confidence 49.25. Earlier row 10 and actual `54` were also preserved by confidence ranking. Tesseract's [official image-quality guidance](https://tesseract-ocr.github.io/tessdoc/ImproveQuality.html#borders) describes excessive white borders as an OCR problem; the concrete diagnosis here comes from replaying these exact images.

The app now runs two local views (original raw-line and cropped single-line), parses Tesseract word-level TSV output, and selects the result with higher character-weighted confidence. This selection does not reference `56`, the exercise, or correctness. It does not require exactly two digits. Both passes share a ten-second execution budget and failed/timed-out processes are killed and reaped. TSV is enabled through `tessedit_create_tsv=1`, avoiding a dependency on extra Tesseract config files absent from the private runtime. Original strokes remain unchanged in SQLite; grading still happens only after recognition.

### Regression results before fresh handwriting

Host CTest **3/3** suites pass. Matching SDK cross build succeeds. Tablet InkTest **12/12**, PenTest **4/4**, EpaperTest **4/4**, including setup/cleanup, all pass. Ink replays check exact stored recognition and grading, including `54` and unreadable negative samples. Static review found no important correctness issues. Tesseract confidence is a ranking signal, not a guarantee of correctness; this does not establish broad handwriting accuracy.

The corrected app was tested with fresh handwriting; the results and remaining misses are recorded below. Recognition remains entirely on device; no server, network OCR, lessons, sync, or dashboard was added. Check performs two recognizer passes and can take a few seconds longer.

### Fresh handwriting results and intended architecture

Fresh submissions under the correction:

- Rows **20–22** recognized `56`, correct=1; each stores two strokes with **342**, **388**, and **356** points respectively. User reports the flow looks working.
- Rows **17–18** read `6` (same stroke sequence submitted twice, one stroke / 219 points); row **19** read `55` (two strokes / 287 points). On clarification, the user said these were unintentional/bad-handwriting cases, so they remain **misses for intended 56**, not intentional negative examples. There are three successful distinct fresh 56 samples and two other distinct fresh intended-56 samples that were misread; the duplicate submission is not independent evidence.
- Intentional single `9` in row **23** recognized `9`, correct=0, one stroke / **238 points**, with retry feedback confirmed by the user. The recognizer has not simply been forced to output two digits or the expected answer.
- All original **16 rows** matched the pre-change baseline exactly when fresh rows were read back. Historical failed attempts were not overwritten or regraded.

The saved 56-as-9 regression is corrected and all automated tests pass, but **local Tesseract is still not reliable for every intended handwritten 56**. No broad OCR accuracy claim is warranted by this small test set.

The user clarified the intended product architecture: **a server sends lessons/exercises to the tablet; the tablet returns the captured work to the server for recognition and grading**. The current offline Tesseract path is a prototype, not the final grading authority. The next server milestone can use only this same 7×8 exercise; no server, lessons, synchronization, credentials, or API calls were implemented during this OCR task.

The test app was stopped and the launcher restored the stock UI and removed its temporary reboot-handler mask. Final service-state verification is recorded below.

Final check: xochitl active; reboot handler loaded/static; runtime mask absent; no learning app process. All 23 saved rows persisted after stopping the app; the tablet database matched the final readback SHA-256 `79c66e088bbbc975a52021463f92a90aac582c83b5b5dc65f73f467301a24f7a`.

## Verified server round trip — 2026-10-27

Implemented the user-authorized single-exercise server architecture. Python service on the host binds 127.0.0.1:8787; USB SSH reverse forwarding exposes it on tablet loopback 127.0.0.1:18787. The actual tablet listener was confirmed as 0100007F:4963 in /proc/net/tcp. No public listener or permanent service was installed.

### Recognition and grading boundary

The server returns only exercise ID `multiply-7-8-v1` and prompt `7 × 8 = ?`. The Qt client fetches these before accepting input and posts raw stroke snapshots asynchronously. The server renders the strokes to PNG and calls OpenAI Responses using **gpt-5.4-mini**, image input, a strict digits-only JSON schema, and `store:false`. The model sees only ink and transcription instructions, never the exercise or expected answer. The server grades the recognized string against its own fixed answer. The tablet displays the returned feedback and does not fall back to Tesseract in server mode.

The API key was read from host .env.local without displaying it or copying it to the tablet. Initial OpenAI HTTP401 was resolved when the user replaced the key. API errors return generic retry messages; upstream bodies and credentials are not returned. The saved misread-56 sample was recognized correctly via the real API; saved actual54, scribble, and9 were also correctly transcribed/graded.

### Fresh physical submissions

The user confirmed the expected feedback for all three cases. Cross-checked tablet `remote_attempts` against server `server_attempts`, matching exact point arrays by attempt ID:

| Attempt ID | Server transcription | Grade | Captured strokes / points |
| --- | --- | --- | --- |
| da882395-999d-49fc-b461-d590a2e81e22 | 56 | Correct; Great work! | 2 / 385 |
| e07542df-497a-4914-a524-9ee16c86684b | 54 | Incorrect; retry feedback | 2 / 375 |
| e8c71c81-5240-49ea-9bb6-a6f35e43eabf | empty | Unreadable feedback | 1 / 249 |

### Failure, restart, and retry on hardware

Stopped the real host server. The user cleared, wrote a fresh56, and pressed Check; confirmed the saved/retry message. Tablet SQLite retained pending attempt **6464dcd4-4811-4014-b092-3416340a6a75**, two strokes / **373 points**, with no correctness value. Then stopped the app, restored the server, and reopened server mode. The user confirmed the saved writing was visible and pressed Check without rewriting.

Readback confirms the **same ID**, exact unchanged local snapshot, matching server point arrays, recognized56, correct1, and Great work feedback. Server contains **exactly one row** for this ID. All four remote attempts are graded; all **23 historical local-only attempts** remain unchanged. Repeated same-ID submissions are also tested to return a cached result without another recognizer call; conflicting payload reuse is rejected.

### Tests and review

- Host CTest **4/4 suites pass**, including local TCP client integration tests; Python server **6/6 tests pass**.
- Matched SDK build succeeds. Tablet InkTest **12/12**, PenTest **4/4**, EpaperTest **4/4**, ClientTest **5/5** pass (counts include setup/cleanup).
- Client tests exercise async prompt/result flow, exact stroke storage, double-Check guard, temporary failure, restart and same-ID retry.
- Final review found permanent HTTP400 rejections could keep the canvas locked. Added a failing regression, then fixed it: rejected snapshots remain preserved as `rejected`, without assigning a grade, and Try again becomes available. The full suite passes after this fix. No other important review findings.

### Scope and limits

This is a manually launched **host-computer/USB prototype**, not cloud hosting, production authentication, a lesson catalog, or continuous synchronization. Offline submissions are preserved for explicit Check retry; no background sync or automatic resend was added. One pending answer locks editing until retry, avoiding feedback being attached to changed strokes. API calls require host internet and credits. Accuracy and latency are validated only on these samples; no broad handwriting-accuracy or production-readiness claim.

Launch instructions are in server/README.md. The private SQLite/pen/refresh runtime is reused; .env.local remains host-only. The stock UI was restored after physical tests; final restoration checks are recorded below.

Final state: xochitl active; reboot handler loaded/static; runtime mask absent; no learning app process. Temporary host server stopped and USB reverse tunnel closed. No persistent services or system configuration changes remain.

## Three-question server sequence — 2026-10-27

User approved fixed order **7×8, 6×7, 8×9**, with correct-only automatic advancement after brief feedback, saved progress and an All done completion screen. Recognition stays server-side, with no question or expected answer supplied to the vision model. The server remains one shared learner/sequence over the existing loopback/USB setup.

### Implementation and automated checks

The additive `lesson_progress` row for `multiplication-three-v1` starts independently of historical single-exercise grades. Correct grading and one-step advancement share a SQLite transaction. Wrong/unreadable answers do not advance. Cached duplicate results return before any progress update; stale/future new submissions are rejected.

The tablet briefly holds correct feedback for one second, then fetches authoritative server state and clears the previous ink. It displays Question N of 3, and hides the writing area and buttons when complete. Failed next-question loads retry loading, not old submissions. A saved earlier answer is reconciled with its original ID even if the server has already advanced/completed; it is shown as Your saved answer until reconciled.

Host Qt **4/4 suites** and Python server **9/9 tests** pass. The matching SDK builds. Tablet InkTest **12/12**, PenTest **4/4**, EpaperTest **4/4**, ClientTest **9/9** pass (includes setup/cleanup). Tests cover duplicate correct requests, wrong-answer retention, forced transaction failure, server/client restart, lost correct/final responses, failed next fetch and completion input guards. Static review found no important issues. No Git operations or key changes.

### Physical verification in progress

User confirmed submitting54 leaves 7×8 in place, then submitting56 shows correct feedback, clears ink and automatically advances to6×7. Server readback: wrong attempt `475ca416-bb42-493b-8afa-20238d22b796` recognized54/correctfalse; correct attempt `172a7a75-fb11-4a6d-8d0f-430a5a7a3bd9` recognized56/correcttrue. Saved position is1 (zero-based second question). Stopped both tablet app and host server, restarted with the same database, and reopened the tablet for a question2/resume check. Final two questions and cleanup remain pending.

All eight historical server attempts match their pre-change baseline unchanged. Completion is persistent; a fresh practice database can start another run after all pending submissions are reconciled. No lesson catalog, account system, randomization, public hosting or boot service was added.

### Completed physical sequence verification

User confirmed that after restarting **both server and tablet app**, the tablet resumed question2 (6×7). Submitting42 automatically cleared ink and advanced to8×9; submitting72 finished with **All done** and no writing/Check controls.

Final cross-check of both databases:

| Question / intent | Attempt ID | Recognized | Correct | Strokes / points |
| --- | --- | --- | --- | --- |
| 7×8, intentional wrong54 | 475ca416-bb42-493b-8afa-20238d22b796 | 54 | 0 | 3 / 411 |
| 7×8 | 172a7a75-fb11-4a6d-8d0f-430a5a7a3bd9 | 56 | 1 | 2 / 442 |
| 6×7, after restart | 2b11d46a-f8cf-4d7e-a0c7-bea7c9df8e66 | 42 | 1 | 3 / 308 |
| 8×9 | 2cd7c284-2e30-4923-8846-a570e55804cf | 72 | 1 | 2 / 300 |

Every new tablet stroke array and stored grade exactly matches its server record. `lesson_progress` is3, the durable completion position for all three questions. All23 historical local-only rows and all4 earlier tablet remote-attempt rows are unchanged; all8 earlier server rows also remain unchanged.

The stock UI is restored, no learning-app process remains, and the temporary reboot-handler mask is absent (handler loaded/static). Completion remains saved: another run using this same database shows All done. See server/README.md for starting a separate fresh practice database without deleting previous attempts. No prolonged-session or multi-user validation was added.

Temporary host server stopped and USB tunnel closed after the completed sequence test.

## Hosted timed practice deployment — 2026-10-27 (America/Chicago)

User approved mixed factors 2–12, 600 continuous counted seconds, reset after a writing gap strictly greater than 30 seconds, server processing excluded, one completion per Chicago date and five distinct dates per Monday–Sunday week. Dedicated Vultr Chicago host `137.220.56.165` (`0ac1026d-db34-4315-b8e3-3df581625d5c`) deployed at `https://learning.haydenindustries.com`; DNSimple A record added by user. Approved baseline $7.20/month including daily provider backups, before OpenAI usage/tax. No existing hosts changed.

Verified infrastructure:
- Public HTTPS `/health` returns only `{"ok": true}`; missing/wrong device credentials return 401; valid device credential returns v2 practice state.
- App runs as non-login `noah-learning`, API binds only 127.0.0.1:8787, Caddy terminates TLS; production startup requires DEVICE_TOKEN even on loopback.
- Host UFW allows 80/443 publicly; SSH limited to setup source the setup computer’s public IP. Image's initial public SSH allow rules removed. Dedicated SSH key/known-host record retained privately in ignored `.runtime/`. Initial host key accepted on first use.
- All 12 legacy server attempts copied intact. SQLite online backup/reopen integrity check passed; production restore was not performed. Daily SQLite backup timer enabled; provider backup API confirms daily 07:00 UTC schedule. Provider's first scheduled backup has not yet run.
- Tablet Wi-Fi joined using stock settings, observed dynamic 192.168.1.68/default route over wlan0. Stock Qt OpenSSL TLS backend and CA certificates present. Real authenticated HTTPS submissions reached hosted server after user was instructed to unplug USB; no reverse tunnel configured. User confirmed false OCR rejections in this test, rather than connection failures.
- Manual runtime service launches independently of SSH using `systemd-run --unit=noah-practice --collect --property=KillMode=mixed --property=TimeoutStopSec=20 /bin/sh /home/root/run-rm2-practice.sh`. No boot launch or stock-menu icon installed. Exit restored stock UI in the first session; reconnect check found xochitl active and practice inactive. Wi-Fi SSH at 192.168.1.68 refused connection, so maintenance still uses USB.
- Only the device credential is installed at `/home/root/.config/noah-learning/device-token` (600, directory700). Provider API keys remain off the tablet; Vultr key also remains off the host.

Timing/recognition validation checkpoint:
- 38 Python server/router tests pass, including injected clocks, completion vs reset ordering, Chicago midnight/week boundaries, idempotency, concurrency, late grades, restart reset, question audit context and fail-closed production authentication.
- All five Qt suites pass on host and tablet: Ink12, Pen4, Epaper4, legacy client9, timed client11 including setup/cleanup. Tablet matched SDK build uses Qt6.10.3; host Qt6.12.0. Captured-handwriting API evaluation is explicit/paid, separate from offline unit suites.
- Initial hosted mini recognizer produced nine attempts/five correct; saved clear33 read23 and99 read79/77. All actual strokes/grades retained; no grade was rewritten to improve the tally. Stronger model comparison reproduced failures without showing the question/expected answer to the model.
- Switched server default to GPT-6.1 Sol with low reasoning and1024 output-token budget. Eight saved stroke fixtures (two33, three99,54,9,scribble) evaluated twice:16/16 exact transcriptions. Approximately1.5–2.3seconds per checked image in this run. This does not establish general handwriting accuracy. Source fixtures: `tests/fixtures/server-handwriting.json`; rerun explicitly with `python3 -m server.evaluate_handwriting --repeats 2`.
- Additive `practice_attempt_context` stores question/factors before new recognition, so future wrong grades can be audited after advancing. Historical question context missing from earlier timed rows is not invented.

Fresh upgraded-model handwriting, physical >30-second reset, full real600-second completion/day credit, and hosted restart/recovery with a saved timed submission remain pending at this checkpoint. Automated tests do not establish these physical results.

Upgraded-model physical follow-up: user answered yes to the USB-unplugged fresh right/wrong/scribble check. Hosted rows show accepted22 for11×2,84 for7×12,77 for7×11; deliberate5 for3×2 rejected; scribble returned empty transcription and retry. The user then confirmed the idle timer resets. Server event `inactivity` at epoch1793153508.0131872, run `59c1a250-2432-4efa-b708-91033dc62549`, was followed by ready/600000ms. Full real ten-minute test is now in progress; no completion credited yet.

## Failed long test and four-minute recovery revision — 2026-10-27

The user reported getting to about two minutes remaining, then a disconnect reset the old ten-minute timer. Hosted event `disconnect` at2026-10-28 02:21:55.876949 UTC followed roughly13seconds without a tablet heartbeat. No server restart occurred at that time. Tablet app logs later show real Qt request timeouts; a background OS3.29.0.153 update was also staged around the test, but causation is unproven. Active firmware remained3.28.0.172 during maintenance. User declined another long test and explicitly reduced new practice runs to four minutes.

Revised behavior deployed: `target_ms=240000` for new runs, historical ten-minute targets preserved; `next_target_ms` describes starts. Network loss and server restart pause and retain earned time, automatically resume, and grant no offline time. Connected writing gaps>30seconds and explicit Stop still reset unfinished progress. Stop is a durable tablet outbox event replayed before new practice if offline. Summaries now use indexed current-run aggregates rather than parsing historical raw ink each heartbeat.

Verification:47 Python tests pass. All five host/tablet Qt suites pass; timed-client suite now has17 cases including setup/cleanup. Its transport regression withholds actual local TCP HTTP responses for14real seconds, observes multiple5-second Qt transfer timeouts, then verifies automatic ready/resume at the same3:00, with no new run or extra time. This exercises the real tablet Qt transport and client, plus separate injected-clock server tests. It is not a claim of a full physical four-minute production/Wi-Fi completion. No additional long human test was requested.

Final hosted data checkpoint after backup/integrity check:12 legacy attempts,72 timed attempts across2 runs,72 indexed grade summaries,0 credited dates. All test history retained; the failed long test was not converted to a completed day.

## Tablet launcher icon — installation in progress

Reviewed/pinned XOVI v19-23052026 ARM32 and AppLoad0.6.0 were installed without a boot hook. There was no previous XOVI installation or stock service override to overwrite. Firmware-specific resource map generated20250entries (666931bytes). Runtime XOVI service overlay activated successfully; xochitl logs confirm AppLoad main UI hooks loaded. Guarded activation temporarily masked remarkable-fail.service, then removed only that mask; xochitl is active. `tablet-launcher/` holds the Practice SVG/PNG, manifest and detached dispatcher. Physical icon-tap/Exit confirmation is pending.

The overlay survives xochitl stop/start in this boot, so Exit should restore the icon too. A reboot removes it by upstream design. No boot auto-injection was configured. Firmware updates require revalidation and a new resource map; staged3.29 is not claimed supported by this app’s private E Ink override.
