# New practice run from completion

> Execute inline with superpowers:executing-plans; final focused review.

**Goal:** A Start new practice button after All done creates another server-graded three-question run while retaining every previous run and attempt.

**Architecture:** Add practice_runs, an active-run pointer and idempotent run-start requests on the server. Bootstrap the prior singleton progress as a legacy run without rewriting historical attempts; associate new attempts through an additive run-attempt mapping. GET and new attempt payloads carry run_id, preventing stale work from grading against a later run. Tablet persists run-start request ID and previous run ID before POST, blocks duplicate taps and retries the same request after failures/restart. After acknowledgment it fetches authoritative current state and clears ink.

**Constraints:** Same sequence and model; server recognition/grading only. No destructive reset, no accounts/catalog, no permanent deployment, no key on tablet, no Git operations (.git placeholder).

**Review focus:** lost start response after server commits; duplicate/stale restart requests; stale attempts from a previous run; upgrade preservation; failed next-state fetch after run creation.

- [ ] Server RED→GREEN: new run only after completion, preserved prior rows, atomic/idempotent creation, run-bound attempts, legacy progress migration/cached replies.
- [ ] Client/UI RED→GREEN: completion button, persisted start request, double-tap guard, failure/restart retry, run IDs in snapshots, clear-on-confirmed-new-run and failed-fetch recovery. Full host suites and SDK build.
- [ ] Hardware: completed prior run → new run → first answer; restart and confirm progress/history; exercise interrupted-start recovery; final review, stock UI restoration and documentation.
