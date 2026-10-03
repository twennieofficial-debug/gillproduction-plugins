"""Run actual Windows VST3 mode/timing checks, retaining binaries and report hashes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("list", type=Path)
parser.add_argument("--destination", type=Path, required=True)
parser.add_argument("--prefix", default="all60")
args = parser.parse_args()
data = json.loads(args.list.read_text(encoding="utf-8-sig"))
entries = data if isinstance(data, list) else data["plugins"]
paths = [Path(entry if isinstance(entry, str) else entry.get("path", entry.get("bundle"))) for entry in entries]
assert len(paths) == 60 and len({p.resolve() for p in paths}) == 60
assert all(p.is_dir() for p in paths)
modules = {}
for path in paths:
    files = list((path / "Contents/x86_64-win").glob("*.vst3"))
    assert len(files) == 1, path
    modules[str(files[0].resolve())] = digest(files[0])
args.destination.mkdir(parents=True, exist_ok=True)
assert not any((args.destination / f"{args.prefix}-{rate}.json").exists() for rate in (44100, 48000, 96000, 192000))
exe = Path("E:/GILLPRODUCTION/Development14/QualityHost/GillQualityHost.exe")
exe_hash = digest(exe)
work = Path(__file__).resolve().parents[1]
host_source = work / "GILLCommon/Tests/QualityHost.cpp"
host_source_copy = work / "GILLMASTER/Tests/QualityHost.cpp"
host_source_hash = digest(host_source)
assert digest(host_source_copy) == host_source_hash, "QualityHost source copies differ"
result = {"platform": "Windows x64", "passed": False, "host_sha256": exe_hash,
          "host_source_sha256": host_source_hash, "modules": modules, "runs": []}
started = time.monotonic()
for rate in (44100, 48000, 96000, 192000):
    report = args.destination / f"{args.prefix}-{rate}.json"
    console = args.destination / f"{args.prefix}-{rate}.txt"
    begin = time.monotonic()
    print(f"START actual 60-bundle Windows VST3 quality and signal-timing host at {rate} Hz", flush=True)
    with console.open("wb") as output:
        process = subprocess.run([str(exe), "--list", str(args.list.resolve()), "--sample-rate", str(rate), "--report", str(report)], stdout=output, stderr=subprocess.STDOUT, timeout=600)
    actual = json.loads(report.read_text()) if report.exists() else {}
    unchanged = (digest(exe) == exe_hash and digest(host_source) == host_source_hash
                 and digest(host_source_copy) == host_source_hash
                 and all(digest(Path(path)) == expected for path, expected in modules.items()))
    products = actual.get("products", [])
    timing = len(products) == 60 and all(
        p.get("live_latency_samples") == 0
        and p.get("live_signal_measured") is True
        and p.get("live_native_bypass_verified") is True
        and p.get("live_impulse_offset_samples") == 0
        and isinstance(p.get("live_dry_maximum_error"), (int, float))
        and 0 <= p["live_dry_maximum_error"] <= 1e-7
        for p in products
    )
    passed = process.returncode == 0 and actual.get("passed") is True and actual.get("actual_vst3_bundles") == 60 and actual.get("sample_rate") == rate and unchanged and timing
    row = {"sample_rate": rate, "passed": passed, "exit_code": process.returncode,
           "elapsed_seconds": round(time.monotonic() - begin, 3), "report": str(report),
           "report_sha256": digest(report) if report.exists() else None, "modules_unchanged": unchanged,
           "measured_live_timing_all_60": timing, "checks": actual.get("checks"), "failures": actual.get("failures")}
    result["runs"].append(row)
    (args.destination / f"{args.prefix}-matrix.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(row), flush=True)
result["passed"] = len(result["runs"]) == 4 and all(row["passed"] for row in result["runs"])
result["elapsed_seconds"] = round(time.monotonic() - started, 3)
(args.destination / f"{args.prefix}-matrix.json").write_text(json.dumps(result, indent=2) + "\n")
raise SystemExit(0 if result["passed"] else 1)
