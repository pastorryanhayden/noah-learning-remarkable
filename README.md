# Noah’s reMarkable multiplication practice

Mixed **2–12 times tables**, one handwritten answer at a time, for **four continuous minutes a day**. The weekly goal is five distinct days (Monday–Sunday, America/Chicago). Recognition and grading run on the hosted server at [learning.haydenindustries.com](https://learning.haydenindustries.com); the tablet uses Wi-Fi and a separate device credential.

Start practice, write with the Marker, and tap Check. Correct answers advance automatically; wrong or unreadable answers stay on the question. Stop or more than30seconds without writing while connected resets unfinished progress. A connection interruption now **pauses and preserves earned time**, resumes automatically after reconnection, and earns no offline time. Server grading waits are excluded. Submitted strokes and earlier runs remain in SQLite on both sides. A completed day is counted once and never revoked by a later reset.

**Current evidence:** real hosted/Wi-Fi answers, right/wrong/unreadable grading, calibrated pen input and Pen E Ink refresh have been tested on reMarkable2 firmware3.28.0.172. The original ten-minute physical test failed when a13-second connection gap reset progress. That failure prompted four-minute targets and recoverable network pauses; full physical four-minute completion is not claimed. Automated tests cover four-minute completion, outages, restart, durable Stop, daily/week boundaries and preserved history. Saved-handwriting evaluation passed16/16 with GPT-6.1 Sol/low; handwriting accuracy is not guaranteed for every sample.

Read [hosted operation](docs/hosted-practice.md), [deployment/recovery](deploy/README.md), and the chronological [hardware checklist](docs/device-checklist.md). The original local Tesseract and fixed three-question USB modes remain comparison paths. The AppLoad Practice icon is installed for the current boot; physical icon/Exit confirmation is pending. A reboot removes the launcher hooks; no automatic boot injection is enabled.

## Host setup

Requires Linux, CMake 3.21+, Qt 6.5+ Quick/Sql/Test development packages, Ninja or Make, and Tesseract with English trained data.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/remarkable_learning
```

On a headless host, run `env -u QT_QPA_PLATFORMTHEME QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure`.

The desktop mouse stands in for a pen; the actual pen path reads Linux input events. The app stores data at the Qt application data location (`attempts.sqlite`), not in reMarkable's document store. Each row contains the fixed question, recognized digits, correctness, timestamp, and a JSON array of strokes. Each stroke contains point coordinates, pressure, and timestamp. In standalone comparison mode, OCR uses local `tesseract` with digits whitelisted. Hosted timed mode sends ink to the authenticated server and never falls back to local grading.

## reMarkable 2 build and manual launch

1. On the tablet, read **Settings → Software** and `cat /etc/os-release` over USB SSH. Choose the matching `rm2` SDK from [reMarkable's official links](https://developer.remarkable.com/links). The SDK is tied to both product and OS version. Do not use the host Qt build on the tablet.
2. Install the matching SDK on a Linux host and source its `environment-setup-cortexa7hf-neon-remarkable-linux-gnueabi` file. Check whether it supplies Qt Quick, Qt SQL, the SQLite driver, and the E Ink plugin. Build in a fresh directory:

   ```sh
   source /path/to/rm2-sdk/environment-setup-cortexa7hf-neon-remarkable-linux-gnueabi
   cmake -S . -B build-rm2 -G Ninja
   cmake --build build-rm2
   ```

3. Check whether `tesseract` and `eng.traineddata` are installed on the tablet. They are required for local recognition; the official SDK does not establish their presence. Cross compile and install them for the tablet if absent, or defer the recognition step. Do not mistake a successful Qt build for a working OCR deployment.
4. The connected 3.28.0.172 tablet has the app and private runtime deployed. For a fresh deployment, copy `build-rm2/remarkable_learning` to `/home/root/`, the contents of `build-rm2/runtime/` to `/home/root/learning-runtime/`, and `scripts/run-rm2.sh` to `/home/root/run-learning.sh`. Run manually over USB SSH:

   ```sh
   ssh -F /dev/null root@10.11.99.1
   sh /home/root/run-learning.sh
   # Exit with Ctrl-C to restore the stock UI.
   ```

   The launcher defaults to `RM_EPAPER_UI_MODE=pen`. For comparison or rollback, run `RM_EPAPER_UI_MODE=content sh /home/root/run-learning.sh`; `mono` is also available. The override is process-local, checks the exact device/firmware and backend metadata, and leaves the backend default intact when compatibility checks fail. Firmware updates require revalidation.

   This uses the [official E Ink launch settings](https://developer.remarkable.com/documentation/qt_epaper), plus the verified portrait Marker mapping and private SQLite/OCR runtime. On this tablet, stopping `xochitl` caused it to segfault and invoke a reboot handler. The launcher temporarily masks that handler in `/run` and restores it after restarting the stock UI. No boot service is installed. See the [session evidence and recovery details](docs/device-checklist.md).
   If the pen is not detected, inspect `/proc/bus/input/devices` and run with `RM_PEN_DEVICE=/dev/input/eventN` for the verified marker device. The app discovers Wacom/pen/stylus named devices by default, but an event number is not assumed. Use `RM_PEN_SWAP_XY=1`, `RM_PEN_INVERT_X=1`, or `RM_PEN_INVERT_Y=1` only after the device coordinate test shows they are needed. On reMarkable software 3.17, the [official guide](https://developer.remarkable.com/documentation/qt_epaper) also documents a `libqsgepaper.so` copy step.

Do not install a boot service for this prototype. The official guide says only pure Qt Quick apps are supported, touch works directly, and Marker input requires separate handling. This app's evdev mapping and Pen refresh mode have been tested on the connected rm2 running 3.28.0.172; other devices and firmware require revalidation.

## Architecture

- `Main.qml`: question, ink area, feedback, countdown, weekly progress, and Start/Stop/Exit controls in timed mode.
- `InkCanvas`: draws actual strokes and routes them to the timed/legacy server client; standalone comparison mode retains local Tesseract grading.
- `PracticeClient`: authenticated asynchronous hosted requests, durable start/answer outbox, pen activity and countdown.
- `server/practice.py`: authoritative timing, random2–12 facts, grading, daily/week credit and preserved question/stroke history.
- `PenInput`: discovers a Linux pen event device, reads position/contact/pressure events, and maps them into the canvas.
- `EpaperRefresh`: selects the verified Pen refresh mode within this app process on matching rm2 firmware.
- `ExerciseClient`: asynchronously fetches the server exercise, preserves pending submissions, and displays server grading.
- `PracticeClient`: asynchronous timed state, writing reports, heartbeats, saved submissions and retry/recovery.
- `server/practice.py`: server-owned timing, run history and calendar completion.
- `server/app.py`: authenticated v2 timed API plus legacy v1, OpenAI transcription and independent grading.

The on-device `56` milestone is verified, including intentional wrong/unreadable answers, SQLite persistence across restart, and stock-UI restoration. The subsequent Pen-mode test resolves the observed lag and outline-then-fill behavior; ten write/clear cycles passed without visible ghosting or responsiveness loss. Prolonged-session behavior and instrumented latency remain unmeasured. Handwriting OCR is still variable: later handwritten `56` samples were read as `9`, so correct grading is not guaranteed for every sample. Lesson scheduling, synchronization, and a parent dashboard remain out of scope.

## Historical server flow

The intended product uses a server to send exercises/lessons to the tablet and receive the captured work back for recognition and grading. The tablet handles display, responsive Marker capture, and local preservation of strokes. The first server connection is now implemented and verified through USB SSH for this same `7 × 8` exercise. The original Tesseract implementation remains an offline comparison mode.

The local OCR correction compares original raw-line and tightly cropped single-line readings by confidence, without consulting the expected answer. It fixes the saved `56`→`9` sample, and fresh testing correctly accepted three `56` samples and rejected an intentional `9`. Two other distinct fresh intended `56` samples were still read as `6` and `55`; handwriting recognition remains imperfect. See the device checklist for exact fixtures, evidence, and test limits.

## Legacy three-question sequence

The server owns the order, expected answers, grading, and persisted progress. Grade storage and advancement are atomic; repeated delivery of a correct submission cannot skip a question. The tablet fetches the current server state after correct feedback. It clears ink for a new question and disables writing at completion. Failed fetches and saved-answer retries retain the earlier recovery behavior. See [server instructions](server/README.md) for resuming progress or using a fresh database after a completed run.

The three-question sequence has been verified physically: an intentional wrong answer of `54` stayed on `7 × 8`; correct `56` advanced to `6 × 7`. Both app and server restarted and resumed there, then correct `42` and `72` advanced to completion. Exact strokes and grades match on both sides. All previous attempts remain preserved. The standalone local comparison mode still uses the original single `7 × 8` question.
