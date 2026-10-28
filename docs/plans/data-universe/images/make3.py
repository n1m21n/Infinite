exec(open('make2.py').read().split('# 4 sound')[0])
from PIL import Image, ImageDraw
# 11 knob model + activity map
p=P("Knob model and activity map","How we learn what each knob does.")
p.card("1. Make the moves",["Render uses all knobs as a base (30%)","Move one knob by over 10% of its range (60%)","Move two or three knobs together (10%)","Then the model picks the moves it is least sure about"],"teal");p.arrow()
p.card("2. Measure the change",["Same note, same everything else","Change in each of the 22 numbers = what that knob did"],"purple");p.arrow()
p.card("3. Build the maps",["Activity: average change per knob (busy or dead)","Tuning curve: sweep one knob, watch each axis","Connectivity: pairs that act beyond the sum of each"],"amber");p.arrow()
p.card("4. Check the model",["Model's own guess of each knob's activity vs measured: rank match 0.68 (Analog), 0.84 (Oscillator)","Claim C2: direction right 90% and size r 0.7: not met yet"],"coral")
base=p.render if False else None
# render p to temp then append chart
p.render("_k.png"); top=Image.open("_k.png")
rows=[("Analog",[("filterType",.56),("cutoff",.38),("attack",.27),("resonance",.18),("noise",.18)]),("Oscillator",[("filterType",.54),("cutoff",.46),("sync",.37),("unison",.22),("waveform",.21)])]
H=300+sum(70+len(r[1])*56 for r in rows)
im=Image.new("RGB",(W,top.height+H),"white"); im.paste(top,(0,0)); d=ImageDraw.Draw(im); y=top.height+10
d.text((60,y),"Activity map: average change per knob",font=f(42,True),fill="#111"); y+=70
for eng,ks in rows:
    d.text((60,y),eng,font=f(36,True),fill="#0F6E56"); y+=52
    for n,v in ks:
        d.text((60,y+6),n,font=f(30),fill="#222"); L=int(v/0.6*520)
        d.rounded_rectangle([290,y,290+L,y+40],radius=8,fill="#5DCAA5",outline="#0F6E56",width=2); d.text((300+L,y+4),"%.2f"%v,font=f(30),fill="#222"); y+=56
    y+=18
d.text((60,y),"A few knobs carry most of the change.",font=f(32),fill="#555"); y+=60
im.crop((0,0,W,y)).save("11_knob_model_activity.png")
# 12 interlink
H=2000; im=Image.new("RGB",(W,H),"white"); d=ImageDraw.Draw(im)
d.text((60,40),"How all stages interlink",font=f(50,True),fill="#111")
d.text((60,110),"Every stage reads and writes the same shared space.",font=f(32),fill="#555")
cx,cy=540,1000
d.rounded_rectangle([cx-250,cy-150,cx+250,cy+150],radius=26,fill="#E4E1FA",outline="#534AB7",width=4)
for i,l in enumerate(["SHARED SPACE","22 characteristics","+ mood on top","(arousal, valence)"]): d.text((cx,cy-100+i*66),l,font=f(40 if i==0 else 34,i==0),fill="#26215C",anchor="mm")
nodes=[("Note model",540,330,"teal","pitch, tempo, density,|tension, velocity"),("Genre / clusters",880,610,"amber","regions of the space"),("Sound model",880,1390,"amber","brightness, attack,|movement ..."),("Instrument",540,1670,"coral","which sounds, how played"),("Effects chain",200,1390,"coral","space, width, noisiness"),("Mix model",200,610,"coral","loudness, width, balance")]
for t,x,y,c,s_ in nodes:
    fill,line,tc=COL[c]; ang=(x-cx,y-cy)
    d.line([cx+int((x-cx)*0.42),cy+int((y-cy)*0.42),x+int((cx-x)*0.2),y+int((cy-y)*0.2)],fill="#555",width=5)
    d.rounded_rectangle([x-190,y-70,x+190,y+70],radius=18,fill=fill,outline=line,width=3)
    d.text((x,y-30),t,font=f(36,True),fill=tc,anchor="mm")
    for i,l in enumerate(s_.split("|")): d.text((x,y+10+i*30),l,font=f(26),fill=tc,anchor="mm")
y=1800
for l in ["Each stage is measured in the 22 numbers.","Mood is computed from them (rules waiting for your sign-off).","So a change in one stage moves the others through the same numbers.","Training the links: count baseline first, then a learned matrix."]:
    d.text((60,y),l,font=f(30),fill="#222"); y+=44
im.crop((0,0,W,y+20)).save("12_interlink.png")
# 13 chain decisions
p=P("How chains are decided","Random, fixed, or learned at each step.")
p.card("Synths: fixed choice",["Our 6 engines: Analog, Oscillator, Wavetable, FM, Metallic, String","Why: they are Infinite's own nodes, render the same every time, cover different kinds of synthesis","Plugins (Diva, Vital ...) only as a cross-check"],"gray");p.arrow()
p.card("Effects: chosen by coverage (proposal)",["20 effects from the 342 node types","Pick so every one of the 22 axes is moved by at least one effect (space, width, noisiness, movement ...)","Also the ones real patches use most. Final list not frozen yet"],"amber");p.arrow()
p.card("Note effects: not chosen yet",["Same rule: pick by what they change in the note axes (density, pitch height, tension)","To decide from the node census"],"coral");p.arrow()
p.card("Knob settings: random but repeatable",["Random draws with a stored seed, so any render can be redone exactly"],"teal");p.arrow()
p.card("Which chain to render next: learned",["The model picks the chains and moves it is least sure about","Held-out chain shapes are frozen first and never trained on"],"teal");p.arrow()
p.card("Which chains to offer a user: shared learned model",["The link layer learns from real patches which synth, effect and note parts go together, steered by genre and mood","Not built yet"],"purple")
p.render("13_chain_decisions.png")
