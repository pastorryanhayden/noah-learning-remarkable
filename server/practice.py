"""Persistent server-authoritative practice, with recognition outside the timing lock."""
import json
import random
import re
import sqlite3
import threading
import time
import uuid
from datetime import datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo
try:
    from .app import validate, render_strokes
except ImportError:
    from app import validate, render_strokes

class TimedPractice:
    def __init__(self, path, recognize, clock=time.time, chooser=None, target_seconds=240):
        self.path, self.recognize, self.clock = str(path), recognize, clock
        if type(target_seconds) not in (int,float) or not 0<target_seconds<=600:
            raise ValueError('Invalid target duration')
        self.target_ms=round(target_seconds*1000)
        self.chooser = chooser or random.choice
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)
        self.inflight = set()
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        with self.db() as db:
            db.executescript('''CREATE TABLE IF NOT EXISTS practice_runs(id TEXT PRIMARY KEY, data TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS practice_starts(id TEXT PRIMARY KEY, run_id TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS practice_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS practice_attempts(id TEXT PRIMARY KEY,payload TEXT NOT NULL,status TEXT NOT NULL,result TEXT);
            CREATE TABLE IF NOT EXISTS practice_days(day TEXT PRIMARY KEY,run_id TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS practice_grade_stats(attempt_id TEXT PRIMARY KEY,run_id TEXT NOT NULL,response_ms INTEGER NOT NULL,correct INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS practice_grade_stats_run ON practice_grade_stats(run_id);
            CREATE TABLE IF NOT EXISTS practice_attempt_context(attempt_id TEXT PRIMARY KEY,question_id TEXT NOT NULL,prompt TEXT NOT NULL,factor_a INTEGER NOT NULL,factor_b INTEGER NOT NULL,created_at REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS practice_events(id INTEGER PRIMARY KEY,run_id TEXT,kind TEXT,at REAL,data TEXT);''')
            # Migrate only missing summaries. Historical payload/result bytes are
            # never rewritten, and large ink snapshots are decoded only here.
            missing=db.execute("SELECT a.id,a.payload,a.result FROM practice_attempts a LEFT JOIN practice_grade_stats s ON s.attempt_id=a.id WHERE a.status='graded' AND s.attempt_id IS NULL")
            for aid,payload,result in missing:
                attempt=json.loads(payload); grade=json.loads(result)
                db.execute('INSERT INTO practice_grade_stats VALUES (?,?,?,?)',
                           (aid,attempt['run_id'],attempt['response_ms'],int(grade['correct'])))
            r=self.current(db)
            if r and r['phase'] not in ('complete','stopped'):
                now=self.clock()
                self.pause_connection(db,r,'server_restart',now)
                r['last_seen']=now
                self.save(db,r)
    def db(self):
        return closing_connection(self.path)
    @staticmethod
    def identifier(value):
        try: uuid.UUID(value)
        except (ValueError,TypeError,AttributeError): raise ValueError('Invalid UUID') from None
        return value
    def current(self, db):
        row=db.execute("SELECT value FROM practice_meta WHERE key='current'").fetchone()
        return self.load(db,row[0]) if row else None
    def load(self,db,rid):
        row=db.execute('SELECT data FROM practice_runs WHERE id=?',(rid,)).fetchone()
        if not row: raise ValueError('Unknown run')
        return json.loads(row[0])
    def save(self,db,r): db.execute('INSERT OR REPLACE INTO practice_runs VALUES (?,?)',(r['run_id'],json.dumps(r)))
    def event(self,db,r,kind,now): db.execute('INSERT INTO practice_events(run_id,kind,at,data) VALUES (?,?,?,?)',(r['run_id'],kind,now,json.dumps(r)))
    def reset(self,db,r,kind,now):
        r.update(phase='ready',elapsed=0,last_tick=now,last_write=None,started=False,waiting_reason='',grading_attempt_id='')
        self.event(db,r,kind,now)
    @staticmethod
    def target(r): return r.get('target_ms',600000)/1000
    def pause_connection(self,db,r,kind,now):
        if r['phase'] not in ('complete','stopped'):
            # Grading and question display already exclude elapsed time; retain
            # their ownership/recovery state across a lost connection.
            if r['phase'] in ('active','ready'):
                r.update(phase='waiting',waiting_reason='connection')
            r['last_tick']=now
            self.event(db,r,kind,now)
    def reconcile(self,db,r,now):
        if not r or r['phase'] in ('complete','stopped'): return
        if r['phase']=='active':
            completed_at=r['last_tick']+max(0,self.target(r)-r['elapsed'])
            # Reconcile events in time order: a block already earned within both
            # activity windows cannot be revoked by a later observation.
            if completed_at<=now and completed_at<=r['last_write']+30 and completed_at<=r['last_seen']+10:
                self.complete(db,r,completed_at)
                self.save(db,r)
                return
        if now-r['last_seen']>10:
            if r['phase']=='active' and r['last_write']+30 < r['last_seen']+10 and now>r['last_write']+30:
                self.reset(db,r,'inactivity',now)
            else:
                self.pause_connection(db,r,'connection',now)
            r['last_seen']=now
        elif r['phase']=='active':
            if now-r['last_write']>30:
                self.reset(db,r,'inactivity',now)
            else:
                r['elapsed']+=max(0,now-r['last_tick'])
                r['last_tick']=now
                if r['elapsed']>=self.target(r):
                    self.complete(db,r,now)
        self.save(db,r)
    def complete(self,db,r,now):
        r.update(elapsed=self.target(r),phase='complete')
        db.execute('INSERT OR IGNORE INTO practice_days VALUES (?,?)',(self.date(now).isoformat(),r['run_id']))
        self.event(db,r,'complete',now)
    @staticmethod
    def date(now): return datetime.fromtimestamp(now,ZoneInfo('America/Chicago')).date()
    def view(self,db,r,now):
        today=self.date(now); monday=today-timedelta(days=today.weekday())
        days=[row[0] for row in db.execute('SELECT day FROM practice_days WHERE day>=? AND day<=?',(monday.isoformat(),(monday+timedelta(days=6)).isoformat()))]
        stats={'attempts':0,'correct':0,'mean_response_ms':None}
        if r:
            count,correct,mean=db.execute('SELECT count(*),coalesce(sum(correct),0),avg(response_ms) FROM practice_grade_stats WHERE run_id=?',(r['run_id'],)).fetchone()
            stats=dict(attempts=count,correct=correct,mean_response_ms=mean)
        return dict(next_target_ms=self.target_ms,run_id=r['run_id'] if r else '',question_id=r['question_id'] if r else '',prompt=r['prompt'] if r else 'Ready to practice.',phase=r['phase'] if r else 'stopped',wait_reason=r['waiting_reason'] if r else '',target_ms=r.get('target_ms',600000) if r else self.target_ms,remaining_ms=max(0,min(r.get('target_ms',600000),round((self.target(r)-r['elapsed'])*1000))) if r else self.target_ms,timer_running=bool(r and r['phase']=='active'),today_complete=bool(db.execute('SELECT 1 FROM practice_days WHERE day=?',(today.isoformat(),)).fetchone()),week_days=len(days),weekly_goal=5,feedback=r['feedback'] if r else '',last_event_seq=r['last_event_seq'] if r else 0,stats=stats)
    def question(self,r):
        pairs=[(a,b) for a in range(2,13) for b in range(2,13) if [a,b]!=r.get('pair')]
        pair=self.chooser(pairs)
        if pair not in pairs: raise ValueError('Invalid chosen question')
        r.update(pair=list(pair),question_id=str(uuid.uuid4()),prompt=f'{pair[0]} × {pair[1]} = ?')
    def state(self):
        with self.lock,self.db() as db:
            now=self.clock(); r=self.current(db); self.reconcile(db,r,now)
            return self.view(db,r,now)
    def start(self,request_id):
        self.identifier(request_id)
        with self.lock,self.db() as db:
            now=self.clock(); old=db.execute('SELECT run_id FROM practice_starts WHERE id=?',(request_id,)).fetchone()
            r=self.load(db,old[0]) if old else self.current(db)
            self.reconcile(db,r,now)
            if not old and (not r or r['phase'] in ('complete','stopped')):
                previous=r.get('pair') if r else None
                r=dict(target_ms=self.target_ms,run_id=str(uuid.uuid4()),phase='ready',elapsed=0,last_tick=now,last_seen=now,last_write=None,started=False,waiting_reason='',feedback='',last_event_seq=0,pair=previous)
                self.question(r); self.save(db,r)
                db.execute("INSERT OR REPLACE INTO practice_meta VALUES ('current',?)",(r['run_id'],))
            if not old: db.execute('INSERT INTO practice_starts VALUES (?,?)',(request_id,r['run_id']))
            return self.view(db,r,now)
    def activity(self,payload):
        if not isinstance(payload,dict) or set(payload)-{'run_id','seq','kind','age_ms'}: raise ValueError('Invalid activity')
        rid=self.identifier(payload.get('run_id')); seq=payload.get('seq'); kind=payload.get('kind'); age=payload.get('age_ms',0)
        if type(seq)!=int or seq<1 or kind not in ('write','heartbeat','ready','stop') or type(age)!=int or not 0<=age<=5000 or ('age_ms' in payload and kind!='write'): raise ValueError('Invalid activity')
        with self.lock,self.db() as db:
            now=self.clock(); r=self.current(db)
            if not r or r['run_id']!=rid: raise ValueError('Stale run')
            self.reconcile(db,r,now)
            if seq>r['last_event_seq']:
                r.update(last_event_seq=seq,last_seen=now)
                if kind=='stop' and r['phase']!='complete':
                    self.reset(db,r,'stop',now); r['phase']='stopped'
                elif kind=='write' and (r['phase'] in ('ready','active') or (r['phase']=='waiting' and r['waiting_reason']=='connection')):
                    r.update(phase='active',started=True,last_write=now-age/1000,last_tick=now,waiting_reason='')
                elif kind=='ready' and r['phase']=='waiting' and r['waiting_reason'] in ('question','connection'):
                    r.update(phase='active' if r['started'] else 'ready',last_write=now,last_tick=now,waiting_reason='')
                self.save(db,r)
            return self.view(db,r,now)
    def submit(self,payload):
        if not isinstance(payload,dict) or set(payload)!={'attempt_id','run_id','question_id','strokes','response_ms'}: raise ValueError('Invalid attempt')
        for key in ('attempt_id','run_id','question_id'): self.identifier(payload[key])
        if type(payload['response_ms'])!=int or not 0<=payload['response_ms']<=600000: raise ValueError('Invalid response time')
        validate(dict(attempt_id=payload['attempt_id'],exercise_id='multiply-7-8-v1',strokes=payload['strokes']))
        canonical=json.dumps(payload,sort_keys=True,separators=(',',':'),allow_nan=False); aid=payload['attempt_id']
        with self.condition:
            while aid in self.inflight: self.condition.wait()
            with self.db() as db:
                row=db.execute('SELECT payload,status,result FROM practice_attempts WHERE id=?',(aid,)).fetchone()
                if row and row[0]!=canonical: raise ValueError('Attempt ID reused')
                if row and row[2]: return json.loads(row[2])
                now=self.clock(); r=self.current(db); self.reconcile(db,r,now)
                if not r or r['run_id']!=payload['run_id'] or r['question_id']!=payload['question_id'] or r['phase'] in ('complete','stopped'): raise ValueError('Stale question')
                if r['phase']=='waiting' and (r['waiting_reason']!='grading' or not row): raise ValueError('Question not writable')
                if not row:
                    db.execute('INSERT INTO practice_attempts VALUES (?,?,?,NULL)',(aid,canonical,'pending'))
                    db.execute('INSERT INTO practice_attempt_context VALUES (?,?,?,?,?,?)',
                               (aid,r['question_id'],r['prompt'],r['pair'][0],r['pair'][1],now))
                expected=str(r['pair'][0]*r['pair'][1])
                r.update(phase='waiting',waiting_reason='grading',grading_attempt_id=aid); self.save(db,r)
            self.inflight.add(aid)
        try:
            try:
                digits=self.recognize(render_strokes(payload['strokes']))
                if not isinstance(digits,str) or not re.fullmatch('[0-9]{0,16}',digits): raise ValueError()
            except Exception: raise RuntimeError('Recognition unavailable; attempt preserved') from None
            with self.lock,self.db() as db:
                now=self.clock(); r=self.load(db,payload['run_id']); correct=digits==expected
                feedback="I couldn't read that. Try writing it again." if not digits else 'Great work!' if correct else 'Not quite. Try again!'
                if r['phase']=='waiting' and r['question_id']==payload['question_id'] and r['waiting_reason']=='grading' and r.get('grading_attempt_id')==aid:
                    if correct: self.question(r)
                    r.update(waiting_reason='question',feedback=feedback); self.save(db,r)
                result=dict(attempt_id=aid,run_id=payload['run_id'],question_id=payload['question_id'],recognized=digits,correct=correct,feedback=feedback)
                # Include this grade in the summary before caching its complete response.
                db.execute("UPDATE practice_attempts SET status='graded',result=? WHERE id=?",(json.dumps(result),aid))
                db.execute('INSERT OR IGNORE INTO practice_grade_stats VALUES (?,?,?,?)',
                           (aid,payload['run_id'],payload['response_ms'],int(correct)))
                result['practice']=self.view(db,r,now)
                db.execute('UPDATE practice_attempts SET result=? WHERE id=?',(json.dumps(result),aid))
                return result
        finally:
            with self.condition:
                self.inflight.discard(aid); self.condition.notify_all()

class closing_connection:
    def __init__(self,path): self.path=path
    def __enter__(self):
        self.db=sqlite3.connect(self.path); return self.db
    def __exit__(self,kind,value,tb):
        try:
            if kind: self.db.rollback()
            else: self.db.commit()
        finally: self.db.close()
