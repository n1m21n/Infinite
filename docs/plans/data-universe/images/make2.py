from PIL import Image, ImageDraw, ImageFont
W=1080; F="/System/Library/Fonts/Helvetica.ttc"
def f(s,b=False): return ImageFont.truetype(F,s,index=1 if b else 0)
COL={"gray":("#EEEDE8","#8A8880","#2C2C2A"),"teal":("#D7F0E6","#0F6E56","#04342C"),"amber":("#FAEEDA","#BA7517","#412402"),
"coral":("#FAE0D8","#D85A30","#4A1B0C"),"purple":("#E4E1FA","#534AB7","#26215C"),"red":("#FADADA","#A32D2D","#501313"),"blue":("#DCEBFA","#185FA5","#042C53")}
M=Image.new("RGB",(10,10)); MD=ImageDraw.Draw(M)
def wrap(t,fo,w):
    out=[];line=""
    for wd in t.split():
        if MD.textlength((line+" "+wd).strip(),font=fo)>w and line: out.append(line); line=wd
        else: line=(line+" "+wd).strip()
    return out+[line]
class P:
    def __init__(s,title,sub=""):
        s.items=[("title",title),("sub",sub)] if sub else [("title",title)]
    def card(s,head,lines,col="gray"): s.items.append(("card",head,lines,col))
    def arrow(s): s.items.append(("arrow",))
    def gap(s): s.items.append(("gap",))
    def render(s,name):
        TW=W-120; BW=TW-60; fb=f(32); fh=f(40,True)
        # layout pass
        y=40; lay=[]
        for it in s.items:
            k=it[0]
            if k=="title": ls=wrap(it[1],f(50,True),TW); lay.append((it,y,ls)); y+=len(ls)*64+10
            elif k=="sub": ls=wrap(it[1],f(32),TW); lay.append((it,y,ls)); y+=len(ls)*42+20
            elif k=="card":
                body=[]
                for l in it[2]: body.append(wrap("•  "+l if not l.startswith("#") else l[1:],fb,BW))
                h=80+sum(len(b) for b in body)*42+len(body)*8+16
                lay.append((it,y,body,h)); y+=h+14
            elif k=="arrow": lay.append((it,y,None)); y+=50
            else: y+=20
        im=Image.new("RGB",(W,y+30),"white"); d=ImageDraw.Draw(im)
        for e in lay:
            it=e[0];k=it[0];y0=e[1]
            if k=="title":
                for i,l in enumerate(e[2]): d.text((60,y0+i*64),l,font=f(50,True),fill="#111")
            elif k=="sub":
                for i,l in enumerate(e[2]): d.text((60,y0+i*42),l,font=f(32),fill="#555")
            elif k=="card":
                fill,line,tc=COL[it[3]];h=e[3]
                d.rounded_rectangle([60,y0,60+TW,y0+h],radius=18,fill=fill,outline=line,width=3)
                d.text((90,y0+20),it[1],font=fh,fill=tc)
                yy=y0+78
                for b in e[2]:
                    for l in b: d.text((90,yy),l,font=fb,fill=tc); yy+=42
                    yy+=8
            elif k=="arrow":
                x=W//2; d.line([x,y0+4,x,y0+36],fill="#555",width=5); d.polygon([(x-14,y0+28),(x+14,y0+28),(x,y0+46)],fill="#555")
        im.save(name)
# 4 sound
p=P("Sound model, underneath","The question: given knob settings, what will it sound like, and which knobs make a wanted sound?")
p.card("Data in",["Our own renders, 6 engines: Analog, Oscillator, Wavetable, FM, Metallic, String","Random settings, one-knob moves, two-knob pairs, and settings the model proposed itself","Each render keeps its full recipe (renders repeat exactly, 20 of 20)","Human presets as cross-checks: Diva banks, Vital, your Patches","195k real samples show what real sounds look like"],"gray");p.arrow()
p.card("Measure",["Render in the Infinite app","Turn the audio into the 22 characteristic curves"],"purple");p.arrow()
p.card("What it learns",["Forward: knobs to the 22 numbers","Critic: good sound vs random (AUC 0.92)","Inverse: wanted numbers to knobs, checked by re-rendering"],"teal");p.arrow()
p.card("Claims and result",["C2 each knob's effect: miss on all 6 (best FM 71% direction, bar 90%)","C3 good sound: critic passes, your blind listen 24 of 40 (bar 30)","C7 reach a wanted sound: Analog 9.3% closer, String 17.3%, miss"],"amber");p.arrow()
p.card("Missing",["Knob combinations the presets never show apart","More presets from the Air","Learning curve has not flattened: every doubling of data adds about 0.07 to 0.10 R2"],"red")
p.render("4_sound_model.png")
# 5 note
p=P("Note model, underneath","The question: what makes a part sound played by a person, and is this phrase believable?")
p.card("Data in",["MIDI: lakh, grooves, Bach, game music with emotion labels, EZkeys (13,098 files), Ableton clips and 219 groove templates","Four part types: drums, melody, chords, bass","Per note: pitch over time, velocity, timing offset, length"],"gray");p.arrow()
p.card("Feel measures (drums)",["Velocity spread, accent on the beat, hi-hat alternation, backbeat, swing, layback, jitter, drag","Real bars set a normal band for each (1st to 99th percentile)"],"purple");p.arrow()
p.card("Critic, three layers",["1. Hard rules (no kick, wrong meter, stuck notes)","2. Feel gate: outside the real band, reject","3. Likelihood floor and novelty"],"teal");p.arrow()
p.card("Checks (gates B1 to B10)",["B2: real music falsely rejected must stay at 3% or less","B3: broken music must be caught. Sloppy timing 93% (was 83%), inverted accents 93%","Weak: thin parts 68 of 80, arousal AUC 0.55"],"amber");p.arrow()
p.card("Missing",["Controller moves, odd meters, orchestration, emotion sets not yet extracted","Drum port to the app (approved, not built)"],"red")
p.render("5_note_model.png")
# 6 instrument + clusters
p=P("Instrument model and clusters","Instrument: how sampled instruments are played. Clusters: groups of alike sounds.")
p.card("Data in (instrument atlas, 14,161 rows)",["EXS sampler maps 2,413 (775,519 zones, 2,114 fully playable)","Kontakt maps 229, LABS 2,227, Omnisphere soundsources 11,519","Biggest families: drums 970, synth 487, strings 236, bass 165, keys 81"],"gray");p.arrow()
p.card("Clusters",["Only 477 grouped by their audio","9,790 grouped by name and tags","3,894 still unassigned","Genre and subgenre: regions of the 22 axes plus tempo, groove, chords, form"],"amber");p.arrow()
p.card("Instrument model",["Not trained yet","Step 7: compare our String engine with the library's real acoustic sounds","An in-app sampler built from the atlas is a new feature: your approval first"],"coral");p.arrow()
p.card("Missing",["Audio for most atlas rows (thin audio base)","Per-patch audio of Omnisphere, Kontakt, LABS needs rendering"],"red")
p.render("6_instrument_clusters.png")
# 7 effects + mix
p=P("Effects chain and mix, underneath","Effects: what each node does to a sound. Mix: how the parts sit together.")
p.card("Effects: data",["Node census: 342 node types described","Logic channel strips 26,216, plugin presets 7,429, preset chains, Ableton device chains","Parameter tables built for Reverb, Delay, Chorus, Convolution Reverb"],"gray");p.arrow()
p.card("Effects: how it learns",["Render a chain and measure the 22 numbers after every node","Each link is scored alone: sound in, sound out, which knob moved","Held-out chains are frozen by shape before testing","Claim C9: knows what a node in a chain does"],"teal");p.arrow()
p.card("Effects: state",["Designed, not rendered yet","First renders: one effect after a source, once embeddings finish"],"coral");p.arrow()
p.card("Mix: data and state",["Ableton sets: levels, pans, sends, device chains (443 rows so far)","Album and loop audio: balance, masking, loudness","Waits on more Ableton sets"],"coral")
p.render("7_effects_mix.png")
# 8 mood words taste
p=P("Mood, words and taste","Steering dials that sit on every model.")
p.card("Mood and emotion",["Rules map the 22 axes to arousal and valence (draft, awaiting your sign-off)","Mood reference sets (203k labelled rows, game music) are only a check, never the truth"],"coral");p.arrow()
p.card("Words (warm, glitchy, dark, spacious)",["Learned from preset names, tags, folders","A word-to-node matrix says which nodes make which word","Claim C4, not run yet"],"coral");p.arrow()
p.card("Taste",["Learns from what you keep, undo and tweak","Stays on your machine, never uploaded","Claim C10, not started"],"gray");p.arrow()
p.card("Creativity dial",["One dial from typical to adventurous","Measured by novelty and reach, claim C6"],"amber")
p.render("8_mood_words_taste.png")
# 9 data list
p=P("Every data source","What is collected, what is not, what is missing.")
p.card("Read from the WD Black drive",["Presets: Omnisphere, Keyscape, Trilian, Diva, Alchemy 19,118","Omnisphere arps and wavetables 28,630","Sampler maps 2,413, LABS and Kontakt 2,227, wavetables 1,432","Ableton set arcs 443, MIDI 2,600","Audio samples: 195,081 measured"],"gray")
p.card("Read from the Air",["~60,000 synth presets (u-he 21,005, NI 5,979, FabFilter 4,637, Bitwig 2,781 ...)","MIDI 15,640, audio ~17,400, groove templates 219","Logic channel strips 26,216","246 plugins to render"],"gray")
p.card("Made by us",["Renders of our 6 engines with full recipes","Measured on the 22 axes, held-out sets frozen"],"teal")
p.card("Never collected or copied",["Raw audio is not copied between laptops, only measured rows","Your taste data stays local","Chat corpora are never pushed","Non-music folders skipped"],"purple")
p.card("Still missing",["Partials, vowel, glide, space axes not measured yet","Controller moves and odd meters from MIDI","More Ableton sets for the mix","Mood sign-off, your ratings"],"red")
p.render("9_data_sources.png")
# 10 neuroscience
p=P("Brain ideas we borrowed","Each one became a measurement on nodes, knobs and chains.")
p.card("Sparse activity (node activity)",["Per knob: average change it causes across the 11 sound axes","Few knobs do most of the work: Analog top 3 carry 40%, Oscillator 50%","Busiest: filter type 0.56, cutoff 0.38, attack 0.27"],"teal");p.arrow()
p.card("Context dependence",["A knob can be dead given others: Analog detune is 0 while voices is 1","So activity is measured per context"],"teal");p.arrow()
p.card("Functional connectivity",["Which knob pairs change the result together, beyond the sum","Strongest: filter type with cutoff (Analog 0.21, Oscillator 0.37)","Model ranks knobs like the measurements: 0.68 Analog, 0.84 Oscillator"],"purple");p.arrow()
p.card("Tuning curves (knob behaviour)",["Sweep one knob, watch each axis","Cutoff makes it brighter, attack lengthens attack, release lengthens decay, noise adds noisiness","Pulse width barely does anything"],"purple");p.arrow()
p.card("Lesion (node behaviour)",["Bypass a node and measure what is lost","Rule: nodes with 2 or more inputs never bypass"],"amber");p.arrow()
p.card("Chains and prediction error",["Node chains: measure after every node, score each link alone","The learning loop is prediction error: guess, compare, correct","Embedding check: knob sweeps move steadily, 0.91"],"amber")
p.render("10_brain_ideas.png")
