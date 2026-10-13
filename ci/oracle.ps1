$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'The historical oracle is allowed only in GitHub Actions.' }
$ReferenceCommit = 'a5e5fa6f7e26174b1fa424e7089889546cc44120'
$env:PATH = "$(Resolve-Path vendor/media);$env:PATH"
$env:OPENBLAS_NUM_THREADS = '1'
$env:OMP_NUM_THREADS = '1'
$env:NPY_DISABLE_CPU_FEATURES = 'AVX2,FMA3,AVX512F,AVX512_SKX,AVX512_CLX,AVX512_CNL,AVX512_ICL'
New-Item -ItemType Directory -Force ci-output/reference | Out-Null
git worktree add --detach ci-output/oracle $ReferenceCommit
$Collector = @'
import dataclasses, json, os, pathlib, platform, sys, unittest, urllib.request
root = pathlib.Path.cwd()
ref = root / 'ci-output/reference'
sys.path.insert(0, str(root / 'ci-output/oracle'))
import numpy as np, scipy, scipy.signal as signal, soundfile as sf, wx
import audio_processor as ap, lyrics_processor as lp, config_manager as cm, gui
from i18n import init_translations
def deny_network(*args, **kwargs):
    raise RuntimeError('Real network requests are forbidden in oracle tests')
urllib.request.urlopen = deny_network
def save_json(name, data):
    (ref / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
def state():
    algorithm, key, position, has, gaussian = np.random.get_state()
    return dict(algorithm=algorithm, key=key.tolist(), position=position, has_gaussian=bool(has), gaussian=gaussian)
def reset():
    np.random.seed(73129)
    np.random.normal()
    return state()
def float_fixture(path, data, rate):
    channels = 1 if data.ndim == 1 else data.shape[1]
    with sf.SoundFile(path, mode='w', samplerate=rate, channels=channels, subtype='FLOAT') as stream:
        if sf._snd.sf_command(stream._file, 0x1050, sf._ffi.NULL, 0) != 0:
            raise RuntimeError('PEAK timestamps could not be disabled')
        stream.write(data)
save_json('environment.json',dict(python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,soundfile=sf.__version__,libsndfile=sf.__libsndfile_version__,unicode=__import__('unicodedata').unidata_version,platform=platform.platform(),cpu_features=np._core._multiarray_umath.__cpu_features__,disabled=os.environ.get('NPY_DISABLE_CPU_FEATURES')))
coefficients = {}
for rate in (1000,8000,22050,44100,48000):
    filters = {}
    specs = dict(bass160=(2,160,'lp'),high160=(2,160,'hp'),vocal_mid=(2,[800,3600],'bandpass'),bass180=(2,min(180,rate*.45),'lp'),high180=(2,min(180,rate*.45),'hp'),bass200=(2,200,'lp'),middle=(2,[200,3800],'bandpass'),top=(2,3800,'hp'),formants=(2,[min(400,rate*.49*.5),min(3800,rate*.49)],'bandpass'),eq_high=(2,25,'hp'),eq_low=(4,18500,'lp'),audit=(2,[500,3500],'bandpass'))
    for key,(order,cutoff,kind) in specs.items():
        try: filters[key] = signal.butter(order,cutoff,btype=kind,fs=rate,output='sos').tolist()
        except ValueError: pass
    b,a = signal.butter(2,min(5500,rate*.45),btype='lp',fs=rate)
    filters['air'] = [b.tolist()+a.tolist()]
    filters['phase'] = []
    for f in (420,1350,2900,5400):
        if f >= rate/2: continue
        w0=2*np.pi*f
        b,a=signal.bilinear([1,-w0/1.4,w0**2],[1,w0/1.4,w0**2],rate)
        filters['phase'].append(b.tolist()+a.tolist())
    coefficients[str(rate)] = filters
save_json('filter-bank.json',coefficients)
(root/'resources/filter-bank.json').write_bytes((ref/'filter-bank.json').read_bytes())
cfg=cm.GhostWaveConfig(cloud_api_token='compatibility_secret',lyrics_cloud_api_token='synthetic_token',language='id',first_run=False,custom_celebrity_blacklist=['Drake','A.B.'])
(ref/'legacy-profile.sn').write_bytes(cfg.to_encrypted_bytes())
save_json('legacy-profile.json',cfg.to_dict())
save_json('random-state.json',reset())
np.random.normal(0,1,10001).astype('<f8').tofile(ref/'normal.bin')
save_json('normal-state.json',state())
np.random.uniform(-.2,.3,10001).astype('<f8').tofile(ref/'uniform.bin')
save_json('uniform-state.json',state())
stages = dict(limit=ap.apply_soft_limiting,jitter=ap.apply_micro_chrono_jitter,bode=ap.apply_bode_frequency_shifter,triple=ap.apply_triple_band_bode_shifter,vocal=ap.apply_total_center_vocal_annihilation,scrambler=ap.apply_anti_whisper_vocal_scrambler,peaks=ap.apply_adversarial_peak_injection,phase=ap.apply_schroeder_phase_dispersion,bed=ap.apply_continuous_camouflage_bed,room=ap.apply_virtual_acoustic_reamping,padding=ap.apply_front_and_tail_padding,eq=ap.apply_eq_and_dither,pitch=ap.apply_pitch_and_tempo_shift,wobble=ap.apply_continuous_pitch_wobble)
audio_cases=[]
for label,channels,length,rate in (('mono',1,44117,44100),('stereo',2,44117,44100),('odd',1,4099,44100),('short',2,317,44100),('lowrate',1,1231,8000),('silence',2,4099,44100)):
    samples=np.arange(length,dtype=np.float64)/rate
    data=(.17*np.sin(2*np.pi*337*samples)+.07*np.cos(2*np.pi*731*samples)).astype(np.float32)
    if label=='silence': data*=0
    if channels==2: data=np.column_stack([data,np.roll(data,17)*np.float32(.7)])
    data.astype('<f4').tofile(ref/(label+'.bin'))
    float_fixture(ref/(label+'.wav'),data,rate)
    loaded,loaded_rate=ap.load_audio_samples(str(ref/(label+'.wav')))
    loaded.astype('<f4').tofile(ref/(label+'.loaded.bin'))
    audio_cases.append(dict(name=label+'.load',stage='load',input=label+'.wav',output=label+'.loaded.bin',rate=loaded_rate,channels=channels))
    for name,function in stages.items():
        before=reset()
        try: output=function(data.copy()) if name=='limit' else function(data.copy(),rate)
        except ValueError: continue
        output.astype('<f4').tofile(ref/(label+'.'+name+'.bin'))
        audio_cases.append(dict(name=label+'.'+name,stage=name,input=label+'.bin',output=label+'.'+name+'.bin',rate=rate,channels=channels,random_state=before,expected_random_state=state(),options=dataclasses.asdict(ap.AudioSanitizeOptions())))
save_json('audio-cases.json',audio_cases)
processor=lp.LyricsProcessor.__new__(lp.LyricsProcessor)
processor.celebrity_blacklist=sorted(set(lp.DEFAULT_CELEBRITY_BLACKLIST),key=lambda x:(-len(x),x.lower()))
corpus=['','[Verse]\nMenghapus jejakmu dari hatiku','Café — “Halo”\nTaylor Swift and Drake','(verse 2)\nHello, beautiful world!','[[chorus]]\nYOU ARE FEELING SOMETHING','[Pre-Chorus: Soft]\nCorazón esperando, sueños mágicos.','10,000 times I am waiting','A'*81+'\n'+('word '*700),'星の音楽 １２３\nİstanbul Kelvin ǅuro','\tIntro:\nI\u200b am\x01 here']
corpus += list(lp.ACOUSTIC_PHONETIC_DISGUISE) + list(lp.SYLLABLE_SYNONYMS)
lyrics_cases=[]
for original in corpus:
    for mode in ('scramble','hybrid','phonetic','semantic','cloud'):
        for adlibs in (False,True):
            result=processor.sanitize(original,cloak_mode=mode,break_ngrams=adlibs)
            audit=dataclasses.asdict(result.audit) if result.audit else None
            if audit: audit={k:audit[k] for k in ('original_words','cloaked_words','ngram_overlap_pct','token_similarity_pct','syllable_accuracy_pct')}
            lyrics_cases.append(dict(input=original,profile=dict(lyrics_cloak_mode=mode,lyrics_break_ngrams=adlibs),output=result.sanitized_text,stats=result.stats,audit=audit))
save_json('lyrics-cases.json',lyrics_cases)
words=set(list(lp.ACOUSTIC_PHONETIC_DISGUISE)+['beautiful','menghapus','kuasai','corazón','diriku','cat','rhythm','poetry','two-word',"don't"])
save_json('syllables.json',[dict(input=w,syllabify=lp.universal_syllabify(w),count=lp.count_syllables(w)) for w in sorted(words)])
app=wx.App.Get() or wx.App(False)
init_translations('en')
frame=wx.Frame(None)
panel=gui.AudioSanitizerPanel(frame,ap.AudioProcessor(),lambda *args:None)
pipeline=[]
for index in range(6):
    panel.preset_choice.SetSelection(index)
    panel.on_preset_changed(None)
    options=ap.AudioSanitizeOptions(mode='cloud_api' if index==5 else 'master_evasion',pitch_shift_semitones=panel.pitch_spin.GetValue(),tempo_factor=panel.tempo_spin.GetValue(),enable_micro_chrono_jitter=panel.jitter_chk.IsChecked(),enable_bode_freq_shifter=panel.bode_chk.IsChecked(),enable_adversarial_peaks=panel.decoy_chk.IsChecked(),enable_phase_dispersion=panel.allpass_chk.IsChecked(),enable_reamping_room=panel.reamping_chk.IsChecked(),enable_vocal_cut=panel.vocal_chk.IsChecked(),enable_formant_scrambler=panel.vocal_chk.IsChecked(),inject_preamble=panel.preamble_chk.IsChecked(),apply_eq_filters=panel.filter_chk.IsChecked(),apply_dither=panel.dither_chk.IsChecked(),trim_duration=panel.trim_chk.IsChecked(),max_duration_seconds=panel.trim_spin.GetValue(),strip_metadata=panel.metadata_chk.IsChecked())
    for extension,bitrate in (('wav','wav'),('mp3','320k')):
        options.output_bitrate=bitrate
        before=reset(); name=f'preset{index}.{extension}'
        result=ap.AudioProcessor().process(str(ref/'stereo.wav'),str(ref/name),options)
        if not result.success: raise RuntimeError(result.error_message)
        pipeline.append(dict(input='stereo.wav',output=name,options=dataclasses.asdict(options),random_state=before,expected_random_state=state()))
frame.Destroy()
float_fixture(ref/'slice-source.wav',np.tile(np.array([.1,-.1],dtype=np.float32),int(44100*7.25)),44100)
parts=ap.slice_audio_for_suno(str(ref/'slice-source.wav'),str(ref/'slices'),3.5)
save_json('slices.json',[pathlib.Path(p).name for p in parts])
save_json('pipeline.json',pipeline)
os.chdir(root/'ci-output/oracle')
suite=unittest.defaultTestLoader.loadTestsFromName('test_app')
with (ref/'legacy-tests.txt').open('w',encoding='utf-8') as report:
    result=unittest.TextTestRunner(stream=report,verbosity=2).run(suite)
save_json('legacy-test-result.json',dict(tests=result.testsRun,failures=len(result.failures),errors=len(result.errors)))
if not result.wasSuccessful():
    print((ref/'legacy-tests.txt').read_text(encoding='utf-8'))
    raise RuntimeError('Historical tests failed')
print(f'Captured {len(audio_cases)} audio cases, {len(lyrics_cases)} lyrics cases and {len(pipeline)} exports')
'@
$Collector | Set-Content ci-output/collect.py -Encoding utf8
python ci-output/collect.py
$Digests = [ordered]@{}
Get-ChildItem ci-output/reference -File -Recurse | Where-Object { $_.Name -notin @('environment.json','legacy-profile.sn','legacy-tests.txt') } | Sort-Object FullName | ForEach-Object {
    $Name = [IO.Path]::GetRelativePath((Resolve-Path ci-output/reference), $_.FullName).Replace('\','/')
    $Digests[$Name] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
}
$Digests | ConvertTo-Json -Depth 4 | Set-Content ci-output/reference-hashes.json -Encoding utf8
