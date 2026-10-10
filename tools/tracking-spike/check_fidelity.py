import numpy as np, onnxruntime as ort, tensorflow as tf, cv2
img = cv2.cvtColor(cv2.imread('woman_hands.jpg'), cv2.COLOR_BGR2RGB)
name,size='hand_landmarks_detector',224
x = (cv2.resize(img,(size,size)).astype(np.float32)/255.0)[None]
it = tf.lite.Interpreter(model_path='task/%s.tflite'%name); it.allocate_tensors()
it.set_tensor(it.get_input_details()[0]['index'], x); it.invoke()
od = it.get_output_details()
ref = {o['name']: it.get_tensor(o['index']) for o in od}
sess = ort.InferenceSession(name+'.onnx', providers=['CPUExecutionProvider'])
got = dict(zip([o.name for o in sess.get_outputs()], sess.run(None, {'input_1': x})))
for k,v in ref.items(): print('tflite', k, v.shape, v.ravel()[:3])
for k,v in got.items(): print('onnx  ', k, v.shape, v.ravel()[:3])
# best pairing for each tflite output
for k,v in ref.items():
    d = {g: np.abs(a.reshape(v.shape)-v).max() for g,a in got.items() if a.size==v.size}
    print(k, 'best match', min(d, key=d.get), 'diff %.2e'%min(d.values()))
