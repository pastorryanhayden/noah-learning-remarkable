from contextlib import closing
import json
import sqlite3
import tempfile
import unittest
import uuid
from pathlib import Path
from server.app import ExerciseService, OpenAIRecognizer, RecognitionUnavailable, render_strokes

FIXTURES = Path(__file__).resolve().parents[1] / 'tests/fixtures'

class ServerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / 'attempts.sqlite'
        self.calls = []
        self.digits = '56'
        def recognize(image):
            self.calls.append(image)
            return self.digits
        self.service = ExerciseService(self.path, recognize)
        self.payload = {'attempt_id': str(uuid.uuid4()), 'exercise_id': 'multiply-7-8-v1',
                        'strokes': json.loads((FIXTURES / 'marker-attempt-16.json').read_text())}
    def tearDown(self):
        self.temp.cleanup()
    def test_serves_only_fixed_exercise_without_answer(self):
        self.assertEqual(self.service.exercise(), {'exercise_id': 'multiply-7-8-v1', 'prompt': '7 × 8 = ?', 'position': 1, 'total': 3, 'complete': False, 'feedback': ''})
    def test_grades_and_preserves_exact_strokes_idempotently(self):
        result = self.service.submit(self.payload)
        self.assertEqual((result['recognized'], result['correct'], result['feedback']), ('56', True, 'Great work!'))
        self.assertEqual(self.service.submit(self.payload), result)
        self.assertEqual(len(self.calls), 1)
        with closing(sqlite3.connect(self.path)) as db, db:
            payload, status = db.execute('select payload,status from server_attempts').fetchone()
        self.assertEqual(json.loads(payload), self.payload)
        self.assertEqual(status, 'graded')
    def test_wrong_and_unreadable_do_not_pass(self):
        for digits in ['54', '9', '']:
            self.digits = digits
            self.payload['attempt_id'] = str(uuid.uuid4())
            result = self.service.submit(self.payload)
            self.assertFalse(result['correct'])
            self.assertEqual(result['recognized'], digits)
    def test_failed_recognition_is_durable_and_retryable_after_restart(self):
        def fail(image):
            with closing(sqlite3.connect(self.path)) as db, db:
                self.assertEqual(db.execute('select status from server_attempts').fetchone()[0], 'pending')
            raise RuntimeError('private upstream detail')
        service = ExerciseService(self.path, fail)
        with self.assertRaises(RecognitionUnavailable):
            service.submit(self.payload)
        self.assertTrue(self.service.submit(self.payload)['correct'])
    def test_rejects_conflicting_id_and_invalid_input_without_call(self):
        self.service.submit(self.payload)
        changed = dict(self.payload, strokes=[[dict(self.payload['strokes'][0][0], x=0)]])
        with self.assertRaises(ValueError):
            self.service.submit(changed)
        for strokes in [[], [[{'x': float('nan'), 'y': 0, 'pressure': 1, 'ms': 1}]], [[{}]]]:
            with self.assertRaises(ValueError):
                self.service.submit(dict(self.payload, attempt_id=str(uuid.uuid4()), strokes=strokes))
        self.assertEqual(len(self.calls), 1)
    def test_correct_advances_once_wrong_stays_and_restart_resumes(self):
        self.digits = '54'
        self.assertFalse(self.service.submit(self.payload)['correct'])
        self.assertEqual(self.service.exercise()['exercise_id'], 'multiply-7-8-v1')
        self.payload['attempt_id'] = str(uuid.uuid4())
        self.digits = '56'
        result = self.service.submit(self.payload)
        self.assertEqual(self.service.exercise()['exercise_id'], 'multiply-6-7-v1')
        self.assertEqual(self.service.submit(self.payload), result)
        restarted = ExerciseService(self.path, lambda _: '42')
        self.assertEqual(restarted.exercise()['position'], 2)
        self.assertEqual(restarted.submit(self.payload), result) # lost response from question 1
        self.assertEqual(restarted.exercise()['position'], 2)
        stale = dict(self.payload, attempt_id=str(uuid.uuid4()))
        with self.assertRaises(ValueError): restarted.submit(stale)
    def test_final_correct_finishes_and_rejects_new_attempts(self):
        for exercise_id, digits in [('multiply-7-8-v1','56'), ('multiply-6-7-v1','42'), ('multiply-8-9-v1','72')]:
            self.digits = digits
            payload = dict(self.payload, attempt_id=str(uuid.uuid4()), exercise_id=exercise_id)
            self.assertTrue(self.service.submit(payload)['correct'])
        state = self.service.exercise()
        self.assertTrue(state['complete'])
        self.assertEqual(state['prompt'], 'All done!')
        self.assertEqual(state['position'], 3)
        self.assertNotIn('answer', state)
        with self.assertRaises(ValueError): self.service.submit(dict(payload, attempt_id=str(uuid.uuid4())))

    def test_grade_and_progress_commit_atomically(self):
        with closing(sqlite3.connect(self.path)) as db, db:
            db.execute("CREATE TRIGGER block_progress BEFORE UPDATE ON lesson_progress BEGIN SELECT RAISE(ABORT, 'test interruption'); END")
        with self.assertRaises(sqlite3.IntegrityError): self.service.submit(self.payload)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT status,result FROM server_attempts').fetchone(), ('pending',None))
            self.assertEqual(db.execute('SELECT position FROM lesson_progress').fetchone()[0],0)

    def test_api_sees_only_ink_and_uses_validated_structured_text(self):
        captured = []
        def transport(payload):
            captured.append(payload)
            return {'status': 'completed', 'output': [{'type':'message', 'content':
                [{'type':'output_text','text':'{"digits":"54"}'}]}]}
        recognizer = OpenAIRecognizer('not-a-real-key', transport=transport)
        self.assertEqual(recognizer(render_strokes(self.payload['strokes'])), '54')
        body = json.dumps(captured[0])
        self.assertNotIn('7 × 8', body)
        self.assertNotIn('56', captured[0]['instructions'])
        self.assertFalse(captured[0]['store'])
        self.assertEqual(captured[0]['model'], 'gpt-6.1-sol')
        self.assertEqual(captured[0]['reasoning'], {'effort':'low'})
        self.assertGreaterEqual(captured[0]['max_output_tokens'], 1024)
        self.assertEqual(captured[0]['input'][0]['content'][0]['type'], 'input_image')
        for text in ['{"digits":"56abc"}', '{"digits":56}']:
            recognizer.transport = lambda p: {'status':'completed','output':[{'type':'message','content':[{'type':'output_text','text':text}]}]}
            with self.assertRaises(RecognitionUnavailable): recognizer(b'png')
        recognizer.transport = lambda p: {'status':'incomplete','output':[]}
        with self.assertRaises(RecognitionUnavailable): recognizer(b'png')

if __name__ == '__main__': unittest.main()
