#!/usr/bin/env python3
"""E1 on a CUDA GPU (Colab): the same LoRA run and the same B2 harness as the MLX path, in PyTorch + PEFT.

The MLX run uses an 8-bit model on the Mac; here the bf16 weights are used, so B2 is re-measured on the same
weights (`b2-torch`) and the LoRA arm is compared against that, paired, item by item.

  pip install -U transformers peft accelerate
  python tools/infinite-model/e1_torch.py train --model Qwen/Qwen3.5-0.8B --adapter build/e1/adapters/torch-0.8b
  python tools/infinite-model/e1_torch.py eval  --model Qwen/Qwen3.5-0.8B --adapter build/e1/adapters/torch-0.8b

LoRA settings match e1_lora.sh: rank 8, scale 20 (alpha 160), every linear in the last 8 blocks, lr 1e-4, Adam,
prompt masked, 2 epochs at an effective batch of 4 (450 updates).
"""
import argparse, json, math, os, random, sys, time
import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)


def load(model, adapter=None):
    from transformers import AutoModelForCausalLM, AutoTokenizer
    tok = AutoTokenizer.from_pretrained(model)
    m = AutoModelForCausalLM.from_pretrained(model, torch_dtype=torch.bfloat16).cuda()
    if adapter:
        from peft import PeftModel
        m = PeftModel.from_pretrained(m, adapter)
    m.eval()
    return m, tok


def train(a):
    from peft import LoraConfig, get_peft_model
    from transformers import AutoModelForCausalLM, AutoTokenizer
    torch.manual_seed(0); random.seed(0)
    tok = AutoTokenizer.from_pretrained(a.model)
    m = AutoModelForCausalLM.from_pretrained(a.model, torch_dtype=torch.bfloat16).cuda()
    n = m.config.num_hidden_layers if hasattr(m.config, "num_hidden_layers") else m.config.text_config.num_hidden_layers
    names = sorted({k.split(".")[-1] for k, mod in m.named_modules() if isinstance(mod, torch.nn.Linear)
                    and ".layers." in k and int(k.split(".layers.")[1].split(".")[0]) >= n - 8})
    cfg = LoraConfig(r=8, lora_alpha=160, lora_dropout=0.0, target_modules=names,
                     layers_to_transform=list(range(n - 8, n)), task_type="CAUSAL_LM")
    m = get_peft_model(m, cfg)
    m.print_trainable_parameters()
    d = os.path.join(a.out, "lora-data")

    def rows(split):
        out = []
        for l in open(os.path.join(d, split + ".jsonl")):
            r = json.loads(l)
            p = tok(r["prompt"], add_special_tokens=False)["input_ids"]
            c = tok(r["completion"], add_special_tokens=False)["input_ids"] + [tok.eos_token_id]
            out.append((p + c)[:1024], len(p))
        return out

    tr, va = rows("train"), rows("valid")

    def batch_loss(b):
        L = max(len(x) for x, _ in b)
        ids = torch.full((len(b), L), tok.pad_token_id or 0, dtype=torch.long)
        lab = torch.full((len(b), L), -100, dtype=torch.long)
        att = torch.zeros((len(b), L), dtype=torch.long)
        for i, (x, p) in enumerate(b):
            ids[i, :len(x)] = torch.tensor(x); att[i, :len(x)] = 1
            lab[i, p:len(x)] = torch.tensor(x[p:])
        return m(input_ids=ids.cuda(), attention_mask=att.cuda(), labels=lab.cuda()).loss

    def val():
        m.eval()
        with torch.no_grad():
            ls = [batch_loss(va[i:i + 4]).item() for i in range(0, len(va), 4)]
        m.train()
        return sum(ls) / len(ls)

    opt = torch.optim.Adam([p for p in m.parameters() if p.requires_grad], lr=a.lr)
    m.train()
    print(f"[train] {len(tr)} rows, {a.epochs} epochs, batch {a.batch}; val {val():.3f}", flush=True)
    step, t0 = 0, time.time()
    for ep in range(a.epochs):
        order = list(range(len(tr))); random.shuffle(order)
        for i in range(0, len(order), a.batch):
            loss = batch_loss([tr[j] for j in order[i:i + a.batch]])
            loss.backward(); opt.step(); opt.zero_grad(); step += 1
            if step % 25 == 0:
                print(f"  step {step}  loss {loss.item():.3f}  {time.time() - t0:.0f}s", flush=True)
        print(f"[epoch {ep + 1}] val {val():.3f}", flush=True)
    m.save_pretrained(a.adapter)
    print(f"[train] saved {a.adapter}")


class TorchLM:
    """Drop-in for e1_eval.LM: candidates ranked by summed log-probability of their answer tokens."""

    def __init__(self, model, adapter=None):
        self.model, self.tok = load(model, adapter)

    @torch.no_grad()
    def rank(self, pre, suf, cands, ftype):
        import e1_eval
        t0 = time.time()
        ctx = self.tok(pre, add_special_tokens=False)["input_ids"] + self.tok(suf, add_special_tokens=False)["input_ids"]
        toks = [self.tok(e1_eval.answer_text(ftype, *c), add_special_tokens=False)["input_ids"] for c in cands]
        scores = []
        for i in range(0, len(toks), 32):
            chunk = toks[i:i + 32]
            L = len(ctx) + max(len(t) for t in chunk)
            ids = torch.zeros((len(chunk), L), dtype=torch.long)
            att = torch.zeros((len(chunk), L), dtype=torch.long)
            for k, t in enumerate(chunk):
                x = ctx + t
                ids[k, :len(x)] = torch.tensor(x); att[k, :len(x)] = 1
            lp = torch.log_softmax(self.model(input_ids=ids.cuda(), attention_mask=att.cuda()).logits.float(), -1)
            for k, t in enumerate(chunk):
                s = sum(lp[k, len(ctx) + j - 1, t[j]].item() for j in range(len(t)))
                scores.append(s)
        order = np.argsort(-np.asarray(scores))
        return [cands[i] for i in order], (time.time() - t0) * 1000


def evaluate(a):
    import e1_eval
    e1_eval.LM = TorchLM
    data = e1_eval.Data(a.out, a.cap)
    ns = argparse.Namespace(model=a.model, adapter=a.adapter, tag="torch")
    res = [e1_eval.run_arm(data, "b2", ns)]
    if a.adapter:
        res.append(e1_eval.run_arm(data, "lora", ns))
    e1_eval.table(res)
    if a.adapter:
        e1_eval.compare(a.out, "b2-torch", "lora-torch")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["train", "eval", "both"])
    ap.add_argument("--model", default="Qwen/Qwen3.5-0.8B")
    ap.add_argument("--adapter", default=os.path.join(ROOT, "build/e1/adapters/torch-0.8b"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/e1"))
    ap.add_argument("--epochs", type=int, default=2)
    ap.add_argument("--batch", type=int, default=4)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--cap", type=int, default=150)
    a = ap.parse_args()
    if a.cmd in ("train", "both"):
        train(a)
    if a.cmd in ("eval", "both"):
        evaluate(a)


if __name__ == "__main__":
    main()
