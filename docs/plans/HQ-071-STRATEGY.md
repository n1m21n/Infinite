# 0.71: Render 3D HQ (referência: Google Filament) + higiene de desempenho

Estratégia de trabalho. Pedido original: o material "Render 3D HQ baseado no Google Filament"
(dois nós irmãos: Render 3D preservado, Render 3D HQ novo) + uma varredura do código por coisas que
deixam o sistema lento. Dividido em sessões curtas e independentes, para alternar entre IAs sem
perder o fio. Leia isto, o `HANDOFF.md` e a tabela de estado (seção 6) antes de cada sessão.

## 1. Diagnóstico (o que já existe, para não refazer)

O material assume um Render 3D "do Naman" intocado. Não é bem assim: na 0.61-0.70 o Turbo já pôs
dentro dele HDR, IBL split-sum (GGX pré-filtrado), clearcoat, sheen, iridescência, TAA, FSR 1,
vetores de movimento (câmera + objetos + PartId), culling/LOD na GPU, passe de velocidade, e criou o
nó **Render 3D FX** (AO, SSR, DOF, motion blur McGuire). Isso muda o plano em três pontos:

1. **Não começar do zero.** Os módulos Turbo já isolados (`Render3DTemporal`, `Render3DMotion`,
   `Render3DCull`, `GpuAlgo`, `Render3DPost` para IBL/bloom) viram o "kit" compartilhado do HQ. O HQ
   tem shaders, FBOs, histórico e estado próprios; o kit dá os algoritmos já testados.
2. **"Preservar o Render 3D" = congelar a partir de agora.** Nenhum recurso novo entra nele. Se o
   kit precisar de refactor para servir aos dois, a regra é: mesma saída no Render 3D (`CULLTEST`,
   `TAATEST`, `R3DFXTEST`, compat de 43 patches e perf A/B provam isso).
3. **O ganho real do HQ está onde o Render 3D é fraco de verdade** (confirmado no código):
   partículas são quads com normal apontando para a câmera (`uIsSprite == 1` no vertex shader:
   iluminação chapada, borda serrilhada, sombra de disco); o BRDF usa Schlick e um Smith simples,
   sem compensação de multi-espalhamento nem specular AA; o AO do FX é só por profundidade (sem
   normais, sem GTAO); sombra é um mapa único com PCF fixo; o bloom é simples.

## 2. Decisões de arquitetura (tomadas; mudar só com medida)

- **Forward com pré-passe de profundidade + normais** (MRT: normal + velocidade). Clustered não se
  paga com ≤ 8 luzes (o limite atual do Infinite). O pré-passe alimenta GTAO, contact shadows e TAA,
  e corta overdraw do passe principal (o gargalo medido do Render 3D).
- **Portar técnicas e funções de shader do Filament, não a engine.** Apache 2.0: arquivo de origem
  e revisão no topo de cada shader portado, entrada em `THIRD_PARTY_NOTICES.md`, revisão do Filament
  registrada no doc (S0).
- **Partículas em dois caminhos dentro do HQ**, escolhidos por tamanho projetado (LOD):
  *Sphere PBR* (impostor esférico: normal, profundidade com `gl_FragDepth` num programa só de
  partículas, AA analítico da borda com `fwidth`) e *GPU Points HQ* (poucos px: pontos com
  cobertura, iluminação aproximada, sem quad). Culling/LOD pelo `Render3DCull` existente.
- **Sombras**: mapa direcional estável (texel snapping) + PCF Poisson/PCSS-lite; partículas entram
  no mapa como discos (barato), o "volume" vem do impostor + AO; contact shadows em espaço de tela.
- **Opção de build** `INFINITE_ENABLE_RENDER_HQ` (Windows), tudo em `src/renderhq/` +
  `Render3DHQNode`. Desligada: o nó não registra, e um patch com ele mostra "Render 3D HQ
  indisponível nesta build" (o caminho de nó desconhecido já preserva as linhas do patch).

Parâmetros iniciais do Filament (Options.h): GTAO raio 0.3, power 1, intensidade 1, meia resolução,
4 fatias × 3 passos, bilateral médio; TAA feedback 0.12, Halton(2,3)×16, variance clipping γ 0.7,
HDR; bloom 6 níveis, força 0.10. BRDF: D_GGX, V_SmithGGXCorrelated, F_Schlick, Lambert, DFG LUT
(multi-espalhamento), specular AA geométrico. Conferir cada um no código do Filament em S0.

## 3. Sessões

Cada sessão termina compilando, com o gate verde (`driver.ps1 -Perf -Compat` e `-Mit -Quick`), com
o check novo da sessão passando e com a tabela da seção 6 atualizada. Uma sessão por conversa.
Tamanho: P (pequena), M, G (grande; partir em duas se o token estiver curto).

| # | Sessão | Saída verificável | Tam. | Quem |
|---|---|---|---|---|
| S0 | Auditoria + baseline + estudo Filament | `docs/RENDER-3D-HQ-FILAMENT.md` (esqueleto, revisão do Filament, mapa de interfaces), cenas A-E em `examples/hq/`, capturas BEFORE do Render 3D e perf das cenas, harness de captura `HQCOMPARE`, skill `render-hq` | M | agentes baratos buscam e montam cenas; o modelo forte revisa |
| S1 | Nó Render 3D HQ (esqueleto) | opção CMake, nó registrado, FBOs HDR próprios, pré-passe, saída de textura, coexistência no mesmo patch; check `HQTEST` (renderiza, sem erro GL, Render 3D igual no compat) | P | qualquer |
| S2 | PBR Filament | BRDF portado, DFG LUT, IBL com o mapeamento roughness→lod do Filament, specular AA, normal map, clearcoat/sheen; adaptador do `Material`; cena A BEFORE/AFTER | G | forte |
| S3 | Impostores esféricos | Sphere PBR: normal, profundidade, AA de borda, modos Flat/Soft/PBR Sphere/Emissive; cena B | M | forte |
| S4 | Sombras + contact shadows | mapa estável, PCF/PCSS-lite, partículas no mapa, contact shadows; cenas B/E | G | forte |
| S5 | GTAO | meia resolução, bent normals, bilateral, aplicado só no indireto, modo debug; cenas A/E | M | forte |
| S6 | TAA Filament | jitter Halton 16, YCoCg + variance clipping, rejeição por profundidade/disocclusion, partículas por PartId/P anterior (kit `Render3DMotion`); teste de ghosting com movimento rápido | M | forte |
| S7 | GPU Points HQ + 1M | LOD por tamanho projetado, culling, sem readback; benchmarks 100k/1M/2M estáticos e animados | G | forte |
| S8 | Pós + presets | bloom 6 níveis, tonemap (os do Infinite + o do Filament), exposição, presets Performance/Balanced/High com parâmetros reais e diferença medida | M | qualquer |
| P1 | Varredura de lentidão (fora do HQ) | relatório medido + correções (seção 4) | M | **pode rodar em paralelo** com S2-S8 (arquivos diferentes) |
| S9 | Validação final + release | cenas A-E BEFORE/AFTER, benchmarks medidos, doc completo, exemplos `.inf`, revisão de código, release com aval | M | forte revisa, barato monta |

Ordem: S0 → S1 → S2 → S3 → (S4, S5, S6 em qualquer ordem) → S7 → S8 → S9. P1 a qualquer momento.
O caminho mínimo que já entrega a diferença visível é S0-S3 (materiais e partículas de verdade).

## 4. P1: varredura de lentidão do sistema todo

Medir antes de mexer (`INFINITE_FRAMEPROF=1`, `INFINITE_GPUPROF=1`, perf A/B no corpus). Pistas já
vistas, a confirmar:

- `glGet*` por chamada nos helpers de GPU (`GpuAlgo::Scope`, `PopGpu::StateGuard`,
  `Render3DPost`): sincronizam com a thread do driver. Cache de estado (item F4, GlState).
- `glGetUniformLocation` a cada frame em Render 3D, Temporal, Cull, Motion e FX: cachear por programa.
- No patch de 1M cubos, o FRAMEPROF atribuiu ~13 ms de GPU à etapa "modulation": investigar
  (espera de GPU contada no lugar errado, ou trabalho real).
- Previews 3D dos nós (1,1 ms no patch de referência) e corpos de nó (1,8 ms): limitar frequência e
  resolução das miniaturas fora de foco.
- Leitura de 4 bytes do `PartDeath` (partículas na GPU): contagem de um frame atrás.
- Varredura estática barata (agente): `glFinish`, `glReadPixels`, `glGetBufferSubData`, `.Read(` em
  caminho por frame; alocação por frame dentro de cook; locks na thread de áudio.

Saída: tabela antes/depois por item no CHANGELOG; nada entra sem medida.

## 5. Economia de tokens e alternância entre IAs

- **Modelo forte**: decisões, shaders, revisão final. **Agentes baratos** (Explore /
  general-purpose em modelo menor): buscas, auditorias estáticas, montar cenas `.inf`, rodar
  capturas, tabelas e docs a partir de dados. Pedido ao agente sempre com: arquivos, o que devolver
  (resumo curto, não o conteúdo inteiro), critério de pronto.
- **Skill `render-hq`** (criada em S0): as regras deste plano como checklist (isolamento, licença,
  checks obrigatórios, onde fica cada coisa), para qualquer IA seguir sem ler a conversa.
- **Checkpoint por sessão**: só termina com build verde e a seção 6 atualizada. Se o token acabar no
  meio, a próxima IA lê a seção 6 e o `git diff` e continua; nunca deixar o build quebrado entre
  sessões (se precisar parar, `git stash push -m "<sessão>"`).
- **Prompt de passagem**: ao fim de cada sessão, o bloco "próxima sessão" no `HANDOFF.md` (o que
  fazer, arquivos, check de pronto), pronto para colar em outra IA.
- **Medir em vez de discutir**: toda decisão de desempenho com número do perf A/B ou do GPUPROF.

## 5b. Política de branch (decidida pelo dono em 2026-10-23)

Branch `feature/render3d-hq` a partir de `turbo/next` (3adf2a78). **Nenhum commit até a S3 estar ok**
("commit soh depois de S3 estar ok"): S0-S3 ficam no working tree, e a passagem entre IAs é por este
arquivo e pelo `HANDOFF.md`. Depois da S3, um commit por sessão, sem push até o aval; no fim, merge em
`turbo/next` → `turbo/windows-only` + tag `v0.71.0`.

## 6. Estado das sessões

| Sessão | Estado | Data | Check | Notas |
|---|---|---|---|---|
| S0 | **feita** | 2026-10-23 | cenas A-F carregam e renderizam; BEFORE capturado | doc `RENDER-3D-HQ-FILAMENT.md` (mapa, achados, baseline), `filament-notes.md` (Filament @ 13be3649), cenas `examples/hq/` + F (1M), captura = `capture.ps1` + `INFINITE_DUMPTYPE` (no lugar do `HQCOMPARE`), skill `render-hq` |
| S1 | **feita** | 2026-10-23 | `HQTEST` ok com HQ ON e OFF (stub); `CULLTEST`, `TAATEST`, `R3DFXTEST` ok; cenas A-F com HQ capturadas | nó `RenderHqNode` (deriva de `Render3DNode`), módulo `HqRenderer` atrás de `INFINITE_ENABLE_RENDER_HQ`, pré-passe + HDRI + sombreamento provisório + `Render3DPost`; cozimento do HQ 0,2-0,3 ms (CPU). Achado: Sphere/Cylinder/Cone/TorusKnot/Helix com enrolamento invertido (doc §4), aguarda decisão do dono |
| S2 | **feita** | 2026-10-23 | `HQTEST` (com metal + clearcoat + sheen), `R3DFXTEST`, `CULLTEST`, `TAATEST` ok; suíte completa 71/72 antes do ajuste do `R3DFXTEST` (AO), depois ok; cena A BEFORE/AFTER | BRDF do Filament, DFG multiespalhamento + sheen, IBL próprio com o mapeamento do cmgen, specular AA, clearcoat/sheen, AO do Material, adaptador do Material (8 mapas, Mapping, bump); antes disso, correção do enrolamento de 6 primitivas + `TubeAlong` (OK do dono) e BEFORE recapturado. HQ 0,2-0,4 ms (CPU) |
| S3 | **feita** | 2026-10-23 | `HQTEST` (10k esferas, PointZ em triângulos, órbita + objeto em movimento), capturas B-F, gate completo | impostores esféricos lidos direto dos buffers da PointZ, quad no plano tangente com `depth_greater`, sombreados pelo PBR da S2; PointZ com triângulos pela GPU. F (1M): HQ só 14,4 ms contra 17,4 do Render 3D só |
| S4 | **feita** | 2026-10-23 | `HQTEST` (sombras liga/desliga, mapa reaproveitado parado, órbita + objeto em movimento), capturas B/E, gate | mapa estável ajustado aos casters (chão não alarga), PCF LOW do Filament / PCSS-lite, partículas como point sprites no mapa, contact shadows opcionais (desligadas, como no Filament); saída antecipada das esferas escondidas (F só HQ 14,9 → 13,6 ms). Mapa +0,4 ms na F |
| S5 | **feita** | 2026-10-23 | `HQTEST` (AO liga x desliga, "só AO" com ocluídos e abertos, custo), `CULLTEST`, `TAATEST`, `R3DFXTEST`; capturas A/E; perf F | GTAO do Filament em meia resolução (cadeia de profundidade, kernel 4x3, blur bilateral, upsample bilateral), só na luz indireta; raio padrão 1,0 (não 0,3 m: escala das cenas); cadeia sem grade rotacionada (listras no chão rasante). F só HQ +0,5 ms (12,66 → 13,2). Antes: correção da sombra do Render 3D embaixo dos objetos (commit 0a370ae5) |
| S6 | **feita** | 2026-10-23 | `HQTEST` (convergência, bordas, sem ghosting após movimento rápido, cintilação de objetos pequenos 2,7x menor), `TAATEST`, `R3DFXTEST`; capturas filament/; perf F | TAA do Filament (`hq_taa.comp`: Catmull-Rom, Lanczos-2, caixa 5/9 com clipping preciso, feedback 0,12) sobre o jitter e os vetores de movimento do kit; velocidade por objeto e por partícula (`PartVel`) no pré-passe; GTAO com ruído girando no tempo; fundo liso agora opaco (alpha 1). +0,74 ms na F. Antes: exemplos separados em raster/ e filament/ (commit fdca788b), pedido do dono |
| S7 | **feita** | 2026-10-23 | `HQTEST` (Instance PointZ 10k, cena parada reaproveitada / redesenhada, todos os anteriores), `CULLTEST`, `POPTEST`; benchmarks 100k/1M/2M | perfil antes: 5 ms da F eram do Noise PointZ (P1); esferas e instâncias sem mapas com sombreamento diferido (passe de tela cheia por slot: 3,5 → 0,2 ms na F); Instance PointZ no HQ; cena parada reaproveitada depois de o TAA assentar. HQ <= raster em tudo: esferas 2M 18,0 x 30,9 ms, cubos 1M 13,4 x 15,6 ms. Cena nova `I_instances_100k` |
| S8 | **feita** | 2026-10-23 | `HQTEST` (bloom, 7 tonemaps, RCAS, GTAO cheio, 3 presets com custo), `TAATEST`, `R3DFXTEST`, `CULLTEST`; capturas filament/ (G e H idênticas bit a bit sem bloom); perf dos presets | acabamento próprio `HqPost` (o `Render3DPost` fica intocado): bloom do Filament (threshold + cadeia 9/13 taps + tenda 3×3, antes do tonemap), tonemaps do Render 3D + Filament ACES legacy / ACES, render scale com o FSR do kit, RCAS pós-TAA (`Sharpen`, função nova do kit), viés de mip −1 / −log2(razão), GTAO em resolução cheia, presets Performance / Balanced / High (gravam os parâmetros reais). Bloom D 8,45 → 7,49 ms; presets na F 7,98 / 9,60 / 10,02 ms |
| P1 | **feita** | 2026-10-23 | `POPTEST`/`POPTFTEST`/`POPEQUIVTEST` (Noise curl × kernel anterior, tolerância 1e-4, medido 1,8e-5), `PARTICLEGPUTEST` (motor CPU, cópia por linhas × cópia inteira: bit a bit), `GPUALGOTEST`; perf A/B contra o executável da S7 | medido primeiro (GPUPROF): **Noise PointZ** curl 4D com os 16 gradientes da célula reaproveitados entre as amostras ±e (F: 4,88 → 1,31 ms GPU); **Particle PointZ motor CPU** copiava a fonte GPU inteira de volta todo quadro (305k pontos com `inherit`): agora sorteia os nascimentos adiantado e copia só essas linhas (3,57 → 0,20 ms). Quadro inteiro: patch de referência do dono **14,64 → 10,11 ms (−31%)**, F do HQ 9,65 → 5,68 (−41%), F do raster 15,76 → 11,92 (−24%), I 3,40 → 3,02. Não mexido, com o motivo: `glGet`/`glGetUniformLocation` por quadro (os quadros medidos são limitados pela GPU; fica para o F4), "modulation" de ~13 ms (é trabalho real de GPU das fontes, o GPUPROF confirma), Preview 3D 2,1 ms de GPU no patch de referência (já atualiza um quadro sim, outro não; menos que isso é decisão de UX), leitura de 4 bytes do `PartDeath` (só com morte pela cadeia de forças), "opaque slot" do Render 3D (congelado) |
| S9 | **feita**; S9b (retorno do teste do dono) feita; **0.71.0 lançada** (merge em `turbo/next` e `turbo/windows-only`, tag `v0.71.0`, sem push) | 2026-10-23 | capturas BEFORE/AFTER das 9 cenas olhadas lado a lado (`build/hq/s9_before`, `s9_after`); `HQTEST` (+ instâncias cobertas por malha forward), `TAATEST`, `R3DFXTEST`, `CULLTEST`; perf das 18 cenas intercaladas; revisão de código da S8 e da P1; gate | **defeito achado nas capturas**: na cena I o chão saía cor de cobre entre as instâncias (desde a S7: malha forward sobre uma diferida não regravava o id do slot); corrigido, check novo falha sem a correção (0,651) e passa com ela (0). HQ <= Render 3D em 8 de 9 cenas (F 11,86 → 5,61 ms, C 5,16 → 2,55; B +0,14 ms), tabela no §13 do doc. Problemas conhecidos e decisões do dono no §15 (inclui a costura do HDRI no fundo do Render 3D, achada agora, antiga). CHANGELOG 0.71 reescrito |

## 7. Riscos

- **1M partículas com sombra + GTAO + TAA a 60 fps num RTX 4060 Laptop** é meta, não promessa: só
  fecha com LOD agressivo (a maioria dos pontos a 1-3 px vira GPU Points). Medir em S7.
- **TAA com partículas que nascem e morrem**: sem correspondência temporal confiável, histórico
  conservador (rejeitar); nunca inventar vetor de movimento.
- **Refactor do kit compartilhado** pode mudar o Render 3D sem querer: só com os checks e o compat
  antes e depois.
- **Escopo**: o material é grande; S0-S3 já entregam a diferença visual que motiva o HQ. Se o tempo
  apertar, S4-S8 viram 0.72 sem perder o que foi feito.
