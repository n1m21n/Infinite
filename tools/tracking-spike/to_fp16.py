import sys, onnx, numpy as np
from onnx import numpy_helper, helper, TensorProto
m = onnx.load(sys.argv[1])
g = m.graph
new_init, casts = [], []
for t in g.initializer:
    a = numpy_helper.to_array(t)
    if a.dtype == np.float32 and a.size > 16:
        h = numpy_helper.from_array(a.astype(np.float16), t.name + "_h")
        new_init.append(h)
        casts.append(helper.make_node("Cast", [h.name], [t.name], to=TensorProto.FLOAT, name="cast_" + t.name))
    else:
        new_init.append(t)
del g.initializer[:]
g.initializer.extend(new_init)
for i, c in enumerate(casts):
    g.node.insert(i, c)
onnx.save(m, sys.argv[2])
