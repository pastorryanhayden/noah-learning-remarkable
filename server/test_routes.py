"""Exercise actual HTTP parsing without opening a listening socket."""
import io
import json
import tempfile
import unittest
import uuid
from unittest.mock import patch
from contextlib import redirect_stderr
from server.app import main
from pathlib import Path
from server.app import ExerciseService, handler_for, MAX_BODY
from server.practice import TimedPractice

class Socket:
    def __init__(self,data): self.reader=io.BytesIO(data); self.output=bytearray()
    def makefile(self,*args): return self.reader
    def sendall(self,data): self.output.extend(data)
    def settimeout(self,value): pass

class RoutesTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); path=Path(self.temp.name)/'db.sqlite'
        self.legacy=ExerciseService(path,lambda png:'56')
        self.practice=TimedPractice(path,lambda png:'4',chooser=lambda pairs:pairs[0])
    def tearDown(self): self.temp.cleanup()
    def request(self,path,method='GET',payload=None,token=None,length=None,secured=True):
        body=json.dumps(payload).encode() if payload is not None else b''
        headers=[f'{method} {path} HTTP/1.0', 'Host: localhost']
        if token is not None: headers.append('Authorization: '+token)
        if method=='POST': headers.append('Content-Length: '+str(len(body) if length is None else length))
        sock=Socket(('\r\n'.join(headers)+'\r\n\r\n').encode()+body)
        handler_for(self.legacy,self.practice,'test-token' if secured else None)(sock,('127.0.0.1',1234),object())
        head,body=bytes(sock.output).split(b'\r\n\r\n',1)
        return int(head.split()[1]),json.loads(body)
    def test_health_only_minimal_unauthenticated(self):
        self.assertEqual(self.request('/health'),(200,{'ok':True}))
    def test_all_api_routes_require_bearer(self):
        for path,method in [('/v1/exercise','GET'),('/v1/attempts','POST'),('/v2/practice','GET'),('/v2/runs','POST'),('/v2/activity','POST'),('/v2/attempts','POST')]:
            for token in [None,'Bearer wrong','Basic test-token']:
                self.assertEqual(self.request(path,method,{},token)[0],401)
    def test_v2_flow_and_idempotence(self):
        token='Bearer test-token'
        self.assertEqual(self.request('/v2/practice',token=token)[1]['phase'],'stopped')
        payload={'request_id':str(uuid.uuid4())}
        code,state=self.request('/v2/runs','POST',payload,token)
        self.assertEqual(code,200)
        self.assertEqual(self.request('/v2/runs','POST',payload,token)[1],state)
        code,state=self.request('/v2/activity','POST',dict(run_id=state['run_id'],seq=1,kind='write'),token)
        self.assertTrue(state['timer_running'])
        attempt=dict(run_id=state['run_id'],question_id=state['question_id'],attempt_id=str(uuid.uuid4()),response_ms=100,strokes=[[dict(x=1,y=1,pressure=1,ms=1)]])
        code,result=self.request('/v2/attempts','POST',attempt,token)
        self.assertEqual(code,200); self.assertTrue(result['correct'])
        self.assertEqual(self.request('/v2/attempts','POST',attempt,token)[1],result)
    def test_invalid_requests_and_size_limit(self):
        for payload in [None,[],{},dict(request_id='bad'),dict(request_id=str(uuid.uuid4()),extra=1)]:
            self.assertEqual(self.request('/v2/runs','POST',payload,'Bearer test-token')[0],400)
        self.assertEqual(self.request('/v2/runs','POST',{},'Bearer test-token',MAX_BODY+1)[0],400)
    def test_recognition_unavailable_generic_response(self):
        self.practice.recognize=lambda png:(_ for _ in ()).throw(RuntimeError('secret details'))
        state=self.practice.start(str(uuid.uuid4()))
        attempt=dict(run_id=state['run_id'],question_id=state['question_id'],attempt_id=str(uuid.uuid4()),response_ms=1,strokes=[[dict(x=1,y=1,pressure=1,ms=1)]])
        code,result=self.request('/v2/attempts','POST',attempt,'Bearer test-token')
        self.assertEqual(code,503); self.assertNotIn('secret',json.dumps(result))
    def test_legacy_local_compatibility(self):
        self.assertEqual(self.request('/v1/exercise',secured=False)[0],200)
        self.assertEqual(self.request('/unknown',secured=False)[0],404)

    def test_nonloopback_requires_token_before_loading_api_key(self):
        with patch('sys.argv',['app.py','--host','0.0.0.0']), patch.dict('os.environ',{},clear=True), patch('server.app.load_key') as key, redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error: main()
            self.assertEqual(error.exception.code,2)
            key.assert_not_called()

    def test_production_loopback_requires_token_before_loading_api_key(self):
        stderr=io.StringIO()
        with patch('sys.argv',['app.py','--require-device-token']), patch.dict('os.environ',{},clear=True), patch('server.app.load_key') as key, redirect_stderr(stderr):
            with self.assertRaises(SystemExit) as error: main()
            self.assertEqual(error.exception.code,2)
            self.assertIn('DEVICE_TOKEN is required',stderr.getvalue())
            key.assert_not_called()

if __name__=='__main__': unittest.main()
