"""Stage only plugin source, licensed test inputs, and build scripts when explicitly run."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "work"
REPO = WORK / "packaging/repository"


def selected_sources(work):
    """Enumerate the public source allowlist without modifying the repository."""
    work = Path(work)
    products = json.loads((work / "packaging/macos/products.json").read_text(encoding="utf-8"))
    groups = sorted({p["group"] for p in products})
    selected = []
    skip = {"__pycache__", "_python", ".git", "auto-quality", "final-ui-review"}
    for path in sorted((work / "GILLCommon").rglob("*")):
        if path.is_file() and path.suffix.lower() in {".h", ".hpp", ".cpp", ".c", ".mm", ".md", ".txt", ".png"}:
            selected.append(path)
    for group in groups:
        root = work / group
        for sub in ("Source", "Assets", "ThirdParty", "Tests"):
            for path in sorted((root / sub).rglob("*")):
                if not path.is_file():
                    continue
                relative = path.relative_to(root)
                if any(part in skip or part.startswith(("build", "pluginval")) or part.endswith("_artefacts") for part in relative.parts):
                    continue
                if sub == "Tests":
                    code = path.suffix.lower() in {".cpp", ".h", ".hpp", ".c", ".mm", ".m"}
                    fixture = (group in {"GILLDEREVERB", "GILLRESTORATION"} and path.parent == root / "Tests/fixtures"
                               and (path.suffix.lower() in {".wav", ".flac"} or path.name in {"ORIGIN-AND-LICENSE.md", "provenance.json", "prepare_fixtures.py"}))
                    recipe = ((group == "GILLMIX" and relative.as_posix() == "Tests/HostCMake/CMakeLists.txt")
                              or (group in {"GILLMASTER", "GILLRISE"} and path.name == "CMakeLists.txt"))
                    if not (code or fixture or recipe):
                        continue
                    if path.name.startswith(("processed_", "DECLICK-clean", "DECLICK-mouth-restored", "DECLICK-restored",
                                             "DECRACKLE-clean", "DECRACKLE-mouth-restored", "DECRACKLE-restored")):
                        continue
                if path.suffix.lower() not in {".obj", ".exe", ".dll", ".vst3", ".lib", ".pdb", ".zip"}:
                    selected.append(path)
        for path in root.iterdir():
            if path.is_file() and (path.name in {"CMakeLists.txt", "LICENSE"} or path.suffix.lower() == ".md"):
                selected.append(path)
    mac = work / "packaging/macos"
    for path in sorted(mac.rglob("*")):
        relative = path.relative_to(mac)
        guide = path.name.startswith("GILL-UPDATE-") and path.name.endswith("-ANLEITUNG.md")
        allowed = ((len(relative.parts) == 1 and (guide or path.name in {
            "pipeline.py", "products.json", "ctest-matrix.json", "ci_signing.py", "github-actions.yml",
            "README.md", "MAC-INSTALLATION.txt", "test_pipeline.py", "GILL-PLUGINS-UEBERSICHT.txt"}))
            or relative.parts[0] == "resources")
        if path.is_file() and allowed and path.suffix.lower() in {".py", ".json", ".md", ".yml", ".yaml", ".sh", ".xml", ".html", ".txt"}:
            selected.append(path)
    # The packaging regression tests also verify the staging allowlist.
    selected.append(work / "packaging/prepare_repository.py")
    # The Windows setup ships corresponding source alongside its native modules.
    # Only authored recipes/documentation are admitted, never local fixture trees.
    windows = work / "packaging/update14/windows"
    if windows.is_dir():
        selected.extend(path for path in sorted(windows.iterdir())
                        if path.is_file() and path.suffix in {".py", ".cpp", ".nsi"})
    for relative in ("build_update14.py", "build_hosts14.py", "release_update06.py",
                     "packaging/update14/GILL-UPDATE-14-ANLEITUNG.md",
                     "packaging/update14/GILL-PLUGINS-UEBERSICHT.txt"):
        path = work / relative
        if path.is_file():
            selected.append(path)
    runners = work / "update14_delivery"
    if runners.is_dir():
        selected.extend(path for path in sorted(runners.iterdir())
                        if path.is_file() and path.suffix == ".py")
    return selected


def main():
    manifest = []
    for path in selected_sources(WORK):
        destination = REPO / path.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
        manifest.append({"path": destination.relative_to(REPO).as_posix(),
                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "bytes": path.stat().st_size})
    workflow = WORK / "packaging/macos/github-actions.yml"
    destination = REPO / ".github/workflows/build-macos.yml"
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(workflow, destination)
    (REPO / ".gitignore").write_text("**/build*/\n**/*_artefacts/\n**/__pycache__/\n**/*.obj\n**/*.pdb\n**/*.exe\n**/*.vst3/\nartifacts/\noutputs/\n", encoding="utf-8")
    (REPO / ".gitattributes").write_text("* text=auto\n*.sh text eol=lf\n*.py text eol=lf\n*.yml text eol=lf\n*.png binary\n*.wav binary\n*.flac binary\n", encoding="utf-8")
    (REPO / "README.md").write_text(
        "# GILLPRODUCTION Plugins\n\n"
        "Update 14 development: 60 compact vocal, creative, mixing and mastering VST3 plugins,\n"
        "all version 0.14.0. Windows x64 and native macOS Apple Silicon/Intel targets.\n"
        "GILLVOCODE, GILLGRAIN and GILLPULSE are new. Existing factory identities are preserved.\n"
        "All plugins require zero additional audio samples in LIVE; the audio interface and host still add latency.\n\n"
        "This source revision is not a download or a completed installer. Platform\n"
        "installers are published only after the corresponding native verification gates.\n"
        "The most recent completed public release remains available from [Releases]"
        "(https://github.com/twennieofficial-debug/gillproduction-plugins/releases).\n\n"
        "Build recipes and the product catalog are under work/packaging/macos. Initialize\n"
        "the pinned JUCE 8.0.12 submodule before compiling. Source groups and their\n"
        "third-party notices are under work/GILL*. The initial Mac test distribution is ad-hoc\n"
        "signed, not Apple-notarized. Native CI validation does not assert FL Studio\n"
        "listening tests on Mac. Read MAC-INSTALLATION.txt and GILL-UPDATE-14-ANLEITUNG.md.\n",
        encoding="utf-8")
    (WORK / "packaging/repository-staging.json").write_text(
        json.dumps({"files": manifest, "bytes": sum(x["bytes"] for x in manifest)}, indent=2), encoding="utf-8")
    print(json.dumps({"files": len(manifest), "bytes": sum(x["bytes"] for x in manifest), "workflow_present": True}))


if __name__ == "__main__":
    main()
