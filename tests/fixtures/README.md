# Captured Marker regression data

These are unchanged point arrays from the tablet's SQLite attempts table, captured during the single `7 × 8` hardware test on 2026-10-27. Each point retains its coordinates, pressure and time. Expected labels come from what the user intentionally wrote, not the original OCR result.

| File | Source attempt | Intended input | Original observation |
| --- | --- | --- | --- |
| marker-56.json | 9 | 56 | Read as 1 before the raw-line change |
| marker-attempt-10.json | 10 | 56 | Correctly recognized |
| marker-attempt-14.json | 14 | 56 | Correctly recognized in Pen refresh mode |
| marker-attempt-16.json | 16 | 56 | Read as 9 with zero OCR confidence |
| marker-attempt-11.json | 11 | 54 | Intentional wrong answer, correctly read as 54 |
| marker-attempt-12.json | 12 | Unreadable scribble | No digits recognized |

Attempts 15 and 16 submitted the same stroke sequence twice; only one copy is included. Replays use the real rasterization, Tesseract execution, grading and temporary SQLite storage. They assert the exact recognized text as well as feedback and correctness, so they cannot pass merely by accepting any answer as 56.

`server-handwriting.json` contains eight captured numeric-stroke cases used for the explicit paid server-recognition check. The expected labels remain in the evaluator and are never included in the OpenAI image transcription request. The upgraded server configuration passed all eight cases twice (16/16).
