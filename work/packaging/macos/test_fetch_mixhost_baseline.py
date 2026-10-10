import base64
import copy
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock
import fetch_mixhost_baseline as f

class DonorFetchTests(unittest.TestCase):
    def fixture(self):
        run = {'id':f.RUN_ID, 'head_sha':f.COMMIT, 'repository':{'full_name':f.REPOSITORY},
               'head_repository':{'full_name':f.REPOSITORY}, 'event':'workflow_dispatch',
               'path':'.github/workflows/build-macos.yml'}
        jobs = {'total_count':1, 'jobs':[{'id':17, 'name':f.JOBS['arm64'], 'status':'completed', 'conclusion':'failure'}]}
        artifacts = {'total_count':2, 'artifacts':[
            {'id':i+1, 'name':name, 'expired':False, 'size_in_bytes':20, 'digest':'sha256:'+'a'*64,
             'workflow_run':{'id':f.RUN_ID, 'head_sha':f.COMMIT}}
            for i,name in enumerate(('failed-native-arm64', 'failed-native-binaries-arm64'))]}
        return run,jobs,artifacts
    def test_exact_positive(self):
        run,jobs,artifacts=self.fixture()
        self.assertEqual(f.validate_metadata(run,jobs,artifacts,'arm64')['job']['id'],17)
        f.validate_inputs(f.REPOSITORY,f.RUN_ID,f.COMMIT,f.SOURCE_SHA,'arm64')
    def test_every_input_pin(self):
        correct=[f.REPOSITORY,f.RUN_ID,f.COMMIT,f.SOURCE_SHA,'arm64']
        for index,bad in enumerate(('other/repo',1,'a'*40,'b'*64,'other')):
            values=correct.copy(); values[index]=bad
            with self.subTest(index=index),self.assertRaises(RuntimeError): f.validate_inputs(*values)
    def test_metadata_mutations(self):
        for mutation in range(17):
            run,jobs,artifacts=self.fixture(); item=artifacts['artifacts'][0]
            if mutation==0: run['id']=1
            elif mutation==1: run['head_sha']='a'*40
            elif mutation==2: run['repository']['full_name']='other/repo'
            elif mutation==3: run['head_repository']['full_name']='other/repo'
            elif mutation==4: run['event']='push'
            elif mutation==5: run['path']='.github/workflows/other.yml'
            elif mutation==6: jobs['total_count']=2
            elif mutation==7: jobs['jobs'][0]['status']='in_progress'
            elif mutation==8: jobs['jobs'][0]['conclusion']='success'
            elif mutation==9: artifacts['total_count']=3
            elif mutation==10: item['expired']=True
            elif mutation==11: item['workflow_run']['id']=1
            elif mutation==12: item['workflow_run']['head_sha']='b'*40
            elif mutation==13: item['digest']=''
            elif mutation==14: item['size_in_bytes']=512*1024**2
            elif mutation==15: item['id']=True
            else: item['name']='other'
            with self.subTest(mutation=mutation),self.assertRaises(RuntimeError): f.validate_metadata(run,jobs,artifacts,'arm64')
    def test_duplicates_rejected(self):
        run,jobs,artifacts=self.fixture()
        jobs['jobs'].append(copy.deepcopy(jobs['jobs'][0]));jobs['total_count']=2
        with self.assertRaises(RuntimeError): f.validate_metadata(run,jobs,artifacts,'arm64')
        run,jobs,artifacts=self.fixture()
        artifacts['artifacts'].append(copy.deepcopy(artifacts['artifacts'][0]));artifacts['total_count']=3
        with self.assertRaises(RuntimeError): f.validate_metadata(run,jobs,artifacts,'arm64')
    def test_original_host_exact_bytes(self):
        raw=b'reviewed original\n';record={'type':'file','path':f.HOST_PATH,'encoding':'base64','content':base64.b64encode(raw).decode()}
        with mock.patch.object(f,'HOST_SHA',hashlib.sha256(raw).hexdigest()):
            self.assertEqual(f.original_host(record),raw)
            for field,bad in (('type','dir'),('path','other.cpp'),('encoding','raw'),('content','%%%')):
                altered=record.copy();altered[field]=bad
                with self.subTest(field=field),self.assertRaises((RuntimeError,ValueError)): f.original_host(altered)
    def test_download_digest_and_size(self):
        raw=b'exact transport';artifact={'id':1,'size_in_bytes':len(raw),'digest':'sha256:'+hashlib.sha256(raw).hexdigest()}
        def downloaded(args,**kwargs):
            kwargs['stdout'].write(raw)
            return mock.Mock(returncode=0)
        with tempfile.TemporaryDirectory() as directory,mock.patch.object(f.subprocess,'run',side_effect=downloaded):
            f.download(artifact,Path(directory)/'positive.zip')
            for field,bad in (('size_in_bytes',1),('digest','sha256:'+'0'*64)):
                changed=artifact.copy();changed[field]=bad
                with self.subTest(field=field),self.assertRaises(RuntimeError): f.download(changed,Path(directory)/(field+'.zip'))

if __name__=='__main__': unittest.main()
