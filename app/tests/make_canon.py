# Pachelbel's Canon in D (public domain): ground bass, chords and the opening upper voice, our own simple arrangement
import mido, sys
T=480
mid=mido.MidiFile(ticks_per_beat=T)
meta=mido.MidiTrack();mid.tracks.append(meta)
meta+= [mido.MetaMessage('track_name',name='Canon in D'),mido.MetaMessage('set_tempo',tempo=mido.bpm2tempo(72)),
        mido.MetaMessage('time_signature',numerator=4,denominator=4)]
def track(name,ch,notes):
    t=mido.MidiTrack();t.append(mido.MetaMessage('track_name',name=name));ev=[]
    for s,d,p,v in notes: ev+=[(s,1,p,v),(s+d,0,p,0)]
    ev.sort(key=lambda e:(e[0],e[1]));now=0
    for s,on,p,v in ev:
        t.append(mido.Message('note_on' if on else 'note_off',channel=ch,note=p,velocity=v,time=s-now));now=s
    mid.tracks.append(t)
bass=[50,45,47,42,43,38,43,45]          # D A B F# G D G A, half notes
chords=[[62,66,69],[61,64,69],[59,62,66],[61,66,69],[59,62,67],[57,62,66],[59,62,67],[57,61,64]]
mel=[78,76,74,73,71,69,71,73]
B=[];C=[];M=[]
for rep in range(4):
    for i in range(8):
        s=(rep*8+i)*2*T
        B.append((s,2*T-20,bass[i],90))
        if rep>=1:
            for k,p in enumerate(chords[i]): C+= [(s+j*T+k*30,T-40,p,70) for j in (0,1)]
        if rep>=2:
            if rep==2: M.append((s,2*T-20,mel[i],85))
            else:
                M+= [(s,T-10,mel[i],85),(s+T,T//2-10,mel[i]-2 if i%2==0 else mel[i]+2,75),(s+T+T//2,T//2-10,mel[(i+1)%8],80)]
track('Bass',0,B);track('Strings',1,C);track('Melody',2,M)
mid.save(sys.argv[1])
