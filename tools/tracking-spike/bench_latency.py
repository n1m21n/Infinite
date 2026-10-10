import numpy as np, onnxruntime as ort, time, cv2
img = cv2.cvtColor(cv2.imread('woman_hands.jpg'), cv2.COLOR_BGR2RGB)
def sess(m, prov):
    so = ort.SessionOptions(); so.log_severity_level=3
    return ort.InferenceSession(m+'.onnx', so, providers=prov)
x = (cv2.resize(img,(224,224)).astype(np.float32)/255.0)[None]
cpu = sess('hand_landmarks_detector',['CPUExecutionProvider']).run(None,{'input_1':x})
for opts in ({}, {'ModelFormat':'MLProgram'}, {'ModelFormat':'MLProgram','MLComputeUnits':'CPUAndGPU'}, {'ModelFormat':'MLProgram','MLComputeUnits':'ALL'}):
    prov=[('CoreMLExecutionProvider',opts),'CPUExecutionProvider']
    for m,sz in (('hand_landmarks_detector',224),('hand_detector',192)):
        try:
            s=sess(m,prov)
        except Exception as e:
            print(opts,m,'ERR',str(e)[:80]); continue
        xx=np.random.rand(1,sz,sz,3).astype(np.float32)
        for _ in range(20): s.run(None,{'input_1':xx})
        t=[]
        for _ in range(200):
            a=time.perf_counter(); s.run(None,{'input_1':xx}); t.append((time.perf_counter()-a)*1000)
        extra=''
        if m=='hand_landmarks_detector':
            o=s.run(None,{'input_1':x}); extra='  max landmark diff vs CPU %.3f px'%np.abs(o[0]-cpu[0]).max()
        print(opts,m,'p50 %.2f p95 %.2f'%(np.percentile(t,50),np.percentile(t,95))+extra)
