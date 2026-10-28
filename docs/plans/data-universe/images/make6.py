exec(open('make2.py').read().split('# 4 sound')[0])
def tbl(name,title,sub,cards):
    p=P(title,sub)
    for i,(h,ls,c) in enumerate(cards):
        p.card(h,ls,c)
        if i<len(cards)-1: p.arrow()
    p.render(name)
tbl("22_research_numbers.png","Research, with numbers","Three agents; each number graded read or inferred.",[
("What moves mood (Eerola 2013)",["Share of rating variance: mode 0.29, tempo 0.14, register 0.08, dynamics 0.04, articulation 0.02, timbre 0.01","Mode beta: happy +1.77, sad -1.60","Tempo beta: sad -0.32, happy +0.25","A straight-line sum explains 77-89%"],"teal"),
("From audio (Eerola 2009)",["Predicts valence R2 0.72, activity 0.85, tension 0.79","Valence and tension correlate -0.83"],"teal"),
("Performance (Bresin, Friberg)",["Angry: notes 15% closer, +8 dB, 91% recognised","Happy: 20% closer, +3 dB","Sad: 30% longer, -6 dB","Tender: hardest (25-53%)"],"amber"),
("Tempo vs mode (Hunter 2010)",["Tempo separates happy/sad more than mode","Fast minor or slow major: both ratings drop","So we do not tie mode to tempo"],"amber"),
("Pitch (Hong 2018)",["Valence swings 0.45 across C1-C7, only 0.10 within C3-C7","Another study found the opposite: unresolved"],"coral")])
tbl("23_mix_numbers.png","Mix, space and groove numbers","From the mix and structure agent.",[
("Loudness",["Chart dance tracks -6.2 to -11.3 LUFS, mean -8.3","Loudness range 3.5-13.8 LU, usually 4-7","Streaming normalises near -14 LUFS (Apple -16)"],"teal"),
("Levels and spectrum",["Lead vocal about 2.7 LU above the mix; kick about -13 LU","Centroid: bass 1.0 kHz, vocal 2.9, snare 4.4, mix 2.5","Below 250 Hz only bass is clearly centred"],"teal"),
("Groove and timing",["Swing 3.5:1 slow, 2:1 near 200 bpm, 1:1 fast","Notice threshold about 6 ms","Bass +-24 ms: no harm; snare shifted: hurts","Medium syncopation feels best (inverted U)"],"amber"),
("Structure",["Builds 8-16 bars, drops 16-32 (practitioners)","Verse about 34%, chorus 24% of song time","Silence, layers, reverb by role: no study found, we measure"],"coral")])
tbl("24_what_we_must_add.png","What we must add","Gaps found by the node map.",[
("Missing from the 22 axes",["Mode, tempo (the two biggest mood cues)","Swing, micro-timing, silence ratio","Band energies, LUFS, crest, stereo, build arc","Still unmeasured: space, vowel, glide"],"coral"),
("Missing measurements",["13 synth/source nodes, all 24 note nodes, all modulators","Effects: only single-knob tests (156 knobs)","None of the ~7,000 planned chain renders exist","Knob direction right only 62-64% (bar 90%)"],"amber"),
("Mood data we have",["EMOPIA 1,078 clips; VGMIDI 204 phrases","11,700 chord rows from 965 mood-named files","203k formula rows: not truth"],"teal"),
("Order",["1 add the new axes (MIDI side first)","2 measure effect pairs and note nodes","3 fit mood weights on labelled data","4 held-out check, then your listening","5 mix targets from our renders","6 one mood target for note, sound and chain models"],"purple")])
