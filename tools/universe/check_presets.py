"""Check that every preset's modulatable params really exist in the engine (mod on the last param validates), and write preset_blacklist.json.
    check_presets.py --bin <Infinite>"""
import sys,subprocess,json,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import scene,universe as u,codegen,visual
f=u.load_facts();P=scene.pools_v2(f);VP=visual.vpools(f)
B=sys.argv[sys.argv.index("--bin")+1] if "--bin" in sys.argv else u.DEFAULT_BIN
saw=P["S"].index("Field Synth:saw");bad=[]
for base,items in codegen.PRESETS.items():
    for i,it in enumerate(items):
        n="%s:p%02d"%(base,i)
        if n not in f: continue
        k=len(it["params"]); 
        if k<2: continue
        key="p."+it["params"][-1][0]
        if n in P["N"]: g="N0.1>N%d.7>S%d.1>O ; L0.3@1.%s"%(P["N"].index(n),saw,key)
        elif n in P["S"]: g="N0.1>S%d.7>O ; L0.3@1.%s"%(P["S"].index(n),key)
        elif n in P["E"]: g="N0.1>S%d.1>E%d.7>O ; L0.3@2.%s"%(saw,P["E"].index(n),key)
        else: continue
        p="/tmp/pm.inf";open(p,"w").write(u.compile_inf(g,f,P))
        cp=subprocess.run([B,"--validate",p],capture_output=True,text=True)
        if cp.returncode:
            j=json.loads(u.last_json(cp.stdout)); bad.append((n,it["name"],k,(j["errors"] or [{}])[0].get("message","")[:90]))
json.dump(sorted(b[0] for b in bad),open(os.path.join(os.path.dirname(os.path.abspath(__file__)),"preset_blacklist.json"),"w"))
print(len(bad),"presets refused:",bad)
