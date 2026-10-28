# Timed practice implementation and handoff

User revisions: four counted minutes; network interruption preserves earned time with automatic resume and no offline credit. Explicit Stop and connected writing inactivity>30s reset unfinished progress. Keep five distinct Chicago days per Monday–Sunday week.

- [x] Server-owned questions, transcription/grading, additive history, run/request idempotency and question audit records.
- [x] Four-minute per-run targets; preserve historical ten-minute run targets.
- [x] Indexed current-run statistics; no raw historical stroke parsing on heartbeats.
- [x] Connection/restart pauses, resumed writing window, no offline credit; connected inactivity and Stop still reset.
- [x] Tablet async authenticated client, SQLite outbox for starts/answers/Stop, automatic reconnect/ready, saved retry messages.
- [x] Dedicated approved Vultr host, HTTPS, device auth, firewall and backups; user added DNSimple record.
- [x] Original hardware pen/refresh, hosted right/wrong/unreadable and physical30s idle reset checks.
- [ ] Finish final host/tablet regressions including a14-second actual HTTP stall and automatic recovery; deploy four-minute update and verify health/history.
- [ ] Publish secret-free code to user’s fresh GitHub repository, preserve original read-only .git placeholder.
- [ ] Save complete handoff note and secret-free source snapshot in ~/Notes.
- [ ] Prepare and validate a real tablet Practice launcher icon through reversible AppLoad/XOVI integration.

The real ten-minute test failed because the old liveness rule reset earned time on a13-second transport gap. User explicitly declined repeating it. Do not request another long manual test or claim physical four-minute completion. See device-checklist chronology for exact evidence.
