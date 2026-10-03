"""Build the validated isolated TEST setup or a production setup after its tests.

Unverified development payloads are rejected before staging or compilation.
"""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import gate14 as gate

HERE=Path(__file__).resolve().parent
OUT=gate.PACKAGE/"Windows14"
TEST_ROOT=OUT/"isolated"
INSPECTION_ROOT=gate.HERE/"local-fixtures"
REQUIRED_FUNCTIONAL_TESTS={
    "actual silent TEST EXE upgrades57 to60", "all exact module/support hashes installed",
    "actual generated TEST uninstaller succeeds", "actual uninstaller removes every shipped file",
    "loaded old module refuses before writes", "FL process gate refuses before writes",
    "modified payload refuses before target writes", "old57 factory identities preserved",
    "live plugin bytes, IDs and timestamps untouched", "production registration untouched",
    "transaction semantics match previously tested13", "test and build recipe unchanged during execution"}



def recipe_hashes():
    return {name:gate.sha(HERE/name) for name in ("Transaction.cpp","Setup14.nsi","build14.py","test14.py","gate14.py","check_dependencies14.py","finalize14.py")}


def production_test_gate(manifest_sha):
    report=gate.read(OUT/"transaction-tests.json")
    gate.require(report.get("passed") is True and report.get("development_only") is False
                 and REQUIRED_FUNCTIONAL_TESTS<={c.get("test") for c in report.get("checks",[])}
                 and all(c.get("passed") is True for c in report["checks"]),"Complete final isolated tests required")
    gate.require(report.get("manifest_sha256")==manifest_sha and report.get("validated_manifest_sha256")==gate.sha(gate.MANIFEST)
                 and report.get("recipe_sha256")==recipe_hashes(),"Functional tests belong to different payload/recipe/proof")
    gate.require(report.get("live_installation_changed") is False and report.get("registry_changed") is False,"Test isolation proof absent")
    return report


def preserve_test_evidence(validated):
    report=gate.read(OUT/"transaction-tests.json");build=gate.read(OUT/"build-result.json")
    gate.require(report["payload_sha256"]==gate.sha(OUT/"payload-build.json")==build["payload_sha256"],"Test staging record changed before preservation")
    gate.require(build.get("production") is False and build["recipe_sha256"]==report["recipe_sha256"]==recipe_hashes(),"Test build provenance changed")
    for key in ("installer","helper"):
        gate.require(gate.sha(build[key])==build[key+"_sha256"]==report[key+"_sha256"],"Test artifact changed before preservation")
    snapshot=OUT/("VerifiedSnapshot-"+gate.sha(gate.MANIFEST)[:12]);snapshot.mkdir(exist_ok=True)
    files=[OUT/n for n in ("build-result.json","payload-build.json","transaction-tests.json","PayloadIdentity.h")]
    files += [Path(build["installer"]),Path(build["helper"]),gate.MANIFEST,Path(validated["source_archive"]["path"])]
    records=[]
    for src in files:
        fingerprint=gate.sha(src);dest=snapshot/src.name
        if dest.exists():gate.require(gate.sha(dest)==fingerprint,"Existing verified snapshot differs")
        else:shutil.copy2(src,dest)
        gate.require(gate.sha(dest)==fingerprint,"Test snapshot copy differs")
        records.append({"name":dest.name,"sha256":fingerprint,"bytes":dest.stat().st_size})
    document={"passed":True,"files":records,"test_checks":len(report["checks"]),"payload_root":gate.read(OUT/"payload-build.json")["payload_root"]}
    record=snapshot/"snapshot.json"
    if record.exists():gate.require(gate.read(record)==document,"Snapshot record differs")
    else:gate.write(record,document)
    return {"path":str(record),"sha256":gate.sha(record)}


def build(development=False, production=False):
    gate.require(not development,"Unverified development payloads cannot build an Update14 installer")
    gate.baseline_check()
    validated=gate.verified_manifest() if not development else None
    products=gate.catalog(); rows=[]; payload=OUT/("payload-"+str(time.time_ns())); payload.mkdir(parents=True,exist_ok=False)
    for p in products:
        source=gate.module(p); gate.require(source.is_file(),"Missing module "+p["name"]);gate.check_pe(source,p["version"])
        rel=Path(p["name"]+".vst3")/"Contents/x86_64-win"/(p["name"]+".vst3")
        before=gate.sha(source)
        if validated:gate.require(before==next(r["sha256"] for r in validated["products"] if r["name"]==p["name"]),"Validated module changed before staging")
        dest=payload/"P"/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,dest);gate.require(gate.sha(dest)==before==gate.sha(source),"Module changed during staging")
        rows.append({"root":"P","path":rel.as_posix(),"sha256":before,"bytes":dest.stat().st_size,"origin":"14"})
    support={"START-HERE.txt":"GILLPRODUCTION / 60 VST3 PLUGINS\n\nAll products version 0.14.0, with GILLVOCODE, GILLGRAIN and GILLPULSE and zero additional LIVE buffer latency.\nSave projects and close FL Studio before installation.\nFL Studio: Options > Manage plugins > Find installed plugins.\n\n"+("ISOLATED DEVELOPMENT TEST. NOT VALIDATED FOR DELIVERY.\n" if development else "Complete corresponding source and licenses are included.\n"),
             "Install14/PRODUCTS.json":json.dumps([{k:p[k] for k in ("name","version","code")} for p in products],indent=2)}
    for rel,text in support.items():
        dest=payload/"S"/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text,encoding="utf-8")
    shutil.copy2(gate.WORK/"GILLNEXT/LICENSE",payload/"S/LICENSE.txt")
    overview=gate.HERE/"GILL-PLUGINS-UEBERSICHT.txt"
    if overview.is_file():shutil.copy2(overview,payload/"S/PLUGIN-UEBERSICHT.txt")
    guide=gate.HERE/"GILL-UPDATE-14-ANLEITUNG.md"
    if guide.is_file():shutil.copy2(guide,payload/"S/GILL-UPDATE-14-ANLEITUNG.md")
    licenses=[]
    for group in sorted({p["group"] for p in products}):
        base=gate.WORK/group
        for name in ("LICENSE","LICENSE-NOTICE.md","THIRD-PARTY.md","THIRD-PARTY-NOTICES.md"):
            if (base/name).is_file():licenses.append((base/name,Path(group)/name))
        for source in (base/"ThirdParty").rglob("*"):
            if source.is_file() and source.name.upper().startswith(("LICENSE","COPYING","NOTICE")):
                licenses.append((source,Path(group)/source.relative_to(base)))
    licenses += [(gate.WORK/"dependencies/JUCE/LICENSE.md",Path("JUCE/LICENSE.md")),(gate.WORK/"packaging/windows/nsis-3.12/COPYING",Path("NSIS/COPYING.txt"))]
    for source,rel in licenses:
        dest=payload/"S/Licenses"/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    if validated:
        source=Path(validated["source_archive"]["path"]);dest=payload/"S/Source/GILL-UPDATE-14-QUELLCODE.zip";dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
        gate.require(gate.sha(dest)==validated["source_archive"]["sha256"]==gate.sha(source),"Validated source ZIP changed during staging")
        public={"release":"14","source_sha256":validated["source_sha256"],"source_archive_sha256":validated["source_archive"]["sha256"],
                "baseline13_manifest_sha256":validated["baseline13"]["manifest_sha256"],"retained_tests_claimed_as_rerun":False,
                "products":[{k:r[k] for k in ("name","version","plugin_code","sha256","origin")} for r in validated["products"]]}
        (payload/"S/Install14/PROVENANCE.json").write_text(json.dumps(public,indent=2),encoding="utf-8")
    for path in sorted((payload/"S").rglob("*")):
        if path.is_file():rows.append({"root":"S","path":path.relative_to(payload/"S").as_posix(),"sha256":gate.sha(path),"bytes":path.stat().st_size,"origin":"support"})
    manifest="".join("\t".join(str(r[k]) for k in ("root","path","sha256","bytes","origin"))+"\n" for r in rows)
    manifest_sha=__import__("hashlib").sha256(manifest.encode()).hexdigest()
    functional=production_test_gate(manifest_sha) if production else None
    test_snapshot=preserve_test_evidence(validated) if production else None
    (OUT/"PayloadIdentity.h").write_text('#pragma once\nconstexpr const char* gillPayloadSha="'+manifest_sha+'";\nconstexpr const char* gillPayload=R"GILL('+manifest+')GILL";\nconstexpr const wchar_t* gillTestRoot=LR"GILL('+str(TEST_ROOT)+')GILL";\nconstexpr const wchar_t* gillTestInspectionRoot=LR"GILL('+str(INSPECTION_ROOT)+')GILL";\n',encoding="utf-8")
    gate.write(OUT/"payload-build.json",{"development_only":development,"payload_root":str(payload),"validated_manifest_sha256":None if development else gate.sha(gate.MANIFEST),"manifest_sha256":manifest_sha,"files":rows})
    helper=OUT/("transaction-production.exe" if production else "transaction-test.exe")
    command=["cl","/nologo","/EHsc","/std:c++17","/O2","/MT","/utf-8","/DUNICODE","/D_UNICODE"]
    if not production:command.append("/DGILL_TEST_BUILD")
    command += ["/I"+str(OUT),str(HERE/"Transaction.cpp"),"/Fe:"+str(helper),"/Fo:"+str(OUT/"Transaction.obj")]
    script=OUT/"compile-helper.cmd";script.write_text('@echo off\ncall S:\\work\\toolchain\\msvc\\setup_x64.bat\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n',encoding="utf-8")
    env=dict(os.environ,TEMP=str(gate.BASE/"Temp"),TMP=str(gate.BASE/"Temp"));Path(env["TEMP"]).mkdir(parents=True,exist_ok=True)
    result=subprocess.run(["cmd.exe","/d","/c",str(script)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
    (OUT/"compile-helper.log").write_bytes(result.stdout);gate.require(result.returncode==0,result.stdout.decode(errors="replace")[-8000:])
    npath=lambda p:str(p).replace("/","\\").replace("$","$$")
    nsisfiles=[]
    for r in rows:
        source=payload/r["root"]/r["path"];rel=Path(r["path"])
        nsisfiles += ['SetOutPath "$PLUGINSDIR\\payload\\'+r["root"]+'\\'+str(rel.parent).replace("/","\\")+'"', 'File "'+npath(source)+'"']
    include=OUT/"payload-files.nsh";include.write_text("\n".join(nsisfiles)+"\n",encoding="utf-8")
    nsis=gate.WORK/"packaging/windows/nsis-3.12/makensis.exe"
    output=OUT/("GILLPRODUCTION-SETUP-WINDOWS-14-PREPARED.exe" if production else "GILLPRODUCTION-SETUP-14-DEVELOPMENT-TEST.exe" if development else "GILLPRODUCTION-SETUP-14-TEST.exe")
    args=[str(nsis),"/V3","/INPUTCHARSET","UTF8","/DGILL_OUTPUT="+str(output),"/DGILL_HELPER="+str(helper),"/DGILL_FILES="+str(include),"/DGILL_LICENSE="+str(payload/"S/LICENSE.txt")]
    if not production:args.append("/DTEST_BUILD")
    args.append(str(HERE/"Setup14.nsi"));result=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
    (OUT/"compile-nsis.log").write_bytes(result.stdout);gate.require(result.returncode==0,result.stdout.decode(errors="replace")[-8000:])
    result={"development_only":development,"production":production,"installer":str(output),"installer_sha256":gate.sha(output),"helper":str(helper),"helper_sha256":gate.sha(helper),"payload_sha256":gate.sha(OUT/"payload-build.json"),"manifest_sha256":manifest_sha,"recipe_sha256":recipe_hashes(),"validated_manifest_sha256":None if not validated else gate.sha(gate.MANIFEST),"isolated_test_report_sha256":gate.sha(OUT/"transaction-tests.json") if functional else None,"test_snapshot":test_snapshot}
    gate.write(OUT/("production-build-result.json" if production else "build-result.json"),result)
    print(output)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--production",action="store_true");args=parser.parse_args();build(production=args.production)
