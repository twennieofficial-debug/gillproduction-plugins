#!/usr/bin/env python3
"""Import optional release identities into an ephemeral CI-only keychain.

Secrets are read from the environment, never printed. This helper does not
request certificates, purchase memberships, upload source, or notarize by itself.
Only run in the temporary GitHub Actions runner, never the user's login keychain.
"""
import base64
import os
from pathlib import Path
import secrets
import subprocess
import sys


def run(*args):
    process = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    if process.returncode:
        raise RuntimeError(f"Signing setup command {args[0]} failed ({process.returncode})")
    return process.stdout.strip()


def main():
    if os.environ.get("GITHUB_ACTIONS") != "true" or sys.platform != "darwin":
        raise RuntimeError("This helper is restricted to an ephemeral macOS GitHub Actions runner")
    mode = sys.argv[1]
    root = Path(os.environ["RUNNER_TEMP"]) / "gill-signing"
    root.mkdir(mode=0o700)
    keychain = root / "release.keychain-db"
    password = secrets.token_urlsafe(32)
    run("security", "create-keychain", "-p", password, keychain)
    run("security", "set-keychain-settings", "-lut", "21600", keychain)
    run("security", "unlock-keychain", "-p", password, keychain)
    current = run("security", "list-keychains", "-d", "user").splitlines()
    existing = [line.strip().strip('"') for line in current]
    run("security", "list-keychains", "-d", "user", "-s", keychain, *existing)
    kinds = ["APP", "INSTALLER"] if mode == "package" else ["APP"]
    for kind in kinds:
        path = root / f"{kind.lower()}.p12"
        path.write_bytes(base64.b64decode(os.environ[f"GILL_{kind}_CERT_P12_BASE64"], validate=True))
        path.chmod(0o600)
        run("security", "import", path, "-k", keychain, "-P", os.environ[f"GILL_{kind}_CERT_PASSWORD"],
            "-T", "/usr/bin/codesign", "-T", "/usr/bin/productsign", "-T", "/usr/bin/productbuild")
        path.unlink()
    run("security", "set-key-partition-list", "-S", "apple-tool:,apple:,codesign:", "-s", "-k", password, keychain)
    if mode == "package":
        path = root / "AuthKey.p8"
        path.write_bytes(base64.b64decode(os.environ["GILL_NOTARY_KEY_P8_BASE64"], validate=True))
        path.chmod(0o600)
        run("xcrun", "notarytool", "store-credentials", "gill-release-notary", "--keychain", keychain,
            "--key", path, "--key-id", os.environ["GILL_NOTARY_KEY_ID"], "--issuer", os.environ["GILL_NOTARY_ISSUER"])
        path.unlink()
        # The explicit keychain must be the search destination for the stored
        # profile. Append the profile option safely via the default keychain.
        run("security", "default-keychain", "-d", "user", "-s", keychain)
    with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as output:
        output.write(f"GILL_TEMP_KEYCHAIN={keychain}\n")
        if mode == "package":
            output.write("GILL_NOTARY_PROFILE=gill-release-notary\n")
    print("Ephemeral signing identities prepared; no credentials were logged.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Signing setup stopped: {type(error).__name__}", file=sys.stderr)
        sys.exit(1)
