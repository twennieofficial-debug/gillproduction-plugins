"""Finalize a verified Windows14 setup; never performs a live installation."""
from pathlib import Path
import json, shutil, zipfile
from build14 import gate, OUT, production_test_gate, recipe_hashes
from check_dependencies14 import audit

def finalize():
    proof=gate.verified_manifest();build=gate.read(OUT/'production-build-result.json');payload=gate.read(OUT/'payload-build.json')
    gate.require(build.get('production') is True and build.get('development_only') is False,'Production artifact missing')
    gate.require(build['payload_sha256']==gate.sha(OUT/'payload-build.json') and build['validated_manifest_sha256']==gate.sha(gate.MANIFEST)
                 and build['recipe_sha256']==recipe_hashes(),'Production artifact binding changed')
    functional=production_test_gate(build['manifest_sha256'])
    gate.require(build['isolated_test_report_sha256']==gate.sha(OUT/'transaction-tests.json'),'Functional report changed')
    snapshot=build['test_snapshot'];gate.require(gate.sha(snapshot['path'])==snapshot['sha256'],'Preserved test record changed')
    for row in gate.read(snapshot['path'])['files']:
        gate.require(gate.sha(Path(snapshot['path']).parent/row['name'])==row['sha256'],'Preserved test artifact changed')
    for key in ('installer','helper'):gate.require(gate.sha(build[key])==build[key+'_sha256'],'Compiled artifact changed')
    for row in payload['files']:gate.require(gate.sha(Path(payload['payload_root'])/row['root']/row['path'])==row['sha256'],'Staged artifact changed')
    deps=audit();delivery=gate.PACKAGE/'Delivery';delivery.mkdir(parents=True,exist_ok=True);artifacts={}
    def deliver(src,name):
        src=Path(src);dest=delivery/name;fingerprint=gate.sha(src)
        if dest.exists():gate.require(gate.sha(dest)==fingerprint,'Different final artifact already exists')
        else:shutil.copy2(src,dest)
        gate.require(gate.sha(dest)==fingerprint,'Delivery bytes differ')
        artifacts[name]={'path':str(dest),'sha256':fingerprint,'bytes':dest.stat().st_size}
    deliver(build['installer'],'GILLPRODUCTION-SETUP-WINDOWS-14.exe')
    deliver(proof['source_archive']['path'],'GILL-UPDATE-14-QUELLCODE.zip')
    for name in ('GILL-PLUGINS-UEBERSICHT.txt','GILL-UPDATE-14-ANLEITUNG.md'):deliver(gate.HERE/name,name)
    public={'release':'14','platform':'Windows x64','module_count':60,'updated_count':57,'new_products':3,
            'source_sha256':proof['source_sha256'],'source_archive_sha256':proof['source_archive']['sha256'],
            'quality_host_checks':proof['quality_matrix']['checks'],'mix_workflow_checks':proof['real_mix_workflow']['checks'],
            'isolated_install_checks':len(functional['checks']),'previous_transaction_tests_claimed_as_rerun':False,
            'products':[{k:r[k] for k in ('name','version','plugin_code','sha256','origin')} for r in proof['products']]}
    public_path=delivery/'GILL-UPDATE-14-WINDOWS-PRUEFBERICHT.json';gate.write(public_path,public)
    artifacts[public_path.name]={'path':str(public_path),'sha256':gate.sha(public_path),'bytes':public_path.stat().st_size}
    report={'schema':1,'release':'14','passed':True,'platform':'Windows x64','products':60,'updated14':57,'new_products':3,
            'manifest':{'path':str(gate.MANIFEST),'sha256':gate.sha(gate.MANIFEST)},'source_sha256':proof['source_sha256'],
            'artifacts':artifacts,'production_build':{'path':str(OUT/'production-build-result.json'),'sha256':gate.sha(OUT/'production-build-result.json')},
            'functional_tests':{'path':str(OUT/'transaction-tests.json'),'sha256':gate.sha(OUT/'transaction-tests.json'),'checks':len(functional['checks'])},
            'preserved_test_snapshot':snapshot,'dependencies':{'path':str(OUT/'dependency-report.json'),'sha256':gate.sha(OUT/'dependency-report.json'),'files':len(deps['files'])},
            'transaction_reuse':proof['transaction_reuse'],'live_installation_performed':False,'production_registry_modified':False,'unsigned_windows_installer':True}
    gate.verified_manifest();gate.write(gate.PACKAGE/'windows14-release.json',report)
    print(json.dumps({'passed':True,'installer':str(delivery/'GILLPRODUCTION-SETUP-WINDOWS-14.exe')},indent=2));return report

if __name__=='__main__':finalize()
