# Infinite Predictive Engine & Category Reference Guide

A complete scientific, mathematical, and architectural breakdown of Infinite's **Prediction Engine**, its 6-layer telemetry-to-UI stack, the honesty analysis ("Bluff vs. Real Data"), and the full taxonomy of Predictive nodes.

---

## 1. Executive Summary & Philosophy

In traditional modular audio/visual compositors, parameters are driven by:
1. **Fixed Waveforms** (Yellow cables: LFOs, Envelopes) — non-adaptive, rigid periodicity.
2. **Deterministic Formulas** (Purple cables: Expressions) — strictly mathematical.
3. **Linear Gesture Replays** (Red cables: Gesture records) — exact playback, no generalisation.

Infinite introduces a fourth paradigm: **Prediction (Green cables)**.
Prediction nodes drive parameters using **generative dynamical systems learned directly from the user's historical interaction patterns**.

```
Traditional LFO (Static Math)   ──► Fixed Sinusoids / Shapes (No Adaptation)
Gesture Recorder (Exact Memory) ──► Replays Fixed Points (Zero Generalisation)
Predictive Engine (Dynamical)   ──► Learns Phase-Space Manifolds (Infinite Evolution)
```

---

## 2. Six-Layer Architecture (Bottom-to-Top Flow)

The system is structured as a 6-layer stack. The bottom layer runs continuously in the background for every parameter in the patch, while the top layer is the physical interaction surface.

```
===================================================================================
 [LAYER 6: USER INTERACTION & COGNITION]
  • Drag single macro/drift cable onto multiple targets
  • Knobs glide synchronously along user's habitual manifold
  • Live gesture readout + % of variance explained (Confidence metric)
  • Shift+Drag correction gesture seamlessly updates the model on release
-----------------------------------------------------------------------------------
 [LAYER 5: TARGET WRITE-BACK & ASYMMETRIC RE-INJECTION]
  • Fader-position mapping: value = posToValue(posLo + (posHi - posLo) * p)
  • Clamped & rate-smoothed parameter write
  • Asymmetric telemetry write-back (Weight = 0.1) with Auto-Cut collapse monitor
-----------------------------------------------------------------------------------
 [LAYER 4: NODE SIMULATION TICK (PER-FRAME DRIFT ENGINE)]
  • Langevin Stochastic Differential Equation (SDE) + Damped Momentum:
      v_t = v_{t-1} * exp(-dt / tau_m)
      x_t = x_{t-1} + v_t*dt - eta * U'(x)*dt + sigma * sqrt(dt) * N(0, 1)
  • Temperature scaling (Stray T = sigma^2 / (2*eta)) & Energy link: T_eff = T*(1 + Link*E)
-----------------------------------------------------------------------------------
 [LAYER 3: CO-VARIANCE & DYNAMIC MANIFOLD EXTRACTION (DMD / PCA)]
  • Multi-knob joint dynamics via Dynamic Mode Decomposition (DMD):
      A = H2 * pinv(H1), scaled spectral radius rho(A) <= 1
  • Extraction of dominant coupled movement modes across disparate parameters
-----------------------------------------------------------------------------------
 [LAYER 2: RUNNING SUFFICIENT STATISTICS & THE COLD-START LADDER]
  • O(1) online exponential forgetting (half-life H = 14 days active play):
      Running AR(1) sums (W, Sx, Sy, Sxx, Sxy, Syy) -> theta, mu, sigma
      64-bin Dwell Probability Density Function p(x) -> Potential field U(x) = -ln(p(x) + eps)
  • Fallback Prior Ladder: Param Instance -> NodeType -> ParamRole -> RoleFamily -> You -> Anchor
-----------------------------------------------------------------------------------
 [LAYER 1: ZERO-OVERHEAD CONTINUOUS TELEMETRY CAPTURE]
  • Always-on 10 Hz grid sampling across all parameters in the patch
  • Delta-encoded binary stream (~5-6 bytes per event): (t, key_id, pos01, source, flags)
  • Source attribution: Hand (w=1.0), Correction (w=3.0), Modulator (w=0.1), Reset (w=0.0)
===================================================================================
```

### Layer 1: Zero-Overhead Continuous Telemetry Capture
* **Sampling Cadence:** Runs at the end of every frame's modulation pass (`ApplyModulationAndPalette`).
* **Binary Delta Encoding:** 5–6 bytes per event:
  $$\text{Event} = \langle \text{KeyID} \,(\text{varint}),\, \Delta t \,(\text{varint ms}),\, \text{pos01} \,(\text{uint16}),\, \text{source} \,(\text{uint8}),\, \text{flags} \,(\text{uint8})\rangle$$
* **Stable Keys:** Identified by `(GraphNode::uid, paramIndex)` — impervious to node renaming, patch reorganization, and undo/redo operations.
* **DAgger-Style Source Attribution:**
  * **Hand / Performance Matrix ($w = 1.0$):** Direct human intentionality.
  * **Shift-Grab Correction ($w = 3.0$):** Human intervention overriding an active model.
  * **Automated Modulators ($w = 0.1$):** Decimated to 10 Hz to prevent artificial over-weighting.
  * **Discontinuous Resets / Presets ($w = 0.0$):** Logged for auditability, discarded for training.

### Layer 2: Online Sufficient Statistics & The Cold-Start Ladder
* **$\mathcal{O}(1)$ Memory with Exponential Forgetting:** Active-time half-life ($H = 14$ days of playback time):
  $$\gamma = 2^{-\frac{\Delta t_{\text{active}}}{H}}, \quad W \leftarrow \gamma W + w_i, \quad S_{xy} \leftarrow \gamma S_{xy} + w_i x_t x_{t+1}$$
* **Sufficient Statistics:**
  * **AR(1) Dynamics:** Fitted parameters $(\phi, \theta, \mu, \sigma)$ governing speed and natural baseline.
  * **Dwell Landscape ($p(x)$):** 64-bin histogram smoothed by Gaussian kernel ($\sigma = 2$ bins), representing the parameter's "sweet spots".
  * **Release Momentum ($\nu_{\text{rel}}$):** Rolling velocity over the final $100\text{ ms}$ before touch release.
* **6-Tier Prior Ladder (Cold Start):**
  $$\text{Key Instance (1)} \to \text{Node Type (2)} \to \text{Param Role (2b)} \to \text{Role Family (2d)} \to \text{Global Style (2c)} \to \text{Static Anchor (4)}$$

### Layer 3: Dynamic Manifold Extraction (DMD / PCA)
* **Cross-Parameter Coupling:** Identifies how multiple parameters covary.
* **Exact Dynamic Mode Decomposition (DMD):**
  $$A = H_2 \cdot H_1^\dagger \quad (H_1^\dagger = \text{Moore-Penrose Pseudoinverse})$$
* **Spectral Radius Stabilization:** Clamps $\rho(A) = \max |\lambda_i| \le 1.0$ to guarantee marginal stability without explosion or premature decay.

### Layer 4: Per-Frame Langevin SDE Engine
Every frame ($\Delta t \approx 16.6\text{ ms}$), the simulation advances:
1. **Momentum:** $v_t = v_{t-1} \cdot e^{-\Delta t / \tau_m}$
2. **Langevin SDE:**
   $$x_t = x_{t-1} + v_t \Delta t - \eta \cdot \nabla U(x_{t-1}) \Delta t + \sigma \sqrt{\Delta t} \cdot \mathcal{N}(0, 1)$$
   where $U(x) = -\ln(\hat{p}(x) + \varepsilon)$.
3. **Exact Thermodynamic Stray ($T = \frac{\sigma^2}{2\eta}$):**
   * $T = 1.0$: Faithfully reproduces historical dwell density $\hat{p}(x)$.
   * $T < 1.0$: Tightly docks into sweet spots.
   * $T > 1.0$: Broad exploratory wandering.
4. **Hand Energy Coupling ($T_{\text{eff}} = T \cdot (1 + \text{Link} \cdot E)$):** Wakes up quiet parameters when the user actively plays.

### Layer 5: Target Write-Back & Feedback Protection
* **Perceptual Fader Space:** Values map in normalized fader coordinates before applying logarithmic/exponential curves:
  $$\text{value} = \text{posToValue}\big(\text{posLo} + (\text{posHi} - \text{posLo}) \cdot x_t\big)$$
* **Anti-Collapse Auto-Cut:** Shannon entropy $H(p) = -\sum p_i \ln p_i$ is monitored. If $H(p)$ drops $>25\%$ without new manual input, feedback weight is cut to zero ($w \to 0$).

### Layer 6: User Interaction & Polish
* **Zero Training Wait:** The model is pre-warmed from the start of the session.
* **Unified Macro Action:** One cable glides multiple controls along their learned joint path.
* **Live UI Metrics:** Live confidence dot ($n_{\text{eff}}$), spectral radius gauge, and % variance explained.

---

## 3. The Science: "Bluff vs. Real Data" Analysis

Is the model an all-knowing oracle or an empirical system?

### **Honesty Breakdown: $\approx 35\%$ Illusion / $\approx 65\%$ Empirical Data**

```
0% Confidence ────────────────── 35% Confidence ────────────────── 100% Confidence
[ 100% BLUFF / PRIORS ]        [ HYBRID TRANSITION ]           [ 100% REAL USER DATA ]
• Uses hardcoded 'Role' rules  • AR(1) parameters stabilize   • Exact sweet-spot density
• Centers on static anchor     • 50/50 blend of prior & hand   • True multi-knob covariance
• Generic user speed (2c)      • Momentum matches your hand    • Custom dynamics (DMD)
(0 to 5 seconds of touching)    (10 to 30 seconds of moving)    (1+ minutes of active play)
```

| Component | What it Feels Like | Underlying Reality | Nature |
| :--- | :--- | :--- | :--- |
| **Cold-Start Ladder** | "It knows my taste immediately." | Uses hardcoded keyword roles (`"cutoff"`, `"decay"`) from `ParamRoles.h`. | **85% Prior Illusion** |
| **Release Momentum** | "It predicts where I was going." | Damped Newtonian velocity decay over the last $100\text{ ms}$. | **50% Kinematic Extrapolator** |
| **Un-Moved Anchor** | "It makes safe musical choices." | Gaussian spike centered at the untouched knob position ($\sigma = 0.04$). | **90% Safe Fallback** |
| **Dwell Density $p(x)$** | "It always returns to my favorite sound." | Exact mathematical Langevin sampling on your histogram. | **100% Empirical Data** |
| **Cadence $\theta, \sigma$** | "It moves with my rhythm." | Autoregressive AR(1) calibration matching your nervous system's speed. | **100% Empirical Data** |
| **Joint Manifold (DMD)** | "It turns knobs in musical combinations." | Eigenvector decomposition over the cross-parameter covariance matrix. | **100% Empirical Data** |

---

## 4. Complete Prediction Node Taxonomy

```
                                  INFINITE PREDICTION CATEGORY
                                               │
             ┌─────────────────────────────────┼─────────────────────────────────┐
             ▼                                 ▼                                 ▼
      [ PARAMETER LEVEL ]              [ MULTI-KNOB CLUSTER ]            [ SIGNAL / NOTE LEVEL ]
       Predictive Drift                 Predictive Macro                  Predictive Modulator
       (Langevin SDE)                    (Covariance / DMD)               (Takens Delay DMD)
```

### 1. Predictive Drift (`DriftNode`)
* **Role:** Drives individual or multiple parameters via dedicated green cables.
* **Core DSP:** Langevin SDE on dwell landscape + Damped Momentum.
* **Modes:**
  * **Wander (Default):** Natural continuous drift among favorite sweet spots.
  * **Follow:** Lagged ridge regression on velocities ($\Delta x$) to mirror leader parameters.
  * **Recall:** Nearest-neighbor segment matching based on per-bar summary indices.
* **Controls:** `speed`, `stray` (temperature), `momentum`, `link`, `rangeOverride`, `quantizeRate`, `smoothness`.

### 2. Predictive Macro (`MovesNode`)
* **Role:** Master performance slider that coordinates complex clusters of controls.
* **Core DSP:** Principal joint covariance projection.
* **Behavior:** Instead of requiring manual bezier curve drawing, it projects your single macro movement through the multidimensional path you actually used during your performance.
* **Readout:** Live % of group variance explained.

### 3. Predictive Modulator (`PredictiveModulatorNode`)
* **Role:** Single-input, single-output dynamic oscillator and pattern generator.
* **Core DSP:** Takens Delay Embedding + Stabilized Dynamic Mode Decomposition (DMD).
* **Workflow:**
  1. Patch any source (LFO, Envelope, manual knob) into `in`.
  2. Click **Learn** for $3\text{–}5\text{ s}$ to capture the dynamical phase portrait.
  3. Click **Stop** — the node now free-runs as an autonomous, evolving oscillator.
* **Parameters:**
  * `speed` ($0.10\times \dots 10.0\times$): Time-scale cadence factor.
  * `low` / `high` ($0.0 \dots 1.0$): Modulation window clamping and waveform inversion ($low > high$).
  * `fitData` (Base64): Serialized transition matrix $A$, spectral radius $\rho(A)$, and noise vector.

### 4. Predictive Notes (`PredictiveNotesNode`)
* **Role:** MIDI and musical event generator.
* **Core DSP:** Variable-order Markov transition trees on pitch intervals, velocity distributions, and rhythmic onset grids.
* **Behavior:** Generates stylistic melodic and rhythmic variations that harmonize with incoming sequences.

---

## 5. Summary & Best Practices

1. **Perform First, Predict Second:** Prediction nodes feed on human movement. Jiggle, sweep, and perform your parameters during patch creation to give the models rich, high-confidence empirical data.
2. **Shift-Grab to Guide:** If a predictive cable wanders somewhere you don't like, hold **Shift** and drag the knob to where it belongs. The system receives a $3\times$ weighted correction and seamlessly re-anchors its trajectory upon release.
3. **Use Predictive Modulator for Organic Movement:** Use `PredictiveModulator` to capture human modulation gestures, then scale `speed` and `low`/`high` to replace repetitive, sterile LFO cycles with living, evolving motion.
