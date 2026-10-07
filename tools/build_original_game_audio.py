"""Compose original pentatonic game music and synthesize event sound effects.

No external song or recording samples. Scores, instruments and deterministic
synthesis are included so the bundled Ogg/WAV assets can be rebuilt offline.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import lfilter

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'native_hd/assets/audio'
RATE=32000
RNG=np.random.default_rng(271828)

def freq(note):return 440*2**((note-69)/12)

def instrument(note,duration,kind):
    t=np.arange(int(RATE*duration))/RATE;f=freq(note)
    if kind=='pluck':
        # Bright guzheng-like harmonic attack with independently decaying modes.
        a=sum(np.sin(2*np.pi*f*h*t+0.1*h)*np.exp(-t*(1.15+h*.46))/h**1.12 for h in range(1,10))
        a+=lfilter([.15,.15],[1,-.7],RNG.standard_normal(t.size))*.035*np.exp(-t*25)
        env=(1-np.exp(-t*800))*np.minimum(1,np.maximum(0,(duration-t)/.15))
    elif kind=='flute':
        vibrato=.003*np.sin(2*np.pi*5.1*t)*(1-np.exp(-t*3));phase=2*np.pi*f*np.cumsum(1+vibrato)/RATE
        a=np.sin(phase)+.20*np.sin(2*phase)+.075*np.sin(3*phase)
        a+=lfilter([.015],[1,-.94],RNG.standard_normal(t.size))*.3
        env=np.minimum(1,t/.09)*np.minimum(1,np.maximum(0,(duration-t)/.22))
    elif kind=='strings':
        a=sum((np.sin(2*np.pi*f*h*t)+.4*np.sin(2*np.pi*f*h*1.0018*t))/h**1.8 for h in range(1,6))
        env=np.minimum(1,t/.35)*np.minimum(1,np.maximum(0,(duration-t)/.55))
    elif kind=='bell':
        a=np.sin(2*np.pi*f*t)*np.exp(-t*1.9)+.42*np.sin(2*np.pi*f*2.76*t)*np.exp(-t*3.4)+.19*np.sin(2*np.pi*f*5.41*t)*np.exp(-t*5.5)
        env=np.minimum(1,t/.004)*np.minimum(1,np.maximum(0,(duration-t)/.08))
    else:
        a=np.sin(2*np.pi*f*t)+.14*np.sin(4*np.pi*f*t);env=np.minimum(1,t/.008)*np.exp(-t*2.5)*np.minimum(1,np.maximum(0,(duration-t)/.1))
    return a*env

def add(buffer,signal,start,gain,pan,wrap=True):
    at=int(start*RATE);idx=np.arange(signal.size)+at
    if wrap:idx%=len(buffer)
    else:
        keep=idx<len(buffer);idx=idx[keep];signal=signal[keep]
    np.add.at(buffer[:,0],idx,signal*gain*np.sqrt((1-pan)/2))
    np.add.at(buffer[:,1],idx,signal*gain*np.sqrt((1+pan)/2))

MELODIES=[
 [0,2,4,7,9,7,4,2, 4,7,9,12,9,7,4,2],
 [7,9,12,9,7,4,2,0, 2,4,7,4,2,0,-3,0],
 [4,7,9,7,4,2,0,2, 7,9,12,14,12,9,7,4],
 [9,7,4,2,4,7,2,0, 2,0,-3,0,4,2,0,-12],
]
SETTINGS={
 'title':(84,62,'flute',.14), 'exploration':(94,62,'flute',.16),
 'palace':(76,60,'pluck',.13), 'cave':(80,57,'bell',.12),
 'underwater':(88,65,'bell',.14), 'battle':(132,62,'pluck',.20),
 'boss':(116,57,'strings',.23),
}

def music(name,settings):
    bpm,tonic,lead,gain=settings;beat=60/bpm;length=64*beat;buf=np.zeros((round(length*RATE),2),dtype=np.float64)
    roots=[0,-3,-5,0, 0,4,-3,0, -5,-3,4,0, -5,-3,0,0]
    action=name in ('battle','boss')
    for bar,root in enumerate(roots):
        at=bar*4*beat
        for interval in [0,7,12]:add(buf,instrument(tonic+root+interval,beat*4.8,'strings'),at,.028 if action else .022,(-.5,0,.5)[interval//7])
        for k in range(4):add(buf,instrument(tonic+root-12,beat*.85,'bass'),at+k*beat,.07 if action else .045,-.1)
        arp=[0,7,12,7,4,7,12,14]
        for k,interval in enumerate(arp):add(buf,instrument(tonic+root+interval,beat*2.8,'pluck'),at+k*.5*beat,.052 if action else .039,(-.55 if k%2==0 else .55))
        phrase=MELODIES[bar//4];offset=(bar%4)*4
        for k in range(4):
            # Leave breaths and introduce octave responses in the second half.
            if name in ('cave','underwater') and k==1 and bar%2:continue
            n=tonic+12+phrase[offset+k]
            dur=beat*(1.65 if k==3 else .88)
            add(buf,instrument(n,dur,lead),at+k*beat,gain*.65,.15)
            if bar>=8 and k%2==0:add(buf,instrument(n-12,beat*1.3,'pluck'),at+(k+.25)*beat,.032,-.35)
        if action:
            for k in range(4):
                t=np.arange(int(RATE*.32))/RATE
                drum=np.sin(2*np.pi*(55*t+30*(1-np.exp(-t*20))/20))*np.exp(-t*17)
                add(buf,drum,at+k*beat,.18 if k%2==0 else .10,0)
                noise=RNG.standard_normal(int(RATE*.10));noise=lfilter([1,-.96],[1],noise)*np.exp(-np.arange(len(noise))/RATE*58)
                add(buf,noise,at+(k+.5)*beat,.014,(-.7 if k%2 else .7))
            if bar%4==3:
                for k in range(3):add(buf,instrument(tonic+7+k*5,.35,'bell'),at+(3+k/3)*beat,.045,0)
        elif name=='underwater':
            add(buf,instrument(tonic+24+root,beat*3,'bell'),at+beat*1.5,.026,-.55)
    # Stereo room reflections, circular so reverb carries naturally over loops.
    wet=buf.copy()
    for seconds,level in [(.113,.17),(.229,.11),(.379,.08),(.613,.035)]:wet+=np.roll(buf[:,::-1],int(seconds*RATE),axis=0)*level
    wet=np.tanh(wet*1.25)
    # Match average loudness across scenes, leaving headroom for event effects.
    peak=np.max(np.abs(wet));rms=np.sqrt(np.mean(wet**2))
    wet*=min(.125/max(rms,1e-9),.72/max(peak,1e-9))
    # Remove residual sub-millisecond discontinuities at the loop boundary.
    count=128;delta=wet[0]-wet[-1];wet[-count:]+=np.linspace(0,1,count)[:,None]*delta
    path=OUT/(name+'.ogg')
    with sf.SoundFile(path,'w',samplerate=RATE,channels=2,format='OGG',subtype='VORBIS') as file:
        for start in range(0,len(wet),RATE):file.write(wet[start:start+RATE].astype('float32'))
    return {'name':name,'file':'audio/'+path.name,'duration_seconds':length,'bpm':bpm,'loop':True,'peak':float(np.max(np.abs(wet))),'rms':float(np.sqrt(np.mean(wet**2))),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

def effect(name):
    durations={'ui':.12,'confirm':.24,'cancel':.17,'step':.12,'attack':.34,'impact':.26,'magic':.88,'heal':.72,'teleport':1.25,'capture':1.0,'reward':1.45,'victory':2.8}
    duration=durations[name];t=np.arange(round(duration*RATE))/RATE;noise=RNG.standard_normal(len(t));a=np.zeros_like(t)
    if name in ('ui','confirm','cancel'):
        notes={'ui':[81],'confirm':[79,86],'cancel':[74,69]}[name]
        for k,n in enumerate(notes):
            s=instrument(n,.12,'bell');at=int(k*.07*RATE);end=min(len(a),at+len(s));a[at:end]+=s[:end-at]*.24
    elif name=='step':a=lfilter([.08],[1,-.85],noise)*np.exp(-t*48)*.4
    elif name=='attack':a=lfilter([1,-.94],[1],noise)*np.sin(np.pi*t/duration)**2*np.exp(-t*8)*.35+np.sin(2*np.pi*(500*t-300*t*t))*np.exp(-t*16)*.12
    elif name=='impact':a=lfilter([.18],[1,-.7],noise)*np.exp(-t*28)*.45+np.sin(2*np.pi*84*t)*np.exp(-t*27)*.5
    elif name=='magic':a=(np.sin(2*np.pi*(380*t+750*t*t))+.3*np.sin(2*np.pi*1140*t))*np.sin(np.pi*t/duration)**1.3*.25+lfilter([.1],[1,-.8],noise)*np.sin(np.pi*t/duration)*.12
    elif name in ('heal','capture','reward','victory'):
        notes={'heal':[74,78,81,86],'capture':[62,69,74,81,86],'reward':[74,78,81,86,90],'victory':[62,66,69,74,69,74,78,81]}[name]
        for k,n in enumerate(notes):
            s=instrument(n,.65,'bell');at=int(k*duration/(len(notes)+2)*RATE);end=min(len(a),at+len(s));a[at:end]+=s[:end-at]*.32
    else:a=(np.sin(2*np.pi*(160*t+300*t*t))+.4*np.sin(2*np.pi*(420*t+900*t*t)))*np.sin(np.pi*t/duration)**2*.32
    a*=np.minimum(1,t/.003)*np.minimum(1,np.maximum(0,(duration-t)/.015));a=np.tanh(a*1.3)*.85
    path=OUT/(name+'.wav');sf.write(path,a.astype('float32'),RATE,subtype='PCM_16')
    return {'name':name,'file':'audio/'+path.name,'duration_seconds':duration,'loop':False,'peak':float(np.max(np.abs(a))),'rms':float(np.sqrt(np.mean(a*a))),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

def main():
    OUT.mkdir(exist_ok=True)
    tracks=[]
    for n,s in SETTINGS.items():
        print('Composing '+n,flush=True);tracks.append(music(n,s));print('Saved '+n,flush=True)
    effects=[effect(n) for n in ['ui','confirm','cancel','step','attack','impact','magic','heal','teleport','capture','reward','victory']]
    report={'version':'0.7','origin':'New original scores and deterministic synthesis; no external songs or recorded samples. Original soundtrack was not recovered.',
            'sample_rate':RATE,'music':tracks,'effects':effects,'music_volume':.62,'effects_volume':.8,'license':'CC0-1.0 for the new composed audio assets'}
    (OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    (OUT/'LICENSE.txt').write_text('New original game music and synthesized effects: CC0-1.0. No external recordings or third-party songs were used. The original game and other resources retain their own rights.\n',encoding='utf8')
    print(json.dumps({'music_tracks':len(tracks),'effects':len(effects),'bytes':sum(p.stat().st_size for p in OUT.iterdir()),'directory':str(OUT)},ensure_ascii=False))

if __name__=='__main__':main()
