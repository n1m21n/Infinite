# Tracking spike 2a (hands)

Throwaway tooling for `docs/plans/extensions/spike-2a.md`. Run from a scratch folder, never inside the repo.

```bash
python3 -m venv venv
./venv/bin/pip install "numpy<2" "tensorflow-macos==2.15.0" "tf2onnx==1.16.1" "onnx==1.15.0" onnxruntime==1.19.2 opencv-python-headless
curl -L -o hand_landmarker.task https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/latest/hand_landmarker.task
unzip hand_landmarker.task -d task
curl -L -o woman_hands.jpg https://storage.googleapis.com/mediapipe-tasks/hand_landmarker/woman_hands.jpg
for m in hand_detector hand_landmarks_detector; do ./venv/bin/python -m tf2onnx.convert --tflite task/$m.tflite --output $m.onnx --opset 15; done
./venv/bin/python check_fidelity.py   # ONNX vs TFLite on the sample photo
./venv/bin/python bench_latency.py    # CPU vs Core ML, p50/p95 per model
```
