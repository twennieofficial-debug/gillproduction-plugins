"""Small, reproducible local speech/reverb fixtures; no user audio is read.

Only the start of the official OpenSLR dev-clean archive is streamed, up to
20 MiB, stopping as soon as one speech file has been found. The large archive
is never downloaded to disk. SoundFile is a local fixture-only dependency.
"""
from pathlib import Path
import hashlib
import io
import json
import math
import sys
import tarfile
import urllib.request
import wave

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "_python"))
import numpy as np
import soundfile as sf

URL = "https://www.openslr.org/resources/12/dev-clean.tar.gz"
LIMIT = 20 * 1024 * 1024


class LimitedReader:
    def __init__(self, response):
        self.response = response
        self.count = 0

    def read(self, n=-1):
        if n < 0 or self.count + n > LIMIT:
            raise RuntimeError("Bounded fixture download limit exceeded")
        data = self.response.read(n)
        self.count += len(data)
        return data


def write_wav(name, x, fs):
    # IEEE float keeps the known direct gain and reverb coefficients intact.
    sf.write(HERE / name, np.asarray(x, dtype=np.float32), fs, subtype="FLOAT")


def convolve(a, b):
    n = len(a) + len(b) - 1
    size = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, size) * np.fft.rfft(b, size), size)[:n]


def upsample3(x):
    # Windowed-sinc low-pass interpolation; exactly aligned after FIR removal.
    half = 96
    t = np.arange(-half, half + 1)
    kernel = np.sinc(t / 3.0) * np.kaiser(len(t), 10.0)
    kernel *= 3.0 / np.sum(kernel)
    z = np.zeros(len(x) * 3)
    z[::3] = x
    return convolve(z, kernel)[half:half + len(z)]


def rir(fs, rt60, drr, seed):
    rng = np.random.default_rng(seed)
    n = int(math.ceil(max(1.2, rt60 * 1.7) * fs))
    h = np.zeros(n)
    h[0] = 1.0
    # Early specular arrivals plus a frequency-coloured diffuse late field.
    for delay, gain in zip([.009, .017, .029, .043], [.48, -.33, .25, .18]):
        h[int(round(delay * fs))] += gain
    start = int(.05 * fs)
    t = np.arange(n - start) / fs
    noise = rng.normal(size=len(t))
    colour = np.exp(-np.arange(24) / 3.0)
    colour /= np.sqrt(np.sum(colour ** 2))
    tail = convolve(noise, colour)[:len(t)]
    tail *= np.exp(-np.log(1000.0) * t / rt60)
    tail *= 0.045
    h[start:] += tail
    h[1:] *= np.sqrt(10 ** (-drr / 10.0) / np.sum(h[1:] ** 2))
    return h


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    provenance_path = HERE / "provenance.json"
    if provenance_path.exists():
        previous = json.loads(provenance_path.read_text(encoding="utf-8"))
        flac = HERE / previous["original_flac"]
        source_member = previous["archive_member"]
        bytes_read = previous["archive_bytes_streamed"]
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "GILLDEREVERB-research-fixtures/0.1"})
        with urllib.request.urlopen(request, timeout=30) as response:
            reader = LimitedReader(response)
            with tarfile.open(fileobj=reader, mode="r|gz") as archive:
                for member in archive:
                    if member.isfile() and member.name.endswith(".flac"):
                        if member.size > 2 * 1024 * 1024:
                            continue
                        data = archive.extractfile(member).read()
                        source_member = member.name
                        flac = HERE / Path(member.name).name
                        flac.write_bytes(data)
                        bytes_read = reader.count
                        break
                else:
                    raise RuntimeError("No small speech item found")
    x, fs = sf.read(flac, dtype="float64")
    if fs != 16000 or x.ndim != 1:
        raise RuntimeError("Unexpected LibriSpeech format")
    original_duration = len(x) / fs
    x = x[:12 * fs]
    # Only fades a cut endpoint; retained original FLAC remains unchanged.
    if original_duration > 12:
        x[-160:] *= np.linspace(1.0, 0.0, 160)
    gain = 10 ** (-12 / 20) / max(1.e-10, np.max(np.abs(x)))
    x *= gain
    speech_end = .5 + len(x) / fs
    dry = np.pad(x, (fs // 2, int(1.8 * fs)))
    write_wav("dry_16k.wav", dry, fs)
    write_wav("dry_48k.wav", upsample3(dry), fs * 3)
    rooms = []
    for label, rt60, drr, seed in [
        ("small", .25, 6.0, 2103),
        ("medium", .55, 0.0, 7241),
        ("large", .90, -3.0, 9173),
    ]:
        h = rir(fs, rt60, drr, seed)
        wet = convolve(dry, h)[:len(dry)]
        write_wav(f"rir_{label}_16k.wav", h, fs)
        write_wav(f"wet_{label}_16k.wav", wet, fs)
        write_wav(f"wet_{label}_48k.wav", upsample3(wet), fs * 3)
        early = h.copy()
        early[int(.05 * fs):] = 0
        target = convolve(dry, early)[:len(dry)]
        write_wav(f"early_target_{label}_16k.wav", target, fs)
        write_wav(f"early_target_{label}_48k.wav", upsample3(target), fs * 3)
        rooms.append({
            "label": label, "nominal_rt60_s": rt60,
            "direct_to_all_reflections_db": drr,
            "seed": seed, "early_target_boundary_s": .05,
            "wet_peak": float(np.max(np.abs(wet))),
        })
    provenance = {
        "dataset": "LibriSpeech dev-clean / OpenSLR SLR12",
        "authors": ["Vassil Panayotov", "Guoguo Chen", "Daniel Povey", "Sanjeev Khudanpur"],
        "source": URL, "dataset_page": "https://www.openslr.org/12/",
        "license": "CC BY 4.0", "license_url": "https://creativecommons.org/licenses/by/4.0/",
        "archive_member": source_member, "original_flac": flac.name,
        "original_sha256": hashlib.sha256(flac.read_bytes()).hexdigest(),
        "archive_bytes_streamed": bytes_read, "original_duration_s": original_duration,
        "speech_start_s": .5, "speech_end_s": speech_end,
        "known_added_gain_db": float(20 * np.log10(gain)),
        "derivative_changes": "First <=12 seconds, endpoint fade only if cropped, peak normalized to -12 dBFS, 0.5 s leading and 1.8 s trailing zero padding; 48 kHz sinc interpolation; synthetic RIR convolution.",
        "dry_caveat": "Clean audiobook recording, not certified anechoic studio speech; its original room colour is part of the reference.",
        "rir_caveat": "Seeded analytical hybrid impulses, not measured rooms and not a full geometrical room simulator; nominal RT60 describes the late-envelope decay, not a certified ISO room measurement.",
        "audio_format": "Generated WAV files: mono IEEE float32; original FLAC retained unchanged.",
        "rooms": rooms,
    }
    provenance_path.write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"flac": flac.name, "streamed_bytes": bytes_read, "dry_duration_s": len(dry)/fs, "rooms": len(rooms), "wav_files": len(list(HERE.glob('*.wav')))}, ensure_ascii=False))


if __name__ == "__main__":
    main()
