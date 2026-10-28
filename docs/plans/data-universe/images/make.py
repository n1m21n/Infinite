from PIL import Image, ImageDraw, ImageFont
W=1080
F="/System/Library/Fonts/Helvetica.ttc"
def f(s,b=False): return ImageFont.truetype(F,s,index=1 if b else 0)
COL={"gray":("#EEEDE8","#8A8880","#2C2C2A"),"teal":("#D7F0E6","#0F6E56","#04342C"),"amber":("#FAEEDA","#BA7517","#412402"),
"coral":("#FAE0D8","#D85A30","#4A1B0C"),"purple":("#E4E1FA","#534AB7","#26215C"),"red":("#FADADA","#A32D2D","#501313"),"blue":("#DCEBFA","#185FA5","#042C53")}
class P:
    def __init__(s,h,title):
        s.im=Image.new("RGB",(W,h),"white"); s.d=ImageDraw.Draw(s.im); s.y=40
        s.text(title,52,True,"#111",wrap=True); s.y+=20
    def text(s,t,sz,b=False,c="#111",wrap=False,x=60,w=W-120):
        fo=f(sz,b); words=t.split(); line=""
        for wd in words:
            if s.d.textlength((line+" "+wd).strip(),font=fo)>w and line:
                s.d.text((x,s.y),line,font=fo,fill=c); s.y+=int(sz*1.3); line=wd
            else: line=(line+" "+wd).strip()
        s.d.text((x,s.y),line,font=fo,fill=c); s.y+=int(sz*1.3)
    def box(s,title,sub="",col="gray",h=None,x=60,w=W-120):
        fill,line,tc=COL[col]; sub=sub if isinstance(sub,list) else ([sub] if sub else [])
        h=h or 70+len(sub)*40
        s.d.rounded_rectangle([x,s.y,x+w,s.y+h],radius=18,fill=fill,outline=line,width=3)
        s.d.text((x+w//2,s.y+34),title,font=f(40,True),fill=tc,anchor="mm")
        for i,l in enumerate(sub): s.d.text((x+w//2,s.y+78+i*40),l,font=f(34),fill=tc,anchor="mm")
        s.y+=h
    def arrow(s,n=60):
        x=W//2; s.d.line([x,s.y+6,x,s.y+n-8],fill="#555",width=5)
        s.d.polygon([(x-14,s.y+n-24),(x+14,s.y+n-24),(x,s.y+n-4)],fill="#555"); s.y+=n
    def save(s,name): s.im.crop((0,0,W,s.y+40)).save(name)
# 1 pipeline
p=P(2200,"The whole project")
p.box("1. Data we own",["MIDI files, presets,","195k audio samples, Ableton sets"],"gray"); p.arrow()
p.box("2. The 22 characteristics",["one shared measuring stick","for sound and notes alike"],"gray"); p.arrow()
p.text("3. The models",40,True)
for t,s_,c in [("Note model","how a part is played  (working)","teal"),("Sound model","6 synth engines  (partly)","amber"),("Clusters","sound, genre, subgenre  (partly)","amber"),("Instrument model","sampler maps  (not trained)","coral"),("Effects chain","node by node  (not trained)","coral"),("Mood and words","emotion on the axes  (not trained)","coral"),("Mix model","levels, pans, sends  (not trained)","coral")]:
    p.box(t,s_,c,h=120); p.y+=14
p.arrow(50)
p.box("4. Compose one patch",["render in the app, measure,","play it to you"],"purple"); p.arrow()
p.box("END TEST",["whole samples, blind,","against the website generator"],"purple")
p.save("1_whole_project.png")
# 2 learning + embedding
p=P(2400,"How a model learns")
for i,(t,s_,c) in enumerate([("Pick knob settings","chosen by the model","teal"),("Model predicts","the 22 numbers, first","teal"),("App renders, we measure","the real 22 numbers","teal"),("Compare","guess against real","amber"),("Update the model","learn from the gap","amber"),("Choose next renders","where it was most wrong","amber")]):
    p.box(t,s_,c); p.arrow(50)
p.text("then back to the top",34,False,"#555"); p.y+=30
p.text("What an audio embedding is",52,True)
p.y+=10
for t,s_,c in [("Rendered audio","a few seconds","purple"),("Pretrained network","CLAP or EnCodec","purple"),("A long list of numbers","one point on a map","purple"),("Close points sound alike","distance = difference","purple")]:
    p.box(t,s_,c); p.arrow(50)
p.box("Checked on Analog",["turn one knob, the point moves steadily","CLAP 0.91, EnCodec 0.89 (1.0 = perfect)"],"gray")
p.save("2_learning_and_embedding.png")
# 3 roadmap
p=P(2000,"Where we are")
p.box("DONE",["data collected","195k sounds measured","22 axes defined","critic AUC 0.92","renders repeat exactly"],"teal"); p.arrow(50)
p.box("NOW",["knob effects below the bar","note rules fixed (93% caught)","dev build running"],"amber"); p.arrow(50)
p.box("NEXT",["effect-chain renders","note to sound link","instrument model","mix model","mood rules"],"gray"); p.arrow(50)
p.box("END",["whole samples rated by you","blind, vs the website generator"],"purple"); p.y+=40
p.box("MISSING",["Ableton sets for the mix","more presets from the Air","your listening ratings","mood rules sign-off"],"red")
p.save("3_roadmap.png")
