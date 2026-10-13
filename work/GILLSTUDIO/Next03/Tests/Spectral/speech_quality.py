"""Known-clean speech plus deterministic colored noise, local-only regression.
This quantifies error; it is not a claim that listening tests have passed.
"""
import hashlib, json, pathlib, subprocess, sys, wave
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'templates01/audio/deps'))
import numpy as np
from pedalboard.io import AudioFile
HERE=pathlib.Path(__file__).resolve().parent
source=ROOT/'GILLRESTORATION/Tests/fixtures/dry_48k.wav'
origin=ROOT/'GILLRESTORATION/Tests/fixtures/ORIGIN-AND-LICENSE.md'
with AudioFile(str(source)) as f:
    assert f.samplerate==48000
    speech=f.read(f.frames).mean(axis=0).astype(np.float64)
fs=48000
speech*=.08/max(np.sqrt(np.mean(speech**2)),1e-12)
rng=np.random.default_rng(714182)
def colored(n):
    x=rng.standard_normal(n)
    spectrum=np.fft.rfft(x);freq=np.fft.rfftfreq(n,1/fs)
    shape=np.minimum(np.clip((freq-1400)/600,0,1),np.clip((8500-freq)/600,0,1))
    return np.fft.irfft(spectrum*shape,n)
n=len(speech)
noise=colored(n);noise*=np.sqrt(np.mean(speech**2))/10**(12/20)/np.sqrt(np.mean(noise**2))
learn=colored(2*fs);learn*=np.sqrt(np.mean(noise**2))/np.sqrt(np.mean(learn**2))
build=HERE/'build';build.mkdir(exist_ok=True)
signal=np.concatenate((learn,speech+noise,np.zeros(4096))).astype('<f4')
input_file=build/'speech-input.f32';signal.tofile(input_file)
def snr(reference,audio):
    return float(10*np.log10(np.sum(reference**2)/max(np.sum((audio-reference)**2),1e-30)))
baseline=snr(speech,speech+noise)
results=[]
for live in (False,True):
    for amount in (.5,.85):
        tag=('live' if live else 'pro')+str(round(amount*100))
        output=build/(tag+'.f32')
        run=subprocess.run([str(build/'render-fixture.exe'),str(input_file),str(output),str(fs),str(len(learn)),str(int(live)),str(amount),'18'],capture_output=True,text=True,timeout=40,check=True)
        latency=0 if live else 2048
        wet=np.fromfile(output,dtype='<f4')[len(learn)+latency:len(learn)+latency+n].astype(np.float64)
        assert len(wet)==n and np.isfinite(wet).all()
        after=snr(speech,wet)
        results.append({'mode':tag,'inputSNRdB':baseline,'outputSNRdB':after,'improvementDb':after-baseline,'peak':float(np.max(np.abs(wet))),'latencySamples':latency,'render':run.stdout.strip()})
        with wave.open(str(build/(tag+'.wav')),'wb') as out:
            out.setnchannels(1);out.setsampwidth(2);out.setframerate(fs);out.writeframes((np.clip(wet,-1,1)*32767).astype('<i2').tobytes())
report={'source':str(source),'sourceSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),'originAndLicence':str(origin),'scope':'one clean speech excerpt plus deterministic 1.4-8.5 kHz noise; numerical error, not listening approval','results':results,
        'inputs':{str(p.relative_to(HERE.parent.parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (HERE.parent.parent/'Source/Spectral/Spectral.h', HERE/'render_fixture.cpp',HERE/'speech_quality.py',build/'render-fixture.exe')}}
(build/'speech-quality.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(results,indent=2))
assert all(x['improvementDb']>.5 for x in results),'Speech noise processing failed to improve reference error by 0.5 dB'
print('SPEECH_QUALITY_PASS')
