"""Pure source allowlist helper; never modifies a repository."""
from pathlib import Path
import json

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
    if any(group.startswith("GILLSTUDIO/") for group in groups):
        for relative in ("GILLSTUDIO/Source/Dynamics/Dynamics.h", "GILLSTUDIO/Source/Spectral/Spectral.h",
                         "GILLSTUDIO/Next03/Source/Spectral/Spectral.h",
                         "Effects16/advanced-ui/AdvancedScalpel.h"):
            path = work / relative
            if not path.is_file():
                raise RuntimeError("Missing required transitive Studio input: " + relative)
            selected.append(path)
    if "GILLCREATIVE" in groups:
        for relative in ("Effects16/creative/before/CreativeDSP.h",
                         "Effects16/creative/before-tempo/CreativeDSP.h"):
            path = work / relative
            if not path.is_file():
                raise RuntimeError("Missing Creative comparison source: " + relative)
            selected.append(path)
    for path in sorted(mac.rglob("*")):
        relative = path.relative_to(mac)
        guide = path.name.startswith("GILL-UPDATE-") and path.name.endswith("-ANLEITUNG.md")
        allowed = ((len(relative.parts) == 1 and (guide or path.name in {
            "pipeline.py", "products.json", "ctest-matrix.json", "ci_signing.py", "github-actions.yml", "diagnose-group.yml",
            "README.md", "MAC-INSTALLATION.txt", "test_pipeline.py", "GILL-PLUGINS-UEBERSICHT.txt",
            "incremental_texture.py", "test_incremental_texture.py",
            "incremental_finish.py", "test_incremental_finish.py",
            "incremental_mixhost.py", "test_incremental_mixhost.py",
            "fetch_mixhost_baseline.py", "test_fetch_mixhost_baseline.py",
            "individuals.py", "test_individuals.py", "publish22.py", "test_publish22.py", "test_mac22.py", "stage_inputs.py", "verify.mjs", "REVIEW.md"}))
            or relative.parts[0] == "resources")
        if path.is_file() and allowed and path.suffix.lower() in {".py", ".json", ".md", ".yml", ".yaml", ".sh", ".xml", ".html", ".txt"}:
            selected.append(path)
    # The packaging regression tests also verify the staging allowlist.
    for path in sorted((work / "Mac22/regression").rglob("*")):
        if path.is_file() and not any(part in {"__pycache__", "selftest-output"} or part.startswith("build") for part in path.relative_to(work / "Mac22/regression").parts):
            selected.append(path)
    selected.extend((work / "GILLSTUDIO/Next03/Tests/Spectral").glob("*.cpp"))
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
                        if path.is_file() and path.name in {
                            "validate14.py", "run_quality_host14.py", "run_mix_host14.py"})
    return selected

