# Infinite-Turbo next (0.70): estratégia de trabalho

Documento vivo. Toda sessão que trabalhar na `turbo/next` lê este arquivo e o
[backlog](NEXT-BACKLOG.md) antes de começar, e atualiza o checklist da seção 8 ao terminar.
Os IDs (F1, P3, X1...) são os do backlog.

## 1. Modelo de branches

```
turbo/windows-only  (v0.62.0, estável; só hotfix)
   └── turbo/next   (integração da 0.70)
         ├── feature/f1-perftest
         ├── feature/p1-compute-backend
         └── ...
```

- `turbo/next` sai de `turbo/windows-only` em v0.62.0. Ninguém commita direto nela, exceto docs
  de planejamento e merges.
- Cada tarefa: `feature/<id>-<slug>` a partir de `turbo/next`. Ela volta para a `turbo/next` com
  `git merge --no-ff` depois do gate (seção 4) e do aval do dono.
- Hotfix para a versão estável vai na `turbo/windows-only` e é mergeado na `turbo/next` no mesmo
  dia, para a divergência não crescer.
- Marcos internos ganham tag local (`v0.70.0-m1`, `-m2`...), sem push sem aval.
- Fim: `turbo/next` → `turbo/windows-only`, tag `v0.70.0`.
- Até 3 sessões escrevendo em paralelo, cada uma no próprio worktree e em pastas disjuntas
  (`src/pop` × `src/app` × `src/rtx` × `src/nodes/Render3D*`).

Recomendação: o backlog inteiro é trabalho de meses. Cada marco (seção 3) deve deixar a
`turbo/next` lançável. Se a espera ficar longa, um marco pode virar 0.63/0.64 sem mudar o modelo.

## 2. O que o código já mostra (verificado em 2026-10-22)

| Item do plano | Estado real | Efeito no plano |
|---|---|---|
| V2 HAP | `src/core/HapDecode.cpp` já sobe BC1/BC3/BC4/BC7/YCoCg para a GPU | V2 vira medir e completar (HAP Q/Alpha, DXV) |
| V1 hwaccel | `VideoDecodeWindows.cpp:172` já cria D3D11VA | Falta só o zero-cópia (WGL_NV_DX_interop2) |
| C4 chaves de modulação | `Patch.cpp:467` já salva `mod` com `saveKey`, e o loader lê ref por chave | C4 encolhe: dar chave aos sliders condicionais do Render 3D e aos alvos de MIDI learn, gestos e Performance (ainda ordinais). Adotar o mecanismo do upstream (`FixedParamOrdinal`/`RegisterHiddenParam`) em vez de inventar outro |
| PointZ GPU × CPU | 86 operadores; varredura heurística ([pointz-gpu-status.md](../pointz-gpu-status.md)): 32 GPU, 22 CPU, 5 mistos, 23 a classificar, 4 pass-through | Cerca de metade ainda na CPU ou indefinida: P1 + P2 destravam o resto |
| F4 glGet | `glGet*` em 21 arquivos de `src` | Medir com Tracy antes de fazer o cache inteiro |
| A1 main.cpp | 422 linhas + 51 `.inl` com 58 mil linhas num TU único | Fatiar cedo, porque cada build completo custa minutos |
| Skills herdadas | `git-branch-workflow` e `infinite-code-review` apontam para `/Users/namansoni` | Criar as versões Turbo (seção 6) |

## 3. Prioridade estrutural: camadas e marcos

A ordem segue uma regra: **primeiro o que mede e protege, depois o que muda como todo o resto é
escrito, por último o que só acrescenta.** Os itens de uma camada não dependem das camadas de
cima.

| Camada | Por quê | Itens |
|---|---|---|
| L0 Gates | Sem eles, nenhuma mudança seguinte é verificável | F1, F7, C2, C3, F3, A7 (núcleo) |
| L1 Fundações transversais | Mudam o jeito de escrever código GPU e o formato salvo; fazer depois custa retrabalho | F2, F5, F6, F4, C4, A1, A5 (só código novo) |
| L2 Motor PointZ | Eixo principal do dono | P1 → P2 → P9 → P3 → P4/P5/P6/P7/P8/P10, P11 contínuo |
| L3 Infra de render compartilhada | Vetores de movimento e jitter servem TAA, FSR, SVGF, OptiX temporal e motion blur | MV (novo, ver abaixo) → R4 TAA → R5 FSR → X1 → X4 |
| L4 Features | Só acrescentam | R3, R6–R9, X2, X3, X5–X10, U1, U3, V1, V3–V5 |
| L5 Decisões | Exigem ADR aprovada | A3, X11, X12, A2 (job system), A6 |

**MV (item novo):** um buffer de vetores de movimento e jitter de câmera, comum ao Render 3D e ao
RTX. O plano original espalha isso em R4, R5, X1, X2, X4 e X9. Feito uma vez, como fundação.

### Marcos

| Marco | Conteúdo | Saída |
|---|---|---|
| **M0 Fundação** | docs e skills (seção 6), F1, F7, C3, C2, F3, A7 (doctest + PopCpu), P11 (tabela inicial) | `test-windows.bat -Perf` verde sobre a baseline; MIT verificado |
| **M1 Medir e destravar** | F2 Tracy, F5, F6, C4, A1 (fatias), F4 guiado pelo Tracy, U2 | Zero leitura síncrona no caminho por frame; build incremental mais rápido |
| **M2 PointZ compute** | P1, P2, P9, P3 | Os ~38 Simple idênticos em compute; 1 M de partículas a 60 fps |
| **M3 PointZ completo + grafo** | P4–P8, P10, A2 (ordem topológica, cook ≠ UI), A4, U1, U3 | Tabela P11 com tudo em GPU ou justificado |
| **M4 Tempo real** | MV, R4 TAA, R5 FSR, X1 SVGF, X4, X7, X10 | RTX a 60 fps em 1080p no patch de referência |
| **M5 Qualidade visual** | R3, R4 (GTAO/SSR/DoF), R6–R9, X2, X3, X5, X6, X8, X9 | Paridade raster × RTX |
| **M6 Mídia e decisões** | V1 zero-cópia, V2 (resto), V3–V5, C1, ADRs de A3/X11/X12/A6 | 0.70 lançável |

Paralelismo natural (pastas disjuntas): A1 em `src/app` corre junto com P1/P2 em `src/pop`;
V1/V3 em `src/platform` + `src/core/Video*` correm junto com qualquer marco.

### Ressalvas ao plano original

- **A6 e A8 versus R2:** mover ou reformatar arquivos herdados do Naman quebra a sincronização por
  diff (C1). Regra: `clang-format`, `clang-tidy` e a mudança de pastas valem só para código Turbo.
  Herdado fica no lugar, e o mapa herdado × turbo (C1) registra cada arquivo.
- **A5:** shaders novos já nascem em `shaders/*.glsl`. A migração dos antigos é oportunista: só
  quando o arquivo for tocado de qualquer jeito.
- **F4:** fazer primeiro o Tracy (F2) e medir quanto os `glGet*` custam de fato. Atacar só os do
  caminho por frame.
- **A2:** depende de A1 (estado global num objeto `App`). O job system é opcional e entra só se o
  Tracy mostrar CPU ociosa esperando nodes de áudio ou CPU.
- **P3:** o RNG da GPU muda o resultado visual das partículas. Manter o modo legado na CPU como
  opção salva no patch (chave nova no fim do VisitParams, padrão = legado nos patches antigos).

## 4. Gate de “pronto” por branch

1. Build Release full. Também o MIT quando a branch toca CMake, `src/gpl`, `src/rtx` ou ASIO.
2. `test-windows.bat` completo, sem falha nova além da baseline (`known-baseline.md`).
3. patchcompat sem regressão ok → crash/hang (corpus C3).
4. PERFTEST sem piora > 10% (F1, a partir do M0).
5. Migração CPU→GPU: variável de equivalência rodando os dois caminhos, diferença 0 ou tolerância
   documentada; linha na tabela P11.
6. Revisão de código (seção 5) sem achado crítico aberto.
7. CHANGELOG-TURBO.md, TODO-TURBO.md e o checklist da seção 8 atualizados.

A versão (CMakeLists, README, PACKAGE_MANIFEST) muda só no merge final, para 0.70.0.

## 5. Fluxo econômico em tokens

**Uma tarefa = uma sessão = uma branch.** Contexto curto rende mais que contexto longo.

1. **Brief antes do código.** A skill `write-fix-brief` investiga o código (grep, leitura
   pontual) e escreve o brief da tarefa: arquivos, funções, linhas, critério de aceite. A sessão
   de implementação começa só com o brief, a skill do domínio e este documento. Nada de colar o
   plano de novo.
2. **O repositório é a memória.** Este documento, o backlog, `docs/pointz-gpu-status.md` (P11) e
   as ADRs em `docs/adr/` guardam as decisões. Nenhuma sessão redescobre o que já está escrito.
3. **Modelo por tipo de trabalho:**
   - Opus: desenho e primeira instância de um padrão (P1, P2, P3, A1, A2, MV/TAA, X1).
   - Sonnet: replicar um padrão já fixado (portar os demais operadores, marcadores F3, testes, docs).
   - Haiku / agente Explore: buscas amplas e leitura de código.
4. **Padrão primeiro, lote depois.** Implementar 1–2 operadores no padrão novo, revisar a fundo,
   congelar numa skill e só então portar o resto em lote.
5. **Leitura cirúrgica.** Nunca ler `.inl` ou `main.cpp` inteiros: grep, depois só o trecho.
6. **Builds e testes em degraus.** Durante o trabalho: só o target e `-Quick`. Suíte completa,
   patchcompat e perf só no gate.
7. **Revisão proporcional ao risco.** `code-review` em medium por branch; high em P1, P2, A1, A2 e
   MV; `/code-review ultra` (disparado pelo dono) antes do merge final.
8. **Compactar nos marcos naturais** (fim de exploração, fim de implementação), não no meio de uma
   edição.
9. **GateGuard:** cada arquivo novo hoje é enviado duas vezes (o primeiro Write é bloqueado com
   o conteúdo inteiro). Pôr `docs/**` e `.claude/skills/**` em `GATEGUARD_EXEMPT_GLOBS` corta esse
   custo sem desligar a proteção do código.

## 6. Skills

### Do projeto, já existentes

| Skill | Uso na next |
|---|---|
| `run-turbo-tests` | Gate. Ganha `-Perf` (F1) e a matriz full/MIT (F7) |
| `write-fix-brief` | Brief de toda tarefa antes do código |
| `infinite-turbo-patching` | Montar patches de teste e de benchmark pelo MCP |
| `new-geometry-node`, `geometry-transform-sweep` | Render 3D, Instance, Copy |
| `new-effect-node` | Passes de post (R4) que viram node |
| `audio-node-sweep` | Regressão de áudio durante A1 e A2 |

### A criar no M0 (maior retorno)

| Skill nova | Conteúdo |
|---|---|
| `turbo-branch-workflow` | Substitui `git-branch-workflow`: `turbo/next`, feature branches, gate da seção 4, hotfix merge-forward, commit só com aval |
| `turbo-code-review` | Versão Turbo de `infinite-code-review`: regras R1–R6, AsyncRows/AsyncReadback, zero `glGet` e zero leitura síncrona por frame, ordinal de slider, licenças |
| `pointz-gpu-port` | Procedimento de P1–P10: KernelSpec → compute, SSBO, fallback, variável de equivalência, linha na P11, medição com Tracy. Vai ser usada ~40 vezes |
| `render-gpu-pass` | Regras de passes do Render 3D, post e RTX: GlState, debug groups, alvos HDR, MV/jitter, chave de slider (C4), paridade raster × RTX |
| `rtx-module` | Build OptiX/CUDA 11.8, embed, stub MIT, limites de VRAM, RTXPROF |

### Genéricas instaladas (plugin ecc e embutidas)

- `ecc:cpp-reviewer` (agente) e `ecc:cpp-coding-standards`: revisão de C++ moderno, memória e
  concorrência, junto com `turbo-code-review`.
- `ecc:architecture-decision-records`: ADRs de A3 (Vulkan), A2, A6, C4 e X11.
- `ecc:benchmark-methodology`: desenho do F1 (aquecimento, mediana, variância, limiar de 10%).
- `ecc:search-first` e `ecc:documentation-lookup`: ler a documentação atual de OptiX, FFmpeg, FSR,
  OIDN e Tracy antes de integrar.
- `ecc:silent-failure-hunter`: caminhos assíncronos e de fallback (readbacks, compute → TF, X10).
- `ecc:verification-loop`: checagem antes de declarar uma tarefa pronta.
- `code-review` e `simplify` (embutidas): revisão por branch e limpeza.

### Ferramentas externas (não são skills)

Tracy (BSD-3), RenderDoc, Nsight Graphics e Nsight Systems, glslangValidator, clang-tidy e
clang-format (só código Turbo).

## 7. Riscos

| Risco | Mitigação |
|---|---|
| Branch longa diverge da estável | Hotfix merge-forward no mesmo dia; marcos lançáveis |
| A1 conflita com tudo em `src/app` | Fatias pequenas, sem outra branch aberta em `src/app` ao mesmo tempo |
| Migração GPU muda o resultado | R5: equivalência obrigatória, CPU como fallback |
| Partículas com RNG novo | Modo legado salvo no patch |
| Ordinais de slider deslocados por features novas | C4 no M1, antes de R/X acrescentarem sliders |
| Licença | Toda dependência nova entra em `THIRD_PARTY.md` com versão, licença e edição |
| Escopo | Cada marco lançável sozinho |

## 8. Checklist

Marcar `[x]` ao mergear na `turbo/next`. Nome da branch entre parênteses.

**M0:** [x] skills novas (turbo-branch-workflow, turbo-code-review, pointz-gpu-port; render-gpu-pass e
rtx-module ficam para o M4) · [x] F1 (A/B contra `build\perf-ref`) · [x] F7 · [x] C3 · [x] C2 (doc + chaves
desconhecidas preservadas no save) · [x] F3 · [x] A7 núcleo (doctest; patch I/O primeiro, PopCpu depois) · [x] P11 tabela inicial
**M1:** [x] F2 (sem TracyCUDA ainda) · [x] F5 · [x] F6 (bounds; Image Spectral Synth e Wave Terrain ficam: herdados, leem só quando a textura muda) · [ ] C4 ·
[ ] A1 fatias · [ ] F4 · [ ] U2

Tudo acima está local e sem commit na `turbo/next` (instrução do dono, 2026-10-22).
**M2:** [x] P1 (compute padrão, TF fallback; equivalência 0; −23% de frame) · [~] P2 (scan, compactação,
gather; faltam sort, reduções genéricas, histograma) · [ ] P9 · [x] P3 (motor GPU, `engine`; patch de
referência 19,1 → 9,7 ms; simulação de 1M partículas em 1,8 ms; PartDeath com contagem lida da GPU)
**M3:** [ ] P4 · [x] P5 na 0.70 (Delete, Merge, Normal suave, Facet, Subdivide e Extrude de linhas, Revolve na GPU;
Subdivide/Extrude de faces via MeshOps ficam na CPU → 0.71; "Sweep" não existe como operador PointZ) · [x] P6 na 0.70
(Line Smooth, Line Divide, Line Resample por contagem na GPU; por comprimento fica na CPU, depende de readback) · [ ] P7 · [ ] P8 · [ ] P10 · [ ] A2 · [ ] A4 · [ ] U1 · [ ] U3
**M4:** [~] MV (câmera, pela profundidade; falta o movimento próprio por vértice) · [x] R4 TAA (com cobertura do
fundo; TAATEST) · [~] R5 (FSR 1 EASU + RCAS, raster e RTX; FSR 2 temporal não) · [ ] X1 · [~] X4 (render scale do RTX
com FSR 1: 17,1 → 10,3 ms em 2x) · [ ] X7 · [ ] X10
**M5:** [x] R1 na 0.70 (culling de frustum + LOD em quad por instância na GPU, indirect draw, CULLTEST; cull/LOD também
na sombra com `lod pixels`; automático só com 32k+ cópias e ¼ da caixa fora da tela; −28% de frame com LOD 4 px;
multi-draw único e Hi-Z → 0.71) · [ ] R3 · [ ] R4 resto · [ ] R6 · [ ] R7 · [ ] R8 · [ ] R9 · [ ] X2 · [ ] X3 · [ ] X5 · [ ] X6 · [ ] X8 · [ ] X9
**M6:** [ ] V1 · [ ] V2 resto · [ ] V3 · [ ] V4 · [ ] V5 · [ ] C1 · [ ] ADRs A3/X11/X12/A6
