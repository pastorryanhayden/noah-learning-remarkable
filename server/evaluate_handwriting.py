"""Explicit, paid API regression check using only stored stroke images.

Run: python3 -m server.evaluate_handwriting --repeats 2
Expected labels are compared locally and never enter the model request.
"""
import argparse
import json
import time
from pathlib import Path
from .app import OpenAIRecognizer, load_key, render_strokes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', default='gpt-6.1-sol')
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--env-file', default='.env.local')
    parser.add_argument('--fixtures', default='tests/fixtures/server-handwriting.json')
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error('repeats must be 1–10')
    recognizer = OpenAIRecognizer(load_key(args.env_file), args.model)
    cases = json.loads(Path(args.fixtures).read_text())
    passed = total = 0
    for repeat in range(args.repeats):
        for case in cases:
            start = time.monotonic()
            digits = recognizer(render_strokes(case['strokes']))
            correct = digits == case['expected']
            total += 1
            passed += correct
            print(json.dumps(dict(case=case['name'], repeat=repeat+1, read=digits,
                                  passed=correct, seconds=round(time.monotonic()-start, 2))), flush=True)
    print(f'{passed}/{total} exact transcriptions', flush=True)
    raise SystemExit(0 if passed == total else 1)


if __name__ == '__main__':
    main()
