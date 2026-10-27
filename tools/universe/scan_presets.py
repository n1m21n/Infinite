import os,collections,sys
roots=["/Library/Audio/Presets","/Library/Application Support","/Library/Arturia","/Users/namansoni/Library/Application Support","/Users/namansoni/Library/Audio","/Library/Audio/Plug-Ins","/Applications/Arturia","/Applications/KORG","/Users/namansoni/Documents","/Users/Shared"]
EXT={".fxp",".fxb",".h2p",".vital",".vitalbank",".serumpreset",".fxp",".vstpreset",".aupreset",".nksf",".nksn",".phaseplant",".adg",".adv",".prt_omn",".nki",".nkm",".tfx",".sylenth1",".syx",".xml",".json",".db",".bwpreset",".pjunoxl",".tal",".fxp",".patch",".preset",".mid",".pgm",".prg",".korg",".wav",".aif",".flac",".sfz",".exs",".rx2",".zip",".nkx",".ksd",".nkc",".nkr",".ncw",".lib"}
KEEP={".fxp",".fxb",".h2p",".vital",".serumpreset",".vstpreset",".aupreset",".nksf",".phaseplant",".prt_omn",".nki",".syx",".bwpreset",".pgm",".prg",".tal",".preset",".patch",".db",".ncw",".sfz",".exs"}
res=collections.defaultdict(lambda:[0,0])
for r in roots:
    for dp,dn,fn in os.walk(r):
        if "/Plug-Ins" in dp and "/Contents/" in dp and not "Resources" in dp: dn[:]=[]; continue
        p=dp.split("/")
        for f in fn:
            e=os.path.splitext(f)[1].lower()
            if e in KEEP:
                try: s=os.path.getsize(os.path.join(dp,f))
                except: s=0
                k="/".join(p[:5] if r.startswith("/Library") else p[:7])+" | "+e
                res[k][0]+=1; res[k][1]+=s
for k,v in sorted(res.items(),key=lambda x:-x[1][0])[:150]: print(v[0],v[1]//1024,"KB",k)
