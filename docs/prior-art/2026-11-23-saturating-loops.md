# Prior art: stability of saturating feedback loops under modulation (C1)

Searched 2026-11-23 for `docs/research/saturating-loops`. **OPENED** means the full text was read (PDF text extracted).
**Seen in search only** means the title and abstract were seen in search results; do not cite these as verified.

## Audio

| Source | Status | What it shows | Gap |
|---|---|---|---|
| T. Hélie, "Lyapunov stability analysis of the Moog ladder filter and dissipativity aspects in numerical solutions", DAFx-11, Paris | OPENED | Continuous-time Moog ladder, linear and tanh versions. The natural (capacitor) energy proves stability for **time-varying** cutoff and feedback gain r in [0, 5/12), i.e. k = 4r < 5/3. A second storage function recovers the full r < 1 only for **time-invariant** r. The tanh case is covered exactly for the first function. The bilinear-transform simulation is not guaranteed dissipative for r > 5/12 | Continuous time, by hand, one circuit. The discrete scheme is not proved |
| M. Danish, S. Bilbao, M. Ducceschi, "Applications of port Hamiltonian methods to non-iterative stable simulations of the Korg35 and Moog 4-pole VCF", DAFx20in21 | OPENED | Port-Hamiltonian models of both filters; zero-input Lyapunov stability over the circuits' parameter ranges, and a non-iterative discrete scheme that keeps it. For the Korg35 the argument holds with time-varying parameters; for the Moog it does not extend, because the storage function depends on the resonance | Per circuit, by hand; Moog under modulation still open |
| J. Chowdhury, "Stable structures for nonlinear biquad filters", DAFx2020 | OPENED | Two nonlinear biquad structures, stable by a Lyapunov argument when every nonlinearity has slope ≤ 1 (tanh, clippers) | Fixed structures, not user code; time variation not proved |
| S. Bilbao, "Lyapunov function for the nonlinear Moog voltage controlled filter", arXiv 2104.04376 | Seen in search only | A Lyapunov function for the tanh Moog, time-invariant parameters only | No modulation |
| D. Rossum, saturating nonlinearity in a 2-pole all-pole feedback path (1992); DAFx-15 submission 68 (time-varying coefficients with clipping in an IIR loop) | Seen in search only | Saturation in the feedback path makes digital filters behave like analog ones | Not a stability proof |

## Control theory

| Source | Status | What it shows |
|---|---|---|
| Discrete-time Lur'e systems with sector-bounded nonlinearities: Lyapunov functions giving global stability as LMIs; local stability under saturating feedback (hal-00717576) | Seen in search only | The maths for x⁺ = A x + B φ(C x) with φ in a sector, as LMIs |
| Ahmad, Heath, Li; Kim, Braatz: discrete-time Lur'e with slope-restricted, odd, monotonic nonlinearities (arXiv 1509.01302) | Seen in search only | Lur'e-Lyapunov functions that use slope bounds, less conservative than sector bounds |
| Carrasco, Heath et al.; Ahmad, Carrasco, Heath (CDC 2014); Turner, Drummond (2021): LMI searches for discrete-time Zames-Falb multipliers | Seen in search only | The least conservative known tests for slope-restricted loops (FIR multipliers) |
| De Caigny et al.: LMI stability of discrete-time LPV systems on multi-simplex parameter sets | Seen in search only | Parameter-dependent Lyapunov functions for time-varying linear parts |
| Mills, Mullis, Roberts, "Digital filter realizations without overflow oscillations", 1978 | Seen in search only | State-space realizations whose saturation (overflow) arithmetic can't sustain oscillation, regardless of pole location |

## Not found

No compiler, DSL or audio tool that certifies stability of user-written filters with saturation inside a
modulated loop. Faust and Cmajor were searched for `stable-modulation` and found nothing; nothing new was
found here. Absence in a search is not proof of absence.
