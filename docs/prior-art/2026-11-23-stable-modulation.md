# Prior art: automatic stability certificates for filters under modulation

Date: 2026-11-23. Read-only research. Papers, abstracts and documentation only; no source code opened.
Study: `docs/research/stable-modulation`.

## Problem
Has anyone published or shipped a compiler pass, DSL feature or tool that proves a user-written recursive
filter stays bounded while its parameters are modulated (time-varying stability), without a hand proof per
filter?

| # | Source | What it is | Status | Their method | Opened? |
|---|---|---|---|---|---|
| 1 | Laroche, "On the Stability of Time-Varying Recursive Filters", JAES 55(6):460-471, 2007. https://www.aes.org/e-lib/browse.cfm?elib=14168 | Two state-transition-matrix criteria for BIBO stability of time-varying filters; topology comparison | Published | Criterion 1: state matrix norm < 1; Criterion 2: a state transform | Abstract only (paywall) |
| 2 | Werner & McClellan, "Time-Varying Filter Stability and State Matrix Products", DAFx20in22. https://www.dafx.de/paper-archive/details/RdjkWSf80p3vUeP3_STX_w | Extends Laroche's Criterion 1 to the norm of a product of state matrices over several steps | Published | Sufficient criterion; case studies on known filters | Full text (pdftotext) |
| 3 | McClellan, "Stability Analysis of Time-Varying Virtual Analog Filters", DAFx26 (MIT, Sept 2026). https://www.dafx.de/paper-archive/2026/papers/DAFx26_paper_28.pdf | CQLFs for trapezoidally discretised filters; a continuous-time CQLF carries over to the discretised filter (Thm 3.1). Any stable continuous filter is stable under arbitrary cutoff modulation (Cor. 4.1) | Published 2026 | SVF (Prop. 4.2), Sallen-Key k < 2 (4.4), Moog k ∈ [0, 2.88] stable (4.6), unstable at 2.89 by alternating k (4.7), diode ladder k ∈ [0, 8.90] (4.9), unstable at 8.91 (4.10). P matrices by hand or found "with a random numerical search". Future work: parameters that change slowly from sample to sample | Full text (pdftotext) |
| 4 | Wishnick, "Time-Varying Filters for Musical Applications", DAFx 2014. https://dafx.de/paper-archive/details/yfd4LbdXr2Cywsqxfby8ug | Proves the SVF time-varying stable; speech-coding time-varying methods unsuited to music | Published | Hand proof | Search listing only |
| 5 | Rabenstein & Czarnach 1985, "Stability of recursive time-varying digital filters by state vector transformation", Signal Processing. https://www.sciencedirect.com/science/article/abs/pii/0165168485900908 | Stability depends on coefficients and on the state-space structure; a state transform at coefficient changes restores it | Published | Structural | Abstract only |
| 6 | Control theory: common quadratic Lyapunov functions via LMIs; parameter-dependent Lyapunov functions with bounded rate (LPV systems); joint spectral radius (switched systems) | General theory for linear parameter-varying and switched systems | Textbook | LMI / SDP | Search listings; standard results |
| 7 | Faust libraries (vaeffects). https://faustlibraries.grame.fr/libs/vaeffects/ | Ladder feedback "between 0 and 4" documented as a range | Shipped | A comment, not a check | Doc page in search |
| 8 | KVR forum threads on fast filter modulation (2024). https://www.kvraudio.com/forum/viewtopic.php?p=8874711 | Practitioners: BIBO alone isn't enough under fast modulation; ZDF/TPT used as the safe structure | Forum | Choice of structure | Search listing |

## Patterns
- Every proof found is for a named, fixed filter, done by a person. The most recent (McClellan 2026) uses a
  numerical search for P but by hand, per filter, outside any tool.
- Safety in shipped tools comes from structure (TPT/ZDF) and documented ranges, not from checking code.
- The maths for automation (LMI for CQLF, rate-bounded parameter-dependent Lyapunov functions) is standard
  in control theory.

## Verdict
- A compiler pass that certifies stability of user-written filters under modulation: **not found.**
- A slew-limited certificate for audio filters: named as future work by McClellan 2026, not found done.
- Searches run (2026-11-23): DAFx 2022-2026 archive queries; "automatic stability verification
  time-varying recursive digital filter Lyapunov LMI"; "compiler static analysis prove stability
  user-defined audio filter DSL Faust Cmajor"; Laroche 2007 follow-ups. Absence in these searches is not
  proof of absence.
