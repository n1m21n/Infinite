# Fetch CC0 PBR sets from Poly Haven (1K jpg: diff, nor_gl, rough, disp, ao, arm) into OUT/<name>/.
# usage: python3 tools/realism/pbr_fetch.py OUT name [name ...]   (names from api.polyhaven.com/assets?t=textures)

import json,urllib.request,os,sys
out=sys.argv[1]; names=sys.argv[2:]
tot=0
for n in names:
    req=urllib.request.Request("https://api.polyhaven.com/files/"+n,headers={"User-Agent":"infinite-research"})
    d=json.load(urllib.request.urlopen(req,timeout=30))
    os.makedirs(f"{out}/{n}",exist_ok=True)
    for key,tag in [("Diffuse","diff"),("nor_gl","nor"),("Rough","rough"),("Displacement","disp"),("AO","ao"),("arm","arm")]:
        try: f=d[key]["1k"]["jpg"]
        except KeyError: continue
        dst=f"{out}/{n}/{tag}.jpg"
        if not os.path.exists(dst):
            urllib.request.urlretrieve(f["url"],dst); tot+=f["size"]
    print(n,"ok",flush=True)
print("MB",tot/1e6)
