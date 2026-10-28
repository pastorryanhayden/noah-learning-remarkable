import tempfile
import threading
import sqlite3
from contextlib import closing
from datetime import datetime
from zoneinfo import ZoneInfo
import unittest
import json
from unittest.mock import patch
import uuid
from pathlib import Path
from server.practice import TimedPractice

class PracticeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)/'practice.sqlite'
        self.now = 1800000000.0
        self.calls = 0
        def recognize(png):
            self.calls += 1
            self.assertTrue(png.startswith(b'\x89PNG'))
            return '4'
        self.service = TimedPractice(self.path, recognize, clock=lambda:self.now, chooser=lambda pairs:pairs[0],target_seconds=600)
        self.run = self.service.start(str(uuid.uuid4()))
        self.seq = 0
    def tearDown(self): self.tmp.cleanup()
    def event(self, kind='heartbeat', **extra):
        self.seq += 1
        return self.service.activity(dict(run_id=self.run['run_id'],seq=self.seq,kind=kind,**extra))
    def advance(self, seconds, writing=True):
        for _ in range(int(seconds//2)):
            self.now += 2
            self.event('write' if writing else 'heartbeat')
        self.now += seconds%2
    def attempt(self):
        return dict(attempt_id=str(uuid.uuid4()),run_id=self.run['run_id'],question_id=self.service.state()['question_id'],response_ms=1000,strokes=[[dict(x=1,y=1,pressure=1,ms=1)]])
    def test_initial_and_start_idempotence(self):
        self.assertEqual(self.run['remaining_ms'],600000)
        self.assertFalse(self.run['timer_running'])
        self.assertEqual(self.service.start(str(uuid.uuid4()))['run_id'],self.run['run_id'])
    def test_gap_boundary_and_late_write(self):
        self.event('write'); self.advance(30,False)
        self.assertEqual(self.service.state()['remaining_ms'],570000)
        self.now += .001
        state=self.event('write')
        self.assertEqual(state['remaining_ms'],600000)
        self.assertTrue(state['timer_running'])
    def test_disconnect_and_restart(self):
        self.event('write'); self.advance(20)
        self.now += 11
        reopened=TimedPractice(self.path,lambda png:'4',clock=lambda:self.now)
        self.assertEqual(reopened.state()['remaining_ms'],580000)
        self.assertEqual(reopened.state()['wait_reason'],'connection')
    def test_complete_and_stop_keeps_day(self):
        self.event('write'); self.advance(600)
        self.assertTrue(self.service.state()['today_complete'])
        self.assertEqual(self.event('stop')['phase'],'complete')
        self.assertEqual(self.service.state()['week_days'],1)
    def test_grade_wait_ready_and_duplicate(self):
        self.event('write'); self.advance(20)
        payload=self.attempt(); result=self.service.submit(payload)
        self.assertTrue(result['correct'])
        self.assertEqual(result['practice']['phase'],'waiting')
        self.advance(60,False)
        self.assertEqual(self.service.state()['remaining_ms'],580000)
        self.assertEqual(self.service.submit(payload),result)
        self.assertEqual(self.calls,1)
        self.assertTrue(self.event('ready')['timer_running'])
    def test_duplicate_activity_does_not_write(self):
        self.event('write'); self.advance(30,False); self.now += .001
        state=self.service.activity(dict(run_id=self.run['run_id'],seq=1,kind='write'))
        self.assertEqual(state['phase'],'ready')
    def test_pending_retry(self):
        payload=self.attempt()
        self.service.recognize=lambda png:(_ for _ in ()).throw(Exception('down'))
        with self.assertRaises(RuntimeError): self.service.submit(payload)
        self.assertEqual(self.service.state()['phase'],'waiting')
        self.service.recognize=lambda png:'4'
        self.assertTrue(self.service.submit(payload)['correct'])

    def test_recognition_concurrency_and_heartbeat(self):
        entered=threading.Event(); release=threading.Event(); results=[]
        def recognize(png):
            self.calls += 1; entered.set()
            self.assertTrue(release.wait(3))
            return '4'
        self.service.recognize=recognize
        self.event('write'); payload=self.attempt()
        worker=threading.Thread(target=lambda:results.append(self.service.submit(payload)))
        duplicate=threading.Thread(target=lambda:results.append(self.service.submit(payload)))
        worker.start(); self.assertTrue(entered.wait(3)); duplicate.start()
        self.now += 2
        self.assertEqual(self.event()['phase'],'waiting')
        self.assertEqual(self.event('ready')['phase'],'waiting')
        release.set(); worker.join(3); duplicate.join(3)
        self.assertFalse(worker.is_alive()); self.assertFalse(duplicate.is_alive())
        self.assertEqual(self.calls,1); self.assertEqual(results[0],results[1])

    def test_late_grade_does_not_change_new_run(self):
        entered=threading.Event(); release=threading.Event(); results=[]
        def recognize(png):
            entered.set(); release.wait(3); return '4'
        self.service.recognize=recognize
        payload=self.attempt()
        worker=threading.Thread(target=lambda:results.append(self.service.submit(payload)))
        worker.start(); self.assertTrue(entered.wait(3))
        self.event('stop'); new=self.service.start(str(uuid.uuid4()))
        release.set(); worker.join(3)
        self.assertEqual(self.service.state()['run_id'],new['run_id'])
        self.assertEqual(self.service.state()['question_id'],new['question_id'])
        self.assertEqual(results[0]['practice']['phase'],'stopped')
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT count(*) FROM practice_runs').fetchone()[0],2)
            self.assertEqual(db.execute('SELECT status FROM practice_attempts').fetchone()[0],'graded')

    def test_chicago_completion_date_and_week(self):
        self.now=datetime(2026,10,25,23,55,tzinfo=ZoneInfo('America/Chicago')).timestamp()
        # Reconcile the disconnected old run, then cross Monday midnight.
        self.event('write'); self.advance(600)
        self.assertEqual(self.service.state()['week_days'],1)
        self.assertTrue(self.service.state()['today_complete'])
        self.now=datetime(2026,11,2,12,tzinfo=ZoneInfo('America/Chicago')).timestamp()
        self.assertEqual(self.service.state()['week_days'],0)
        self.assertFalse(self.service.state()['today_complete'])

    def test_payload_conflict_and_unreadable(self):
        self.service.recognize=lambda png:''
        payload=self.attempt(); q=payload['question_id']
        result=self.service.submit(payload)
        self.assertFalse(result['correct'])
        self.assertEqual(result['practice']['question_id'],q)
        changed=dict(payload,response_ms=999)
        with self.assertRaises(ValueError): self.service.submit(changed)
        self.event('ready')
        with self.assertRaises(ValueError): self.event('heartbeat',age_ms=1)

    def test_wait_reason_and_ready_cannot_create_pause(self):
        self.assertEqual(self.run['wait_reason'],'')
        self.event('write'); self.advance(20)
        self.assertEqual(self.event('ready')['phase'],'active')
        self.assertEqual(self.service.start(str(uuid.uuid4()))['remaining_ms'],580000)
        result=self.service.submit(self.attempt())
        self.assertEqual(result['practice']['wait_reason'],'question')
        self.assertEqual(self.event('ready')['wait_reason'],'')

    def test_aged_write_after_reset_cannot_backfill_time(self):
        self.event('write'); self.advance(30,False); self.now += 1
        state=self.event('write',age_ms=5000)
        self.assertEqual(state['remaining_ms'],600000)
        self.advance(24,False)
        self.assertEqual(self.service.state()['remaining_ms'],576000)
        self.now += 2
        self.assertEqual(self.event()['phase'],'ready')

    def test_restart_short_gap_resets_unfinished_block(self):
        self.event('write'); self.advance(20); self.now += 1
        reopened=TimedPractice(self.path,lambda png:'4',clock=lambda:self.now)
        self.assertEqual(reopened.state()['remaining_ms'],580000)
        self.assertEqual(reopened.state()['wait_reason'],'connection')

    def test_disconnected_inflight_grade_retains_ownership(self):
        entered=threading.Event(); release=threading.Event(); results=[]
        def recognize(png):
            entered.set(); release.wait(3); return '4'
        self.service.recognize=recognize
        self.event('write'); self.advance(20); payload=self.attempt()
        thread=threading.Thread(target=lambda:results.append(self.service.submit(payload)))
        thread.start(); self.assertTrue(entered.wait(3))
        self.now += 40
        self.assertEqual(self.event()['wait_reason'],'grading')
        self.assertEqual(self.event('ready')['wait_reason'],'grading')
        release.set(); thread.join(3)
        self.assertEqual(self.service.state()['wait_reason'],'question')
        self.assertNotEqual(self.service.state()['question_id'],payload['question_id'])
        self.assertEqual(self.service.state()['remaining_ms'],580000)

    def test_completion_precedes_later_inactivity_reset(self):
        self.event('write'); self.advance(570)
        self.advance(28,False)
        self.now += 3
        self.assertEqual(self.event()['phase'],'complete')

    def test_completion_cannot_extend_past_liveness_window(self):
        self.event('write'); self.advance(580)
        self.now += 21
        self.assertEqual(self.event()['wait_reason'],'connection')
        self.assertFalse(self.service.state()['today_complete'])

    def test_delayed_completion_uses_actual_midnight_date(self):
        self.now=datetime(2026,10,25,23,49,59,tzinfo=ZoneInfo('America/Chicago')).timestamp()
        self.event('write'); self.advance(570); self.advance(28,False)
        self.now += 3
        state=self.event()
        self.assertEqual(state['phase'],'complete')
        self.assertFalse(state['today_complete'])
        self.assertEqual(state['week_days'],0)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT day FROM practice_days').fetchone()[0],'2026-10-25')

    def test_attempt_question_context_survives_advance_and_new_run(self):
        payload=self.attempt()
        result=self.service.submit(payload)
        self.assertTrue(result['correct'])
        self.event('stop'); self.service.start(str(uuid.uuid4()))
        with closing(sqlite3.connect(self.path)) as db:
            context=db.execute('SELECT question_id,prompt,factor_a,factor_b,created_at FROM practice_attempt_context WHERE attempt_id=?',(payload['attempt_id'],)).fetchone()
        self.assertEqual(context[:4],(payload['question_id'],'2 × 2 = ?',2,2))
        self.assertEqual(context[4],self.now)
        self.assertNotIn('factor_a',str(result))

    def test_failed_recognition_has_durable_question_context(self):
        payload=self.attempt()
        def fail(png):
            with closing(sqlite3.connect(self.path)) as db:
                context=db.execute('SELECT question_id,prompt,factor_a,factor_b FROM practice_attempt_context WHERE attempt_id=?',(payload['attempt_id'],)).fetchone()
            self.assertEqual(context,(payload['question_id'],'2 × 2 = ?',2,2))
            raise RuntimeError('unavailable')
        self.service.recognize=fail
        with self.assertRaises(RuntimeError): self.service.submit(payload)
        reopened=TimedPractice(self.path,lambda png:'4',clock=lambda:self.now)
        self.assertTrue(reopened.submit(payload)['correct'])
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT count(*) FROM practice_attempt_context').fetchone()[0],1)

    def test_context_migration_preserves_old_attempts_without_fabrication(self):
        payload=self.attempt(); result=self.service.submit(payload)
        with closing(sqlite3.connect(self.path)) as db, db:
            before=db.execute('SELECT * FROM practice_attempts').fetchall()
            db.execute('DROP TABLE practice_attempt_context')
        reopened=TimedPractice(self.path,lambda png:self.fail('cached attempt regraded'),clock=lambda:self.now)
        self.assertEqual(reopened.submit(payload),result)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT * FROM practice_attempts').fetchall(),before)
            self.assertEqual(db.execute('SELECT count(*) FROM practice_attempt_context').fetchone()[0],0)

    def test_stats_are_run_scoped_idempotent_and_do_not_decode_ink(self):
        first=self.attempt(); self.service.submit(first)
        with closing(sqlite3.connect(self.path)) as db:
            original=db.execute('SELECT * FROM practice_attempts').fetchall()
        raw_payload=original[0][1]
        decode=json.loads
        def guarded(value,*args,**kwargs):
            if value==raw_payload: self.fail('heartbeat decoded historical ink')
            return decode(value,*args,**kwargs)
        with patch('server.practice.json.loads',side_effect=guarded):
            self.assertEqual(self.event()['stats'],dict(attempts=1,correct=1,mean_response_ms=1000))
        self.service.submit(first)
        self.event('stop'); self.run=self.service.start(str(uuid.uuid4())); self.seq=0
        self.assertEqual(self.run['stats'],dict(attempts=0,correct=0,mean_response_ms=None))
        self.service.recognize=lambda png:'0'
        second=dict(self.attempt(),response_ms=300)
        self.service.submit(second); self.service.submit(second)
        self.assertEqual(self.service.state()['stats'],dict(attempts=1,correct=0,mean_response_ms=300))
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT * FROM practice_attempts WHERE id=?',(first['attempt_id'],)).fetchall(),original)
            self.assertEqual(db.execute('SELECT count(*) FROM practice_grade_stats').fetchone()[0],2)

    def test_stats_migration_backfills_without_changing_history(self):
        payload=self.attempt(); self.service.submit(payload)
        with closing(sqlite3.connect(self.path)) as db, db:
            original=db.execute('SELECT * FROM practice_attempts').fetchall()
            db.execute('DROP TABLE practice_grade_stats')
        for _ in range(2):
            reopened=TimedPractice(self.path,lambda png:'4',clock=lambda:self.now)
            self.assertEqual(reopened.state()['stats'],dict(attempts=1,correct=1,mean_response_ms=1000))
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT * FROM practice_attempts').fetchall(),original)
            self.assertEqual(db.execute('SELECT count(*) FROM practice_grade_stats').fetchone()[0],1)

    def test_second_completion_same_day_and_later_resets_preserve_earned_day(self):
        self.event('write'); self.advance(600)
        self.assertEqual(self.service.state()['week_days'],1)
        self.run=self.service.start(str(uuid.uuid4())); self.seq=0
        self.event('write'); self.advance(600)
        state=self.service.state()
        self.assertTrue(state['today_complete'])
        self.assertEqual(state['week_days'],1)
        self.run=self.service.start(str(uuid.uuid4())); self.seq=0
        self.event('write'); self.advance(30,False); self.now += 1
        self.assertEqual(self.event()['phase'],'ready')
        self.assertTrue(self.service.state()['today_complete'])
        stopped=self.event('stop')
        self.assertTrue(stopped['today_complete'])
        self.assertEqual(stopped['week_days'],1)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual(db.execute('SELECT count(*) FROM practice_days').fetchone()[0],1)
            runs=[json.loads(row[0]) for row in db.execute('SELECT data FROM practice_runs')]
            self.assertEqual(sum(run['phase']=='complete' for run in runs),2)

    def test_five_distinct_chicago_weekdays_and_next_week_preserve_history(self):
        for day in range(19,24):
            self.now=datetime(2026,10,day,12,tzinfo=ZoneInfo('America/Chicago')).timestamp()
            if day!=19:
                self.run=self.service.start(str(uuid.uuid4())); self.seq=0
            self.event('write'); self.advance(600)
            state=self.service.state()
            self.assertEqual(state['week_days'],day-18)
            self.assertTrue(state['today_complete'])
            self.assertEqual(state['weekly_goal'],5)
        self.now=datetime(2026,10,26,0,0,tzinfo=ZoneInfo('America/Chicago')).timestamp()
        state=self.service.state()
        self.assertFalse(state['today_complete'])
        self.assertEqual(state['week_days'],0)
        with closing(sqlite3.connect(self.path)) as db:
            self.assertEqual([row[0] for row in db.execute('SELECT day FROM practice_days ORDER BY day')],
                             [f'2026-10-{day}' for day in range(19,24)])

    def test_default_four_minutes_and_connection_pause_resume(self):
        path=Path(self.tmp.name)/'four.sqlite'
        service=TimedPractice(path,lambda png:'4',clock=lambda:self.now)
        state=service.start(str(uuid.uuid4())); rid=state['run_id']
        self.assertEqual(state['target_ms'],240000)
        self.assertEqual(state['remaining_ms'],240000)
        service.activity(dict(run_id=rid,seq=1,kind='write'))
        self.now += 8
        service.activity(dict(run_id=rid,seq=2,kind='heartbeat'))
        self.now += 40
        state=service.activity(dict(run_id=rid,seq=3,kind='heartbeat'))
        self.assertEqual(state['wait_reason'],'connection')
        self.assertEqual(state['remaining_ms'],232000)
        state=service.activity(dict(run_id=rid,seq=4,kind='ready'))
        self.assertTrue(state['timer_running'])
        self.now += 2
        state=service.activity(dict(run_id=rid,seq=5,kind='write'))
        self.assertEqual(state['remaining_ms'],230000)
        self.assertEqual(service.activity(dict(run_id=rid,seq=6,kind='stop'))['remaining_ms'],240000)

    def test_fresh_write_resumes_connection_pause_without_offline_credit(self):
        self.event('write'); self.advance(20)
        self.now += 40
        state=self.event('write')
        self.assertEqual(state['remaining_ms'],580000)
        self.assertTrue(state['timer_running'])

    def test_four_minute_block_completes_at_240_seconds(self):
        self.service=TimedPractice(Path(self.tmp.name)/'default.sqlite',lambda png:'4',clock=lambda:self.now)
        self.run=self.service.start(str(uuid.uuid4())); self.seq=0
        self.event('write'); self.advance(238)
        self.assertEqual(self.service.state()['remaining_ms'],2000)
        self.advance(2)
        self.assertEqual(self.service.state()['phase'],'complete')
        self.assertTrue(self.service.state()['today_complete'])

    def test_legacy_target_remains_ten_minutes_after_upgrade(self):
        with closing(sqlite3.connect(self.path)) as db, db:
            data=json.loads(db.execute('SELECT data FROM practice_runs').fetchone()[0])
            data.pop('target_ms')
            db.execute('UPDATE practice_runs SET data=?',(json.dumps(data),))
        service=TimedPractice(self.path,lambda png:'4',clock=lambda:self.now)
        state=service.state()
        self.assertEqual(state['target_ms'],600000)
        self.assertEqual(state['remaining_ms'],600000)
        service.activity(dict(run_id=state['run_id'],seq=1,kind='stop'))
        self.assertEqual(service.start(str(uuid.uuid4()))['target_ms'],240000)

    def test_connected_idle_deadline_before_connection_loss_still_resets(self):
        self.event('write'); self.advance(28,False)
        self.now += 20
        state=self.event()
        self.assertEqual(state['phase'],'ready')
        self.assertEqual(state['remaining_ms'],600000)

if __name__ == '__main__': unittest.main()
