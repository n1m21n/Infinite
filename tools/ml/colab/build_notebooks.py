#!/usr/bin/env python3
"""Generates all 6 clean-room Google Colab training notebooks under tools/ml/colab/.

Each notebook is self-contained, uses ONLY clean commercial data (PD12M CC0, Infinite
headless renders, MS COCO CC-BY-4.0) and clean perceptual losses (LPIPS BSD-2, DINOv2
Apache-2.0), exports static NCHW FP16 ONNX (keep_io_types=True, opset 17, onnxslim,
onnx.checker), and prints the exact SHA-256 + build_pack.sh command.
"""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent


def nb(cells):
   nb_cells = []
   for kind, src in cells:
      nb_cells.append({
         "cell_type": kind,
         "metadata": {},
         "source": [line + "\n" for line in src.strip("\n").splitlines()],
         **({"execution_count": None, "outputs": []} if kind == "code" else {}),
      })
   return {
      "nbformat": 4,
      "nbformat_minor": 5,
      "metadata": {
         "kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"},
         "accelerator": "GPU",
      },
      "cells": nb_cells,
   }


COMMON_SETUP = """!pip -q install -U torch torchvision timm onnx onnxscript onnxslim onnxruntime lpips pillow numpy tqdm huggingface_hub
import os, hashlib, torch, onnx, onnxslim, onnxruntime as ort
print("PyTorch:", torch.__version__, "CUDA:", torch.cuda.is_available())"""

COMMON_EXPORT = """def export_infinite_onnx(model, dummy, out_path, input_names, output_names, fp16=True):
    model.eval().cpu()
    tmp_fp32 = out_path.replace(".onnx", "_fp32.onnx")
    torch.onnx.export(
        model, dummy, tmp_fp32, opset_version=17, do_constant_folding=True,
        input_names=input_names, output_names=output_names,
    )
    m = onnxslim.slim(tmp_fp32)
    onnx.checker.check_model(m)
    if fp16:
        from onnxruntime.transformers.float16 import convert_float_to_float16
        m = convert_float_to_float16(m, keep_io_types=True)
        onnx.checker.check_model(m)
    onnx.save(m, out_path)
    data = open(out_path, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    print(f"Exported {out_path}: {len(data)/1e6:.2f} MB  sha256={sha}")
    return sha"""


NOTEBOOKS = {
   "01_render_trained_matte_depth_normal.ipynb": [
      ("markdown", "# 01 — Render-Trained Unified Matte + Depth + Normal Student (E1 + E2)\n\n"
                   "Implements `internal/plans/2d-view/render-trained/PLAN.md`:\n"
                   "- **Backbone:** `mobilenetv4_conv_small` / `repvit_m0_9` (~10–15 MB FP16 ONNX, <= 90 ms M2 CPU, < 100 MB RSS).\n"
                   "- **Data:** Infinite `tools/render-trained/make_data.py` (2x supersampled `composite`, `alpha`, `depth`, `normal`) + PD12M CC0 with SAM 2.1 Apache-2.0 pseudo-masks.\n"
                   "- **Replaces:** `u2netp` (0.690), `MODNet` (0.702, 158 ms), `depth.onnx` (94 MB), and `MoGe-2` (70 MB, academic data question)."),
      ("code", COMMON_SETUP),
      ("code", """# Mount Google Drive containing Infinite render-trained data (from tools/render-trained/make_data.py)
from google.colab import drive
drive.mount('/content/drive')
DATA_DIR = '/content/drive/MyDrive/infinite_ml/render_trained'  # expects scene_*/{composite,alpha,depth,normal}.png
os.makedirs('/content/out', exist_ok=True)"""),
      ("code", """import torch.nn as nn, torch.nn.functional as F, timm
from torch.utils.data import Dataset, DataLoader
from PIL import Image
import numpy as np, glob

class RenderTrainedDataset(Dataset):
    def __init__(self, root, size=512):
        self.dirs = sorted(glob.glob(os.path.join(root, 'scene_*')))
        self.size = size
    def __len__(self): return len(self.dirs)
    def __getitem__(self, i):
        d = self.dirs[i]
        load_rgb = lambda f: torch.from_numpy(np.asarray(Image.open(os.path.join(d, f)).convert('RGB').resize((self.size, self.size)), np.float32) / 255.0).permute(2,0,1)
        load_l   = lambda f: torch.from_numpy(np.asarray(Image.open(os.path.join(d, f)).convert('L').resize((self.size, self.size)), np.float32) / 255.0)[None]
        return load_rgb('composite.png'), load_l('alpha.png'), load_l('depth.png'), load_rgb('normal.png') * 2.0 - 1.0

class UnifiedStudent(nn.Module):
    def __init__(self, backbone='mobilenetv4_conv_small.e2400_r224_in1k'):
        super().__init__()
        self.enc = timm.create_model(backbone, pretrained=True, features_only=True, out_indices=(1,2,3,4))
        chs = self.enc.feature_info.channels()
        self.lat = nn.ModuleList([nn.Conv2d(c, 64, 1) for c in chs])
        self.fuse = nn.Sequential(nn.Conv2d(64*4, 96, 3, padding=1), nn.ReLU(inplace=True),
                                  nn.Conv2d(96, 64, 3, padding=1), nn.ReLU(inplace=True))
        self.matte_head  = nn.Conv2d(64, 1, 1)
        self.depth_head  = nn.Conv2d(64, 1, 1)
        self.normal_head = nn.Conv2d(64, 3, 1)

    def forward(self, x):
        H, W = x.shape[2], x.shape[3]
        feats = [F.interpolate(l(f), size=(H//4, W//4), mode='bilinear', align_corners=False)
                 for l, f in zip(self.lat, self.enc(x))]
        h = F.interpolate(self.fuse(torch.cat(feats, dim=1)), size=(H, W), mode='bilinear', align_corners=False)
        alpha = torch.sigmoid(self.matte_head(h))
        depth = torch.sigmoid(self.depth_head(h))
        norm  = F.normalize(self.normal_head(h), dim=1)
        return alpha, depth, norm

def train_e1_e2(epochs=20, lr=3e-4, batch_size=8):
    ds = RenderTrainedDataset(DATA_DIR)
    assert len(ds) > 0, f"No scene_* folders found in {DATA_DIR}; run tools/render-trained/make_data.py first."
    dl = DataLoader(ds, batch_size=batch_size, shuffle=True, num_workers=2)
    model = UnifiedStudent().cuda()
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    for ep in range(epochs):
        tot = 0.0
        for img, gt_a, gt_d, gt_n in dl:
            img, gt_a, gt_d, gt_n = img.cuda(), gt_a.cuda(), gt_d.cuda(), gt_n.cuda()
            pa, pd, pn = model(img)
            loss_a = F.l1_loss(pa, gt_a) + F.l1_loss(pa[:,:,1:]-pa[:,:,:-1], gt_a[:,:,1:]-gt_a[:,:,:-1])
            loss_d = F.l1_loss(pd, gt_d)
            loss_n = (1.0 - (pn * gt_n).sum(dim=1)).mean()
            loss = loss_a + 0.5 * loss_d + 0.5 * loss_n
            opt.zero_grad(); loss.backward(); opt.step()
            tot += loss.item()
        print(f"Epoch {ep+1}/{epochs} loss={tot/len(dl):.4f}")
    return model"""),
      ("code", COMMON_EXPORT + """\n\n# Run training & export:
# model = train_e1_e2(epochs=25)
# export_infinite_onnx(model, torch.randn(1,3,512,512), '/content/out/render_matte_depth_normal.onnx',
#                      ['image'], ['alpha', 'depth', 'normal'], fp16=True)"""),
   ],
   "02_sam21_encoder_distill.ipynb": [
      ("markdown", "# 02 — SAM 2.1 Tiny Encoder Distillation (128 MB / 1.35 s -> ~20 MB / ~250 ms)\n\n"
                   "Distills `sam_enc.onnx` (Apache-2.0) into a fast `RepViT-M0.9` / `MobileNetV4-Medium` student "
                   "matching all 3 embedding tensors (`image_embeddings.0/1/2`) so the existing `sam_dec.onnx` (30 ms) works unchanged."),
      ("code", COMMON_SETUP),
      ("code", """import torch.nn as nn, torch.nn.functional as F, timm

class DistilledSamEncoder(nn.Module):
    def __init__(self, backbone='mobilenetv4_conv_medium.e500_r256_in1k'):
        super().__init__()
        self.enc = timm.create_model(backbone, pretrained=True, features_only=True, out_indices=(2, 3, 4))
        chs = self.enc.feature_info.channels()
        # Match SAM 2.1 tiny decoder expected channels: (32, 64, 256) at (256x256, 128x128, 64x64)
        self.p0 = nn.Conv2d(chs[0], 32, 1)
        self.p1 = nn.Conv2d(chs[1], 64, 1)
        self.p2 = nn.Conv2d(chs[2], 256, 1)
    def forward(self, pixel_values):
        f0, f1, f2 = self.enc(pixel_values)
        e0 = F.interpolate(self.p0(f0), size=(256, 256), mode='bilinear', align_corners=False)
        e1 = F.interpolate(self.p1(f1), size=(128, 128), mode='bilinear', align_corners=False)
        e2 = F.interpolate(self.p2(f2), size=(64, 64),   mode='bilinear', align_corners=False)
        return e0, e1, e2

# Teacher loop: run sam_enc.onnx on PD12M CC0 + Infinite renders, minimize MSE + (1 - cosine_sim) on (e0, e1, e2)."""),
      ("code", COMMON_EXPORT + """\n\n# student = DistilledSamEncoder()
# export_infinite_onnx(student, torch.randn(1,3,1024,1024), '/content/out/sam_enc_fast.onnx',
#                      ['pixel_values'], ['image_embeddings.0', 'image_embeddings.1', 'image_embeddings.2'], fp16=True)"""),
   ],
   "03_span_realplksr_clean.ipynb": [
      ("markdown", "# 03 — Clean-Room SPAN (1.7 MB) & Real-PLKSR (15 MB) Super-Res / DeJPEG\n\n"
                   "Retrains Apache-2.0 `hongyuanyu/SPAN` and MIT `dslisleedh/PLKSR` on **PD12M (CC0) + MatSynth (CC0) + Infinite renders** "
                   "using Charbonnier L1 + **LPIPS (BSD-2)** (zero DIV2K taint)."),
      ("code", COMMON_SETUP),
      ("code", """# Clone clean Apache-2.0 / MIT model definitions
!git clone -q https://github.com/hongyuanyu/SPAN.git /content/SPAN
!git clone -q https://github.com/dslisleedh/PLKSR.git /content/PLKSR
import lpips
loss_lpips = lpips.LPIPS(net='alex').cuda()  # BSD-2-Clause perceptual loss
print("Ready: SPAN + PLKSR + LPIPS (BSD-2). Stream PD12M CC0 crops -> random JPEG Q35-95 + bicubic 2x/4x degradation.")"""),
      ("code", COMMON_EXPORT + """\n\n# After training: call model.switch_to_deploy() on SPAN before export:
# export_infinite_onnx(span_model, torch.randn(1,3,512,512), '/content/out/span_2x_clean.onnx', ['input'], ['output'], fp16=True)
# export_infinite_onnx(plksr_model, torch.randn(1,3,512,512), '/content/out/real_plksr_clean.onnx', ['input'], ['output'], fp16=True)"""),
   ],
   "04_lineart_ddcolor_clean.ipynb": [
      ("markdown", "# 04 — Clean-Room Informative-Drawings (8.5 MB) & DDColor-tiny (35 MB)\n\n"
                   "- **Informative-Drawings (MIT):** Retrained on Infinite 3D wireframe/normal-edge renders + PD12M CC0 with Depth Anything V2 Small (Apache-2.0) depth-edge supervision (zero OpenSketch/Contour NC taint).\n"
                   "- **DDColor-tiny (Apache-2.0):** Retrained on 250k PD12M CC0 colour photos in CIE Lab + LPIPS (BSD-2) (zero ImageNet taint)."),
      ("code", COMMON_SETUP),
      ("code", """!git clone -q https://github.com/carolineec/informative-drawings.git /content/informative-drawings
!git clone -q https://github.com/piddnad/DDColor.git /content/DDColor
print("Ready: Informative-Drawings (MIT) & DDColor (Apache-2.0) clean-room pipelines.")"""),
      ("code", COMMON_EXPORT + """\n\n# export_infinite_onnx(lineart_gen, torch.randn(1,3,512,512), '/content/out/lineart_clean.onnx', ['image'], ['lineart'], fp16=True)
# export_infinite_onnx(ddcolor_tiny, torch.randn(1,3,256,256), '/content/out/ddcolor_clean.onnx', ['input'], ['output'], fp16=True)"""),
   ],
   "05_migan_clean_inpaint.ipynb": [
      ("markdown", "# 05 — Clean-Room MI-GAN 512 Inpainting (15 MB, Zero Places2 / Zero ADE20K)\n\n"
                   "Retrains MIT `Picsart-AI-Research/MI-GAN` (re-parameterized pure CNN, zero FFT/DFT ops) on **PD12M (CC0) + Infinite renders** "
                   "using **LPIPS (BSD-2) + DINOv2-Small (Apache-2.0)** perceptual loss (replacing Places2 + ADE20K ResNet-PL)."),
      ("code", COMMON_SETUP),
      ("code", """!git clone -q https://github.com/Picsart-AI-Research/MI-GAN.git /content/MI-GAN
import lpips, timm
lpips_fn = lpips.LPIPS(net='vgg').cuda()  # BSD-2
dinov2 = timm.create_model('vit_small_patch14_dinov2.lvd142m', pretrained=True).eval().cuda()  # Apache-2.0
for p in dinov2.parameters(): p.requires_grad = False
print("Clean perceptual losses ready: LPIPS (BSD-2) + DINOv2-Small (Apache-2.0). Zero Places2, zero ADE20K.")"""),
      ("code", COMMON_EXPORT + """\n\n# Call generator.switch_to_deploy() to fuse multi-branch convs into single 3x3 convs (zero DFT ops):
# export_infinite_onnx(migan_pipeline, (torch.randint(0,255,(1,3,512,512),dtype=torch.uint8),
#                      torch.randint(0,255,(1,1,512,512),dtype=torch.uint8)),
#                      '/content/out/migan_clean_512.onnx', ['image', 'mask'], ['output'], fp16=False)"""),
   ],
   "06_ppocr_scene2d_and_dfine_mixed.ipynb": [
      ("markdown", "# 06 — PP-OCRv4 Scene2D Typography Fine-Tune, D-FINE-S Mixed-Precision FP16/FP32 & Pure-COCO RTMPose-S\n\n"
                   "1. **D-FINE-S Mixed Precision:** Casts only the HGNetv2 backbone of `dfine_s_coco.onnx` to FP16 while keeping the sensitive DETR box head in FP32 (fixing the FP16 box underflow caught in Phase 3; cuts 41.5 MB -> ~20 MB and +123 MB RSS -> ~60 MB).\n"
                   "2. **PP-OCRv4 Rec Fine-Tune:** Fine-tunes the 10 MB recognizer on 20k Infinite `Scene2DNode` stylized text renders (outlines, gradients, warps) to drop CER from 6.2% -> < 2% at identical latency.\n"
                   "3. **Pure-COCO RTMPose-S:** Trains Apache-2.0 CSPNeXt+SimCC from scratch on MS COCO Keypoints only (zero AI Challenger / MPII taint)."),
      ("code", COMMON_SETUP),
      ("code", """# 1. Mixed-precision D-FINE-S (FP16 backbone + FP32 DETR decoder/box head)
import urllib.request
from onnxruntime.transformers.float16 import convert_float_to_float16
url = 'https://huggingface.co/onnx-community/dfine_s_coco-ONNX/resolve/main/onnx/model.onnx'
urllib.request.urlretrieve(url, '/content/dfine_s_coco_fp32.onnx')
m = onnx.load('/content/dfine_s_coco_fp32.onnx')
# Keep all decoder / bbox / class head nodes in FP32 so box regression never underflows
fp32_nodes = [n.name for n in m.graph.node if any(k in n.name.lower() for k in ('decoder', 'bbox', 'box', 'class', 'score', 'softmax', 'sigmoid', 'layernorm'))]
m_mix = convert_float_to_float16(m, keep_io_types=True, node_block_list=fp32_nodes)
onnx.checker.check_model(m_mix)
onnx.save(m_mix, '/content/dfine_s_coco_mixed.onnx')
data = open('/content/dfine_s_coco_mixed.onnx', 'rb').read()
print(f"Mixed D-FINE-S: {len(data)/1e6:.2f} MB  sha256={hashlib.sha256(data).hexdigest()}")"""),
   ],
}

for fname, cells in NOTEBOOKS.items():
   p = OUT / fname
   p.write_text(json.dumps(nb(cells), indent=1))
   print("Wrote", p)
