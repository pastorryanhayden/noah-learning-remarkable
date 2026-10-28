# First answer design

The first milestone is one offline exercise on a reMarkable 2: show `7 × 8 = ?`, accept pen strokes for `56`, preserve those strokes, recognize the digits, and give immediate feedback with a retry option. The app does not include lesson authoring, sync, a dashboard, spaced repetition, or other exercise types yet.

Use the official reMarkable Qt Quick SDK and match its `rm2` release to the tablet firmware. Keep the interface monochrome and large enough for a child to use. A Qt Quick painted item stores the actual points, including timing and pressure, and renders ink. A Linux evdev adapter reads the marker because the official Qt Quick guide does not promise marker delivery. A desktop mouse path supports local development. On submission, render the strokes to an image, run local Tesseract with a digit whitelist, compare its result to `56`, and store the strokes and result in SQLite. OCR uncertainty asks the child to try again; a recognized wrong answer invites a retry without revealing the answer.

The prototype must fail visibly when pen input or OCR is unavailable. The exact marker device, coordinate transform, E Ink latency, SDK build, and Tesseract availability on the tablet require hardware verification. The host build and automated tests establish only the local behavior.
