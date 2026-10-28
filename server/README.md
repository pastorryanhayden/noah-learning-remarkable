# Timed practice server

The active hosted experience uses the authenticated **v2 timed practice API** at `https://learning.haydenindustries.com`. Mixed factors range from 2 to 12; the server tracks four continuous minutes and one earned day per America/Chicago date toward five days each Monday–Sunday week. Writing gaps strictly greater than30seconds while connected and Stop reset unfinished progress. Connection loss and server restarts pause and preserve earned time; reconnection resumes automatically without offline credit. Server recognition and question-display waits pause counting and the writing-gap deadline. Historical attempts and earned days remain preserved.

The dedicated production service runs as `noah-learning`, binds `127.0.0.1:8787`, and starts with `--require-device-token`; Caddy provides public HTTPS. Missing/wrong bearer credentials return 401; a valid device credential returns 200. Only `/health` is public and returns a minimal healthy response. `OPENAI_API_KEY` and `DEVICE_TOKEN` arrive through the protected systemd environment file; keys are never sent to the tablet. See [hosted launch and verified limits](../docs/hosted-practice.md) and [deployment/restore](../deploy/README.md).

## v2 timed contract

All practice requests require `Authorization: Bearer` with the device credential over HTTPS. JSON bodies are limited to 2 MB.

- `GET /v2/practice`: current run/question, phase, prompt, remaining milliseconds, timer status, today/week completion, feedback and attempt statistics.
- `POST /v2/runs`: idempotent start with UUID `request_id`. An existing ready/active run is returned without resetting it.
- `POST /v2/activity`: `run_id`, monotonic positive `seq`, `kind` (`write`, `heartbeat`, `ready`, `stop`), and optional writing `age_ms`. Heartbeats do not count as writing.
- `POST /v2/attempts`: UUID `attempt_id`, `run_id`, `question_id`, captured `strokes`, and `response_ms`. Returns recognized digits, grade, feedback and updated practice state. Retries retain the original identity and cannot grade or advance twice.

Pending ink is saved before recognition; grading releases the timing lock while state/heartbeat requests remain responsive. No expected answer or question is sent to the recognizer. Incorrect/unreadable answers retain the question. Correct answers select a new pair, avoiding an immediate exact repeat. Client `ready` resumes only after a writable question is displayed. Lost connectivity earns no offline credit.

Automated validation: `python3 -m unittest server.test_app server.test_practice server.test_routes -v` (47 tests at this checkpoint); five Qt suites, including transport-stall and durable-Stop timed-client tests, passed on host and tablet. Real Wi-Fi submissions work. The original mini recognizer made false rejections; GPT-6.1 Sol with low reasoning now passes 16/16 captured-handwriting checks. Fresh hardware right/wrong/unreadable recognition and physical >30-second reset are confirmed. The original ten-minute physical test failed on a13-second network gap; the user declined a repeat. New four-minute completion has not been physically accepted.

## Legacy three-question exercise server

This prototype serves the fixed sequence **7 × 8**, **6 × 7**, **8 × 9**, one question at a time. Wrong or unreadable answers stay on the current question. A correct answer briefly shows feedback, then the tablet automatically clears the ink and fetches the next question. After three correct answers it shows **All done!**. The tablet fetches it from the server, saves submitted Marker strokes locally, and posts a snapshot with a unique attempt ID. The server saves that snapshot, rasterizes the ink, sends only the ink image to OpenAI, and grades the returned digits against its own answer. The model is not given the question or expected answer. The tablet displays the server's feedback; server mode never falls back to local OCR.

## Run on this computer

Python 3 and Pillow are required (`python3 -m pip install -r server/requirements.txt` in your chosen environment). Keep the API key in the project-root `.env.local`, which is ignored. Do not copy it to the tablet.

```sh
./scripts/run-server.sh
```

The service binds **127.0.0.1:8787** and defaults to `gpt-6.1-sol` (low reasoning). The model can be changed with `--model`; new models need recognition validation. Server data is in `server/attempts.sqlite`. This launcher is the legacy local USB comparison path; production uses the authenticated deployment described above.

In a second terminal, establish a USB reverse tunnel (keep it open):

```sh
ssh -F /dev/null -N -o ExitOnForwardFailure=yes \
  -R 127.0.0.1:18787:127.0.0.1:8787 root@10.11.99.1
```

Then launch on the tablet from another terminal:

```sh
ssh -F /dev/null root@10.11.99.1
sh /home/root/run-learning-server.sh
# Ctrl-C restores xochitl and removes the runtime recovery mask.
```

The tablet launcher sets `RM_SERVER_URL=http://127.0.0.1:18787`. Deploy `build-rm2/remarkable_learning` to `/home/root/`, and `scripts/run-rm2-server.sh` as `/home/root/run-learning-server.sh`. It uses the previously deployed `/home/root/run-learning.sh` and private Qt SQLite runtime. Existing pen mapping, Pen refresh mode, and stock-UI recovery remain in place.

## Contract and recovery

- `GET /v1/exercise`: `exercise_id`, `prompt`, one-based `position`, `total`, `complete`, `feedback`; no answer. Completion has an empty exercise ID and the All done prompt.
- `POST /v1/attempts`: `attempt_id` (UUID), `exercise_id`, `strokes` (arrays of points with `x`, `y`, `pressure`, `ms`). The body is limited to 2 MB, 128 strokes and 20,000 finite points with bounded coordinates/pressure/times.
- Result: matching IDs, recognized digits, correctness and feedback. The server owns grading.
- The server stores progress in `lesson_progress` under `multiplication-three-v1`. Correct grade storage and one-step advancement commit atomically. Wrong/unreadable grades never advance. This is one shared learner/sequence, not a multi-user lesson system. Existing historical grades do not automatically count toward the new sequence.
- Same attempt ID + same payload returns the existing grade without advancing again; same ID + different payload is rejected. Pending records are committed before recognition; failed recognition stays retryable. A single-process lock serializes grading for this one-tablet prototype.
- On the tablet, `remote_attempts` is additive beside the historical local `attempts` table. A pending snapshot is committed before POST. During a request, drawing and clearing are disabled. On a temporary failure the preserved answer stays visible; Check retries it with the same ID, including after app restart. Permanent HTTP rejection is preserved as `rejected` without a grade and unlocks Try again.
- After a correct grade, the tablet freezes editing for one second, then fetches authoritative server progress. A lost grade response can be retried from saved ink even if the server has already advanced or completed; the saved answer is reconciled before new writing is accepted. A failed next-question fetch retries loading instead of resubmitting old ink.
- If the initial exercise fetch fails, Check retries loading. No cached or hardcoded question is silently substituted in server mode.
- The original local-only prototype remains available by launching `run-learning.sh` without `RM_SERVER_URL`.

No API key or expected answer is sent to the tablet. The OpenAI request contains a rasterized handwriting image and transcription instructions, with `store:false`. Raw strokes remain in the tablet and server databases. API failures are reported without returning upstream response bodies or credentials.

## Verification

```sh
python3 -m unittest server.test_app -v
cmake --build build
env -u QT_QPA_PLATFORMTHEME QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

The client integration test uses a local TCP server, so local sockets must be permitted. Backend-specific refresh tests skip on hosts without the vendor plugin. See `../docs/device-checklist.md` for physical evidence and remaining limits.

API implementation follows the official [image input](https://developers.openai.com/api/docs/guides/images-vision?api-mode=responses) and [structured output](https://developers.openai.com/api/docs/guides/structured-outputs?api-mode=responses) documentation.

## Saved progress and another practice run

Restarting the server or tablet resumes the current question. A completed sequence stays completed; no reset button or lesson manager is included. After completing the current sequence, a separate practice database starts a fresh run while preserving the old records:

```sh
./scripts/run-server.sh --database server/practice-run-2.sqlite
```

Use a new filename for a new run, and finish/reconcile pending tablet submissions before switching databases.

## Explicit handwriting regression

`python3 -m server.evaluate_handwriting --repeats 2` makes paid API calls over saved stroke fixtures. Expected labels are checked locally, never sent to the model. The upgraded GPT-6.1 Sol/low configuration passed16/16 at this checkpoint, including previously misread33/99, wrong54, single9 and a scribble. This is separate from the offline unit suites and does not guarantee all handwriting. Grading always compares returned digits against the stored server question.
