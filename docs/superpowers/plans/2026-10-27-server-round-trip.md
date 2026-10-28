# Single Exercise Server Round Trip Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline, with a final focused code review.

**Goal:** Serve 7 × 8 to the tablet, receive actual strokes, recognize on the server via OpenAI vision, grade on the server, and return visible feedback.

**Architecture:** A Python service on this computer binds loopback port 8787. USB SSH reverse forwarding exposes it only as tablet loopback port 18787. The Qt client fetches the exercise and submits asynchronously; both sides preserve raw strokes in SQLite. The API key stays in .env.local on the computer. Recognition sees only ink, never the question or expected answer; deterministic server grading compares recognized text to 56.

**Tech stack:** Python standard library HTTP/SQLite/urllib and Pillow, OpenAI Responses image input with structured output, Qt Network/Sql/Quick.

**Spec:** User-approved one-exercise server round trip in this conversation; no lesson catalog, sync, dashboard, public hosting, or boot service.

## Global constraints

- Keep existing local OCR prototype available when RM_SERVER_URL is absent; server mode never silently grades locally.
- Save a submitted stroke snapshot before any request; on failure preserve pending work and retry the same attempt ID, including after restart.
- Server idempotency protects repeated submissions; conflicting payload reuse is rejected.
- Network operations must not block drawing/UI; freeze answer editing only during an active submission.
- Preserve historical local attempts; add separate server-attempt tables.
- Keep verified pen mapping/refresh and manual stock-UI recovery.
- .git is an empty read-only placeholder: no Git operations or commits.

## Review focus

- Network/API failure: pending work survives and no fake grade is displayed.
- Duplicate/restarted submission: exactly one server record/result with matching payload.
- Late response/clear: feedback cannot attach to another stroke sequence.
- Malformed/oversized input or model refusal: bounded requests, no wrong grade or secret leakage.
- Wrong/unreadable answers: recognition is independent of expected 56.

## Tasks

- [x] 1. Server core + HTTP. Write failing Python tests for validation, durable pending storage, idempotency/conflict, wrong/unreadable grading, API payload separation and malformed model output. Implement and run the full Python suite.
- [x] 2. Qt server flow. Write failing Qt tests against a local fake HTTP service for fetched prompt, snapshots, success, API failure/retry, restart recovery and duplicate guards. Implement async networking and additive SQLite storage; bind prompt/status/busy in QML. Run all host tests.
- [x] 3. Live verification. Replay saved positive/negative strokes through the real server/API, build with matched SDK, verify HTTP via USB tunnel, and test fresh correct/wrong/unreadable handwriting on the tablet plus failure/retry and persistence. Restore stock UI, record precise evidence/limits, and complete focused review.

## Execution evidence

Task 1: six Python tests pass; real API correctly recognized saved 56, 54, scribble, and 9. Initial HTTP401 resolved after the user replaced the key. No key contents logged.

Task 2: four host Qt suites pass. Tablet InkTest12/12, PenTest4/4, EpaperTest4/4, ClientTest5/5 pass (including setup/cleanup). Cross build succeeds.

Final review: fixed permanent HTTP rejection locking the canvas across restart — permanentRejectionPreservesAnswerWithoutLockingCanvas RED→GREEN, full host suite4/4; tabletClientTest5/5. Rejected snapshots are retained without assigning a grade. No other important findings.

No Git commits/worktree operations: the workspace .git remains an empty read-only placeholder.

Task3: real saved-sample API checks and fresh physical56/54/scribble passed; server outage then app restart recovered the exact373-point answer, sameUUID, one server record, correct grade. All23 historical local attempts unchanged. Four remote attempts graded on both sides. Stock UI restored; final service state checked in device checklist.
