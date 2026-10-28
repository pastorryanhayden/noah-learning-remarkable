# Three-question sequence implementation plan

> Use superpowers:executing-plans inline and a final focused review.

**Goal:** User-approved sequence: 7×8, 6×7, 8×9; advance automatically after correct feedback; save progress; finish with All done.

**Architecture:** Server singleton sequence progress in SQLite, independent of historical single-exercise grades. Grade and progress advancement commit atomically. Cached attempt responses never advance again. Tablet waits briefly after correct feedback, then fetches authoritative current state; it never computes the answer/order. Pending retries still use original exercise IDs and strokes.

**Constraints:** Recognition stays server-side and sees only ink. Preserve existing attempts and pending recovery. No accounts, catalog, dashboard, randomization, public hosting or boot service. Existing key authorization applies; no key copied to tablet. No usable Git repository.

**Review focus:** duplicate correct requests; lost response/restart after server advancement; stale/new question IDs; completion must disable input; failed next-question fetch must be retryable without resubmitting old strokes.

- [x] Server: RED tests for wrong/stale/completion, atomic one-step advance, duplicate/restart/lost-response recovery. Implement sequence validation and durable position. Run Python suite.
- [x] Client: RED tests for arbitrary server exercise IDs, correct-only delayed reload, wrong stays put, completion, restored pending from an earlier exercise, and failed reload recovery. Clear ink only when server changes question/completes; retain pending ink. Run Qt/Python suites, cross build.
- [x] Hardware: deploy, check wrong then correct advancement, restart midway, finish all three and verify both databases. Review, restore stock UI, close temporary processes, document results.

Server9/9 tests pass, including forced SQLite failure proving grade/advancement atomicity. HostQt4/4 suites pass. Tablet InkTest12/12, PenTest4/4, EpaperTest4/4, ClientTest9/9 pass (includes setup/cleanup). Static review found no important issues. Physical sequence test in progress. SDK compiler emits ABI compatibility notes, not build failures. No Git operations.

Physical verification complete: wrong54 stayed at7×8; correct56 advanced/cleared to6×7. Both app/server restarted and resumed6×7. Correct42 advanced/cleared to8×9; correct72 showedAllDone without writing/Check controls. Serverposition3; allnewgrades/strokes matchtablet. Historical23local+4remote+8server attempts preserved. StockUIrestored; temporary maskremoved.
