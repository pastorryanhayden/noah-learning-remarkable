# First Answer Implementation Plan

> **For agentic workers:** Implement task by task with tests before code.

**Goal:** Deliver a desktop testable Qt Quick prototype and documented reMarkable 2 deployment path for one handwritten `7 × 8` answer.

**Architecture:** A QQuickPaintedItem owns ink strokes and exposes submit/clear to QML. An evdev reader feeds the same stroke methods on tablet. Tesseract performs local OCR and SQLite records every submission.

**Tech Stack:** C++17, Qt 6 Quick, Linux evdev, Tesseract CLI, SQLite via Qt SQL, CMake.

**Spec:** `docs/superpowers/specs/2026-10-27-first-answer-design.md`

## Global Constraints

- Use Qt Quick only for the device UI.
- Keep the first question fixed at `7 × 8 = ?`.
- Preserve raw stroke points in SQLite.
- Never silently guess when OCR or marker access fails.

## Review Focus

- Empty answer: request handwriting rather than grading.
- Unreadable OCR: preserve strokes and allow retry.
- Wrong recognized answer: preserve attempt and allow retry.
- Missing pen device or Tesseract: show a visible error.
- Device coordinate bounds: keep pen points within the answer canvas.

### Task 1: Ink model and grading

Create `src/InkCanvas.h`, `src/InkCanvas.cpp`, and `tests/ink_test.cpp`. Test stroke capture, serialization, empty submission, recognition outcomes, and SQLite writes. Implement minimal code and run tests.

### Task 2: Tablet pen input and Qt Quick UI

Create `src/PenInput.h`, `src/PenInput.cpp`, `src/main.cpp`, `src/Main.qml`, and `CMakeLists.txt`. Add tests for evdev frame processing and coordinate mapping. Build and run an offscreen smoke check.

### Task 3: Setup and device validation

Write `README.md` and `docs/device-checklist.md` with the exact SDK selection and launch steps, OCR dependency, and hardware-only checks. Verify host build, tests, and the documented commands against official guidance.
