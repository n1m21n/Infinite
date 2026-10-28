exec(open('make2.py').read().split('# 4 sound')[0])
def tbl(name,title,sub,cards):
    p=P(title,sub)
    for i,(h,ls,c) in enumerate(cards):
        p.card(h,ls,c)
        if i<len(cards)-1: p.arrow()
    p.render(name)
tbl("18_feel_1_emotions.png","Feel map 1: five emotions","Cue recipes from music-psychology studies.",[
("Happy",["fast tempo, loud, staccato","fast attack, bright sound","major mode"],"amber"),
("Sad",["very slow, quiet, legato","slow attack, dull sound","minor mode"],"gray"),
("Angry",["fast, loud, staccato","abrupt attack, sharp harsh sound","rough, dissonant"],"coral"),
("Tender / calm",["slow, quiet, legato","slow attack, soft sound","major or modal, consonant"],"teal"),
("Fear",["fast, quiet and uneven, staccato","irregular vibrato, pauses","(source row inconsistent: to recheck)"],"purple")])
tbl("19_feel_2_cues.png","Feel map 2: one cue at a time","V valence (happy), E energy, T tension. + up, - down.",[
("Tempo and density",["Faster: E up a lot, V up a little","Slower: E down, V down a little"],"teal"),
("Loudness",["Louder: E up, T up","Quiet: E down"],"teal"),
("Register (pitch height)",["High: V up, E up, T up (light, bright)","Low: V down, E down (dark, heavy)","Wide range: more energy"],"teal"),
("Articulation and attack",["Staccato, fast attack: V up, E up (playful, crisp)","Legato, slow attack: E down (smooth, tender)","Gentle attack raises tension"],"amber"),
("Brightness",["Bright: E up a lot, T up, V up","Dull: all down (sad, muffled)"],"amber"),
("Roughness",["Dissonant, rough: V down, T up a lot (anxious, harsh)"],"coral")])
tbl("20_feel_3_harmony_rhythm.png","Feel map 3: harmony and rhythm","Where studies exist; gaps are marked.",[
("Mode (biggest single cue)",["Major: V up a lot","Minor: V down, tension up (serious, exotic)","Modes: happiest Ionian, then falling; Lydian less happy"],"teal"),
("Chords",["Major: pleasant, stable, best as final chord","Minor: sad, tense; natural minor most negative","Harmonic minor most negative scale","Sus, dim, aug, 7ths: NO study found, we measure"],"amber"),
("Progressions",["Resolve to home: pleasant","Surprise chord: tension","(from expectation theory, to verify)"],"purple"),
("Rhythm and drums",["Fast beat: energy up; slow beat: down","Medium syncopation: most urge to move","Hard accents, busy hats: energy up","Swing, micro-timing: no study found, we measure"],"coral")])
tbl("21_feel_4_sound_instruments.png","Feel map 4: sounds and instruments","Studies on single instrument tones.",[
("Instruments",["Violin, trumpet, clarinet: happy-type","Horn, flute: sad, depressed","Oboe: neutral","Guitar, harp, plucked: more negative"],"teal"),
("Timbre numbers",["Strong partials, sharp attack: valence up","Brighter and quicker: energy up","Brighter, varied, gentle attack: tension up","Few numbers explain 50-57% of valence and energy"],"amber"),
("Synth knobs",["Each knob maps through the axes it moves: filter opens = brighter, noise = rougher, slow attack = softer","Needs our own measurement, not yet done"],"coral"),
("Honest limits",["Mostly Western listeners","Studies give direction; we fit weights on our data","Cues mostly add up in a line (77-89%)","You listen last: 70% agree is the bar"],"purple")])
