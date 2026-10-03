"""Fail-closed Windows14 delivery evidence. Reading never installs plugins."""
from pathlib import Path
import argparse, ctypes, hashlib, importlib.util, json, re, struct, sys, zipfile
import xml.etree.ElementTree as ET

HERE=Path(__file__).resolve().parents[1]
WORK=HERE.parents[1]
ROOT=WORK.parent
BASE=Path('E:/GILLPRODUCTION/Development14')
PACKAGE=BASE/'Packaging'
MANIFEST=PACKAGE/'windows14-validation.json'
BASELINE=Path('E:/GILLPRODUCTION/Development13/Packaging')
OLD_MANIFEST=BASELINE/'windows-validation.json'
RATES=(44100,48000,96000,192000)

def require(ok,message):
    if not ok: raise RuntimeError(message)
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def write(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def pipeline():
    spec=importlib.util.spec_from_file_location('mac_pipeline14',WORK/'packaging/macos/pipeline.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def catalog():
    rows=read(WORK/'packaging/macos/products.json')
    require(len(rows)==60 and all(r['version']=='0.14.0' for r in rows),'Expected60 products version0.14.0')
    for key in ('name','target','code','bundle_id'):
        require(len({r[key] for r in rows})==60,'Duplicate '+key)
    require({r['name'] for r in rows if r['group']=='GILLTEXTURE'}=={'GILLVOCODE','GILLGRAIN','GILLPULSE'},'Wrong new products')
    return rows

def module(p):
    return BASE/p['group'].removeprefix('GILL')/(p['target']+'_artefacts/Release/VST3')/(p['name']+'.vst3')/'Contents/x86_64-win'/(p['name']+'.vst3')

def check_pe(path,version):
    with Path(path).open('rb') as f:
        head=f.read(64);require(head[:2]==b'MZ','Not a PE module')
        f.seek(struct.unpack_from('<I',head,0x3c)[0]);pe=f.read(6)
        require(pe[:4]==b'PE\0\0' and pe[4:]==b'\x64\x86','Not x64')
    api=ctypes.WinDLL('version',use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p]
    size=api.GetFileVersionInfoSizeW(str(path),None);require(size>0,'Missing PE version')
    data=ctypes.create_string_buffer(size)
    api.GetFileVersionInfoW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p]
    require(api.GetFileVersionInfoW(str(path),0,size,data),'Unreadable PE version')
    ptr=ctypes.c_void_p();length=ctypes.c_uint32()
    api.VerQueryValueW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint32)]
    require(api.VerQueryValueW(data,'\\',ctypes.byref(ptr),ctypes.byref(length)),'Missing fixed version')
    words=ctypes.cast(ptr,ctypes.POINTER(ctypes.c_uint32))
    actual=(words[2]>>16,words[2]&65535,words[3]>>16,words[3]&65535)
    require(actual==tuple(map(int,(version+'.0').split('.'))),'PE version differs')

def baseline_check():
    doc=read(OLD_MANIFEST);prior={p['name']:p for p in doc['products']}
    require(doc['release']=='13' and doc['passed'] is True and len(prior)==57,'Invalid13 baseline')
    old_catalog=read(HERE/'catalog13-baseline.json');current={p['name']:p for p in catalog()}
    require(len(old_catalog)==57 and {p['name'] for p in old_catalog}==set(prior),'Wrong old catalog')
    for p in old_catalog:
        require(all(current[p['name']][k]==p[k] for k in ('name','code','target','bundle_id')),'Old catalog identity differs')
        require(sha(prior[p['name']]['module'])==prior[p['name']]['sha256'],'Previous13 module changed: '+p['name'])
    return doc,{'manifest_sha256':sha(OLD_MANIFEST),'modules':{n:r['module'] for n,r in prior.items()}}

def transaction_reuse():
    old=WORK/'packaging/update13/windows/Transaction.cpp';new=HERE/'windows/Transaction.cpp'
    expected=old.read_text(encoding='utf-8')
    for a,b in [('Update13','Update14'),('.gill13','.gill14'),('Install13','Install14'),('0.13.0','0.14.0')]:expected=expected.replace(a,b)
    require(new.read_text(encoding='utf-8')==expected,'Transaction semantics changed beyond release identifiers')
    require('Local\\\\GILL12_' in expected and 'Local\\\\GILL_INSTALL_' in expected,'Legacy/shared mutex changed')
    path=BASELINE/'Windows13/transaction-tests.json';proof=read(path)
    require(proof.get('passed') is True and proof.get('development_only') is False and len(proof['checks'])>=80
            and all(c['passed'] is True for c in proof['checks']),'Previous transaction proof incomplete')
    require(proof['recipe_sha256']['Transaction.cpp']==sha(old),'Previous transaction implementation differs from tested source')
    return {'previous_source_sha256':sha(old),'current_source_sha256':sha(new),'previous_tests':str(path),
            'previous_tests_sha256':sha(path),'previous_checks':len(proof['checks']),
            'only_release_identifiers_changed':True,'previous_tests_claimed_as_rerun':False}

def expected_group_sources(group):
    files=[WORK/group/'CMakeLists.txt']
    for folder in ('Source','Assets','ThirdParty'):
        files += [p for p in (WORK/group/folder).rglob('*') if p.is_file()]
    files += [p for p in (WORK/group/'Tests').rglob('*') if p.is_file() and (p.suffix in ('.cpp','.h','.hpp','.mm') or p.name=='CMakeLists.txt') and not any(x.startswith('build') for x in p.parts)]
    files += [p for p in (WORK/'GILLCommon').rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.cpp','.mm','.png','.txt')]
    return {p.relative_to(WORK).as_posix():sha(p) for p in sorted(set(files))}

def native_records():
    matrix=read(WORK/'packaging/macos/ctest-matrix.json');records=[]
    require(set(matrix)=={p['group'] for p in catalog()},'Build matrix group mismatch')
    for group,required in matrix.items():
        directory=BASE/group.removeprefix('GILL');receipt_path=directory/'build14.json';receipt=read(receipt_path)
        require(receipt.get('group')==group and receipt.get('build_exit')==0 and receipt.get('test_exit')==0
                and receipt.get('source_unchanged') is True,'Build/test receipt failed: '+group)
        require(receipt.get('source_files')==expected_group_sources(group),'Build source closure changed: '+group)
        xml=directory/'ctest14.xml';tests=ET.parse(xml).getroot().findall('testcase')
        names=[t.get('name') for t in tests];needed=set(required)|({'VST3_HOST'} if group=='GILLDEREVERB' else set())
        require(needed<=set(names) and len(names)==len(set(names)) and all(t.get('status')=='run' and t.find('failure') is None and t.find('skipped') is None for t in tests),'Failed/incomplete CTest: '+group)
        records.append({'group':group,'passed':True,'build_receipt':str(receipt_path),'build_receipt_sha256':sha(receipt_path),
                        'report':str(xml),'report_sha256':sha(xml),'tests':[{'name':n,'passed':True} for n in names]})
    require(sum(len(r['tests']) for r in records)>=74,'Missing native tests')
    return records

def validation(product):
    folder=BASE/'Validation'/product['target'];path=folder/'result.json';result=read(path)
    require(result.get('passed') is True and result.get('exit_code')==0 and result.get('strictness')==10,'Strict validation failed')
    require(result.get('module_unchanged') is True and result.get('factory_version_verified') is True
            and result.get('module_sha256')==sha(module(product)),'Validator module binding differs')
    require(result.get('validator')=='Tracktion pluginval 1.0.4','Unexpected validator')
    console_path=folder/'console.txt';console=console_path.read_text(encoding='utf-8',errors='replace')
    require('SUCCESS' in console and product['name']+' v'+product['version'] in console,'Validator success/version absent')
    pairs={(int(a),int(b)) for a,b in re.findall(r'Testing with sample rate \[(\d+)\] and block size \[(\d+)\]',console)}
    require(pairs=={(a,b) for a in (44100,48000,88200,96000,192000) for b in (1,16,64,127,256,512,1024,2048)},'Strict sample/buffer matrix incomplete')
    return {'result':result,'report':str(path),'report_sha256':sha(path),'console':str(console_path),'console_sha256':sha(console_path)}

def quality_matrix_check(rows,old):
    path=BASE/'QualityHost/all60-matrix.json';matrix=read(path);p=pipeline()
    require(matrix.get('passed') is True and len(matrix['runs'])==4
            and matrix['host_sha256']==sha(BASE/'QualityHost/GillQualityHost.exe'),'QualityHost matrix failed or executable changed')
    require(matrix['host_source_sha256']==sha(WORK/'GILLCommon/Tests/QualityHost.cpp')==sha(WORK/'GILLMASTER/Tests/QualityHost.cpp'),'QualityHost source changed')
    require(len(matrix['modules'])==60 and {Path(k).stem:v for k,v in matrix['modules'].items()}=={r['name']:r['sha256'] for r in rows},'Host module matrix differs')
    reports={}
    for run in matrix['runs']:
        require(run.get('passed') is True and run['exit_code']==0 and run['modules_unchanged'] is True and sha(run['report'])==run['report_sha256'],'Host run changed/failed')
        result=read(run['report']);p.verify_quality_result(result,run['sample_rate'],{r['name']:Path(r['bundle']) for r in rows})
        reports[run['sample_rate']]=result
    require(set(reports)==set(RATES),'Missing native host sample rate')
    previous=read(old['quality_matrix']['path']);require(sha(old['quality_matrix']['path'])==old['quality_matrix']['sha256'],'Previous factory matrix changed')
    prior_run=next(r for r in previous['runs'] if r['sample_rate']==48000)
    require(sha(prior_run['report'])==prior_run['report_sha256'],'Previous factory report changed')
    old_ids={r['name']:r['factory_uid'] for r in read(prior_run['report'])['products']}
    ids={r['name']:r['factory_uid'] for r in reports[48000]['products']}
    require(len(old_ids)==57 and all(ids.get(n)==uid for n,uid in old_ids.items()) and len(set(ids.values()))==60,'Old factory IDs changed or new IDs collide')
    return {'path':str(path),'sha256':sha(path),'checks':sum(r['checks'] for r in matrix['runs']),'passed':True,'old57_factory_ids_preserved':True}

def mix_workflow_check(rows):
    path=BASE/'MixRealHost/real-host-matrix.json';matrix=read(path)
    require(matrix.get('passed') is True and matrix.get('products_unchanged') is True,'Fresh MIX host failed')
    require(sha(matrix['host_executable'])==matrix['host_executable_sha256'] and sha(WORK/'GILLMIX/Tests/RealHost.cpp')==matrix['host_source_sha256'],'MIX executable/source changed')
    expected={r['name']:r for r in rows};products=matrix['products']
    require(len(products)==2 and {r['name'] for r in products}=={'GILLMIX','GILLLINK'},'Wrong MIX products')
    for row in products:
        require(row['version']=='0.14.0' and row['sha256']==sha(row['module'])==expected[row['name']]['sha256'],'MIX bytes changed')
    runs=matrix['runs'];require(len(runs)==4 and {r['sample_rate'] for r in runs}==set(RATES),'Missing MIX host rate')
    for run in runs:
        require(run['exit_code']==0 and sha(run['report'])==run['report_sha256'],'MIX report failed/changed')
        result=read(run['report']);require(result==run['result'] and result.get('passed') is True and result.get('failures')==0
                and result.get('checks',0)>0 and result.get('native_instances')==4 and result.get('sample_rate')==run['sample_rate'],'Incomplete MIX workflow')
        require(sorted(r['name'] for r in result['products'])==['GILLLINK']*3+['GILLMIX'] and all(r['version']=='0.14.0' for r in result['products']),'Wrong MIX instances')
    return {'path':str(path),'sha256':sha(path),'checks':sum(r['result']['checks'] for r in runs),'origin':'updated14','passed':True}

def archive_check(path,source):
    pipeline().verify_source_archive(path,source)
    with zipfile.ZipFile(path) as archive:
        names=archive.namelist();require(len(names)==len(set(names)),'Duplicate source archive entries')
        for name in names:
            require(not name.startswith(('/','\\')) and '..' not in Path(name).parts and not any(p in {'.git','.env','.codex'} for p in Path(name).parts),'Unsafe source archive entry')
        for recipe in (HERE/'windows').iterdir():
            if recipe.suffix in {'.cpp','.py','.nsi'}:
                name=recipe.relative_to(ROOT).as_posix()
                require(name in names and hashlib.sha256(archive.read(name)).hexdigest()==sha(recipe),'Missing/changed installer recipe: '+name)

def collect():
    products=catalog();source=pipeline().source_check(WORK);old,baseline=baseline_check();native=native_records();rows=[]
    for product in products:
        path=module(product);check_pe(path,product['version'])
        rows.append({k:product[k] for k in ('name','version','group','target')}|{'plugin_code':product['code'],'module':str(path),
                     'bundle':str(path.parents[2]),'sha256':sha(path),'origin':'updated14','validation':validation(product)})
    archive=PACKAGE/'GILL-UPDATE-14-QUELLCODE.zip';archive_check(archive,source)
    return {'schema':1,'release':'14','version':'0.14.0','passed':True,'source_sha256':source['source_sha256'],
            'baseline13':{'manifest':str(OLD_MANIFEST),'manifest_sha256':baseline['manifest_sha256'],'count':57,'retained_tests_claimed_as_rerun':False},
            'products':rows,'native_ctest':native,'quality_matrix':quality_matrix_check(rows,old),'real_mix_workflow':mix_workflow_check(rows),
            'transaction_reuse':transaction_reuse(),'source_archive':{'path':str(archive),'sha256':sha(archive)}}

def verified_manifest():
    saved=read(MANIFEST);current=collect();require(saved==current,'Validation evidence changed after sealing');return saved

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');parser.add_argument('--check-builds',action='store_true');args=parser.parse_args()
    if args.check_builds:print(json.dumps(native_records(),indent=2))
    elif args.write:write(MANIFEST,collect());print('Sealed exact60 validated Windows14 manifest: '+str(MANIFEST))
    else:verified_manifest();print('Exact60 Windows14 manifest verified')
