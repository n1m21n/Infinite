# Render 3D HQ (referência técnica: Google Filament)

Documento técnico do nó **Render 3D HQ** (0.71). Plano e sessões: `docs/plans/HQ-071-STRATEGY.md`.
Notas do Filament (fórmulas e parâmetros extraídos do código-fonte): `docs/filament-notes.md`.
Este arquivo cresce a cada sessão; as seções marcadas "(S*n*)" são preenchidas na sessão *n*.

## 1. Objetivo e separação

Dois nós irmãos, independentes e coexistentes no mesmo patch:

- **Render 3D**: o renderizador do Infinite (com as extensões Turbo até a 0.70). Congelado a partir
  da 0.71: nenhum recurso novo; qualquer refactor do kit compartilhado mantém a mesma saída
  (`CULLTEST`, `TAATEST`, `R3DFXTEST`, compat de 43 patches, perf A/B).
- **Render 3D HQ**: renderizador novo do Turbo, com técnicas do Filament, focado em materiais,
  partículas, sombras, AO, TAA e pós em tempo real. Windows / OpenGL 4.6; opcional na build
  (`INFINITE_ENABLE_RENDER_HQ`).

## 2. Revisão do Filament usada

Ver o topo de `docs/filament-notes.md` (SHA e data do `main` no dia do estudo, S0). Cada shader
portado leva no topo: arquivo de origem no Filament, revisão, licença (Apache 2.0) e o que mudou.

## 3. Mapa do Render 3D atual (auditoria S0)

Referências de linha são da 0.70.0 (`33cd68dc`); confirmar antes de editar.

**Registro e pinos.** `REGISTER_NODE(Render3DNode, Render 3D, "3D")` em
`app/ui/PinsAndTheme.inl:428`; categoria do menu em `core/NodeFactory.cpp:41`. Pinos: geometria
0-3 (`kSlots`), câmera 4, luzes 5-7 (`kLightSlots`), HDRI 8 (`kEnvSlot`, único `ImageCable`).
`Render3DNode` aparece em ~20 `dynamic_cast` (contagem de pinos, `CableFor`, conectar/validar/
ligar em `Graph.inl`, desenho de cabos em `Links.inl`, salvar em `PatchIO.inl`, desligar em
`Mapping.inl`, limpeza ao apagar em `AudioGraph.inl`, layout e corpo em `Nodes.inl`, preview e
órbita em `Preview.inl`, RPC em `RpcTools.inl`, FX e Volume leem câmera/profundidade).
**Consequência para S1:** derivar o HQ de `Render3DNode` herda tudo isso de graça (e FX/Volume
funcionam nele); um irmão só de `INode` exige tocar os ~20 lugares. O despacho do corpo
(`Nodes.inl:705`) é um `else if`: o teste do HQ tem que vir antes do do Render 3D.

**Entrada de cena.** `IGeometrySource` (`Geometry3DNodes.h:186-276`): `GetMesh`, `GetPop`,
`GetPopInstances`, `GetMaterial` (struct POD em `:21-88`), texturas por canal, matriz de modelo,
`GroupChild` (Group 3D, até 64 draws), nuvem de pontos legada. `CollectDraw()` achata em `mDraw`;
`BuildSceneSignature()` decide se o quadro precisa ser redesenhado. Câmera `CameraNode`
(`SceneNodes.h:13`), luzes `LightNode` (`:50`; tipo 0 dir, 1 ponto, 2 sol, 3 ambiente, 4 spot),
HDRI `EnvironmentNode` via `envInput`.

**Dados na GPU.** `GpuMesh` por slot (`Geometry3DNodes.h:763-826`). Atributos: 0 pos, 1 normal,
2 uv, 3-6 matriz de instância, 7 cor de instância, 8 cor de vértice, 9 índice de textura, 10 índice
de instância (cull). PointZ: `BindPopAttrib` (local ao .cpp). Instâncias: `pop::InstanceMatrices`.
**Partículas/pontos**: `drawCloudSlot` desenha um quad por ponto, `uIsSprite == 1`, normal =
`normalize(uCamPos - worldPos)` (sempre de frente para a câmera: iluminação chapada), forma redonda
por `discard`, fora do passe de sombra e do passe de velocidade.

**Saída.** HDR RGBA16F (`mColorTex`) + profundidade em textura (`mDepthBuffer`, 24 bits),
MSAA por renderbuffer com resolve; `Render3DPost::Finish` faz bloom, tonemap (ACES, Reinhard, AgX,
Neutral), sRGB e dither → RGBA8. `GetOutputFormat()` não é sobrescrito (RGBA8 para os nós depois).
`RequestDepth()` / `RequestVelocity()` servem FX e Volume. O estado GL é salvo e restaurado no fim
do cook (obrigatório: os nós 2D dependem dele).

**Kit reaproveitável (código Turbo, já isolado).** `Render3DTemporal` (jitter Halton 16, MV de
câmera, TAA, FSR 1, `BuildComputeShader` com `//@include`), `Render3DMotion` (matrizes do quadro
anterior por PartId, passe de velocidade), `Render3DCull` (frustum + LOD, indirect draw),
`Render3DPost` (IBL: GGX pré-filtrado, irradiância, BRDF LUT; bloom; tonemaps), `pop::gpu`
(scan, compactação, gathers), `GLUtil` (DebugScope, profiler `INFINITE_GPUPROF`, leituras).
Shaders em `shaders/*.glsl` entram sozinhos no executável (`cmake/EmbedFiles.cmake`). Os shaders
principais do Render 3D são strings dentro de `Geometry3DNodes.cpp`: o HQ não os reaproveita.

**Build opcional.** Padrão do RTX: `option(...)`, header com fábrica + `...Built()`, `.cpp` real
sob a opção e um stub no caso contrário (`nodes/RtxStub.cpp`); o nó continua registrado e diz o
motivo no painel quando o módulo não está na build.

**Testes.** Ganchos em `app/selftest/CanvasHooks.inl` (`INFINITE_<NOME>` + `frameId`), cena 3D
comum em `app/frame/Startup.inl:1832`, lista do driver em `driver.ps1` (`'NOME:quadros'`).

## 4. Achados da auditoria (S0)

- **Partículas chapadas**: confirmado acima (normal para a câmera). Cenas B, C, D, E mostram nuvens
  brancas sem volume nem sombra.
- **Costura do HDRI**: o fundo e os reflexos mostram uma linha vertical tracejada onde a textura
  equirretangular dá a volta (a derivada salta e o mipmap cai para o nível mais baixo). Visível em
  todas as cenas com HDRI. No HQ: amostrar o equirretangular com `textureLod`/`textureGrad`.
  (No Render 3D é correção de bug, não recurso: decidir em P1.)
- **Partículas fora da sombra e do passe de velocidade** no Render 3D: não projetam sombra e o TAA
  não tem movimento para elas.
- **Primitivas com enrolamento invertido (achado na S1; corrigido em 2026-10-23 com o OK do dono).** `Primitives::
  Sphere`, `Cylinder`, `Cone`, `TorusKnot` e `Helix` (`src/core/Mesh.cpp`, código do upstream) emitem
  os triângulos em sentido horário visto de fora (0% concordam com as normais); `Pyramid` em parte
  (37,5%). As outras 14 estão certas. Com `GL_BACK` + `CCW` (Render 3D e HQ), o hemisfério da frente
  é descartado e o que se vê é o interior do de trás: silhueta certa, mas luz e reflexos invertidos em
  profundidade (uma luz de trás parece vir da frente; a metade de cima de uma esfera metálica reflete
  o chão do HDRI). Provado com a normal/reflexo pintados no HQ (cena A) e pelo teste
  `tests/unit/MeshTests.cpp` (marcado `may_fail` até a decisão). Corrigir é trocar a ordem do
  `PushQuad`/triângulos nessas primitivas; muda a imagem do Render 3D (e a sombra, que usa
  `glCullFace(GL_FRONT)`) em todo patch que as usa, por isso não foi feito sem o OK do dono.
  **Corrigido:** Sphere, Cylinder (parede e tampas; Cone junto), TorusKnot, Helix, Pyramid (faces e
  base, e a normal das faces, que tinha y e z errados) e também `MeshOps::TubeAlong` (Curve, Curve
  Ops, L-System), achado pelo mesmo teste, que agora cobre 21 formas sem `may_fail`. Suíte completa:
  71/72, e o único caso que caiu (`R3DFXTEST`, AO no knot) mudou porque o knot visto de fora tem menos
  vincos que o interior que se via antes; o teste passou a usar raio 1,0. BEFORE recapturado.
- **Sombra do Render 3D sumindo embaixo dos objetos (verificação do dono, 2026-10-23, corrigido).**
  Duas causas somadas, diagnosticadas numa cena com esfera encostada, esfera a 20 cm e cubo a 2 cm
  do chão:
  1. O passe de sombra usava `glCullFace(GL_FRONT)` e gravava o lado *de trás* de cada objeto.
     Antes da correção do enrolamento isso gravava, sem querer, o lado virado para a luz; depois dela
     passou a gravar a face de baixo, a centímetros do chão. O bias do Render 3D é em profundidade
     normalizada e escala com a cena inteira, chão incluído (0,0025 × 5 × raio ≈ 25 cm com um chão
     de 30 m, até ~70 cm com luz rasante), então o chão logo abaixo ficava iluminado: meia-lua clara
     na cena A, sombra inteira sumida na cena de diagnóstico. Agora o passe não descarta faces e
     grava o lado mais próximo da luz, qualquer que seja o enrolamento (modelos importados com faces
     invertidas também).
  2. Os limites a que o mapa é ajustado só existem depois do upload, que acontece no passe principal,
     *depois* do passe de sombra. O primeiro desenho de uma cena saía sem sombra e, com a cena parada,
     o cache nunca redesenhava (só aparecia quando algo mudava). Agora, se a sombra ficou esperando
     os limites, o próximo quadro redesenha.
  O HQ não tinha nenhum dos dois problemas (ajuste só aos objetos que projetam sombra, bias do
  Filament em texels, limites antes do mapa). `SHADOWTEST`, `3DTEST`, `RENDER3DCACHESWEEPTEST`, `HQTEST`,
  `CULLTEST`, `TAATEST` e `R3DFXTEST` ok. Muda a imagem de todo patch com sombra no Render 3D (de
  propósito): o compat do gate vai acusar.

## 5. Cenas de referência e linha de base (S0)

Cenas em `examples/hq/raster/` (Render 3D) e `examples/hq/filament/` (Render 3D HQ), um renderizador por patch, nós em colunas sem sobreposição (pedido do dono, 2026-10-23; antes os dois renderizadores dividiam o patch) (geradas por `scripts/make_hq_scenes.py`; HDRI procedural de estúdio
`examples/hq/hq_media/studio.hdr` por `scripts/make_hq_studio_hdr.py`, sem licença de terceiros):
A estúdio de materiais (6 esferas PBR + chão), B estúdio de partículas (90k pontos, luz lateral,
câmera em órbita), C nuvem generativa (partículas na GPU), D partículas emissivas (bloom), E
geometria + partículas, F 1M de pontos (benchmark).

Capturas: `capture.ps1` (skill run-turbo-tests) toca cada cena até o quadro 150 e salva cada nó do
tipo pedido. **BEFORE atual** (Render 3D com o enrolamento corrigido, 2026-10-23 03:30), em
`build/hq/before/`: A 1,0 ms · B 1,2 · C 1,5 · D 1,6 · E 1,4 · F 14,8 (quadro 17,4). O executável da
0.70.0 (`build/perf-ref`) mede os mesmos 14,7 ms na F nessa hora: a diferença para a tabela abaixo é
do estado da GPU do notebook (clock/temperatura), não de código. Compare sempre BEFORE e AFTER da
mesma sessão. A primeira linha de base (S0, com o enrolamento invertido) ficou em
`build/hq/before_s0/`:

| Cena | Render 3D (ms) | Quadro (ms) |
|---|---|---|
| A material studio | 0.9 | 2.3 |
| B particle studio | 1.0 | 2.4 |
| C generative cloud | 2.3 | 3.9 |
| D emissive particles | 2.3 | 5.6 |
| E geometry + particles | 1.7 | 3.0 |
| F 1M points | 8.6 | 15.8 |

(Uma medida por cena, com o FRAMEPROF; para comparar desempenho use `perf.ps1 -PatchFiles`, que
repete e intercala.)

## 6. Arquitetura do HQ (S1)

**Nó.** `RenderHqNode` (`src/nodes/render_hq/RenderHqNode.*`) deriva de `Render3DNode`: herda pinos
(geometria A-D, câmera, 3 luzes, HDRI), cabos, salvar, órbita do preview e os ~20 `dynamic_cast`. Tem
`VisitParams` próprio (só o que o HQ lê; parâmetros novos sempre no fim) e `CookIfNeeded` próprio, que
monta um `HqFrame` (lista de draw já achatada e cozida, câmera, luzes, HDRI, ajustes) e chama o
renderizador. Registro: `PinsAndTheme.inl` (`REGISTER_NODE`), `NodeFactory.cpp` (3DScene), corpo em
`SceneParams.inl` (`DrawRenderHqParams`), despacho em `Nodes.inl` **dentro** do ramo do Render 3D
(mais um `else if` estoura o limite de aninhamento do MSVC, C1061), ajuda em `Help.inl`.
O que ainda é do Render 3D por herança e não vale para o HQ: `DepthTexture()`, `LastCamera()` e
`VelocityTexture()` não são virtuais, então Render 3D FX e Volume ligados a um HQ não veem profundidade
nem câmera (passam a imagem adiante). Entra quando o HQ tiver esses alvos (S5/S6).

**Módulo.** `HqRenderer` (interface, `HqRenderer.h`) com `CreateHqRenderer()`; `HqRenderer.cpp` só entra
com `INFINITE_ENABLE_RENDER_HQ=ON`; com OFF entra `HqStub.cpp`, o nó desenha como Render 3D e o painel
diz por quê (testado: o stub linka e o `HQTEST` passa nos dois modos).

**Passes (S1).** Alvos próprios: cor RGBA16F, normal de view + rugosidade RGBA16F (para GTAO/contact
shadows), profundidade 24 bits. 1) pré-passe de profundidade + normal (`hq_surface.vert` +
`hq_prepass.frag`, `invariant gl_Position`); 2) HDRI ao fundo onde a profundidade ficou 1
(`hq_background.*`, `textureLod` 0: sem a costura do achado 4); 3) sombreamento com `LEQUAL` sem escrita
de profundidade, um fragmento por pixel (`hq_surface.frag`: Lambert + IBL split-sum provisório, a S2
troca pelo BRDF do Filament); 4) `Render3DPost` (bloom, tonemap, sRGB) com instância própria, mapas de
IBL próprios. Malhas com VAO/VBO próprios por slot, reenviados só quando `MeshRevision()` muda. Pontos e
instâncias PointZ ainda não desenham (contados em `HqSkipped()`, aviso no painel): S3/S7. Locais de
uniform lidos uma vez no link. Estado GL salvo uma vez por cozimento e restaurado (FBO, viewport,
depth/cull/blend/scissor); os `glGet` disso saem com o cache de estado do F4.

**Medidas (S1, 1920×1080, RTX 4060 Laptop, captura única).** Cozimento do HQ no FRAMEPROF
(`renderhq(incl. its sources)`, CPU; as fontes já foram cozidas pelo Render 3D no mesmo patch):
A 0,3 ms · B 0,3 · C 0,2 · D 0,2 · E 0,2 · F 0,2. Capturas em `build/hq/after/`.
## 7. Materiais PBR (S2)

Portado de Filament @ 13be3649 (`docs/filament-notes.md` §2), em `shaders/hq_brdf.glsl` (termos),
`shaders/hq_surface.frag.glsl` (montagem) e `shaders/hq_ibl.frag.glsl` (mapas):

- **Luz direta**: `D_GGX`, `V_SmithGGXCorrelated` (qualidade HIGH), `F_Schlick` com f90 do f0
  (`saturate(dot(f0, 50*0.33))`), difuso Lambert, compensação de energia multiespalhamento
  `1 + f0 (1/dfg.y - 1)`; clearcoat (D GGX, V Kelemen, F com IOR 1,5, base atenuada por `1 - Fcc`;
  com normal map usa a normal geométrica) e sheen (D Charlie, V Neubelt, base escalada por
  `1 - max3(sheenColor) dfg.z`).
- **Parâmetros por pixel** na ordem do Filament: specular AA geométrico (Tokuyoshi-Kaplanyan,
  variância 0,15 e limiar 0,2, os padrões do MaterialBuilder, ligado sempre), clearcoat engrossa a
  base, clamp em 0,045, quadrado.
- **IBL**: DFG multiespalhamento 128×128 (x = NoV, y = rugosidade perceptual, 1024 amostras; canal z =
  albedo direcional do sheen, 4096 amostras uniformes), feita uma vez; radiância prefiltrada 512×256
  com 6 níveis, nível i em `pr = 1 - sqrt(1 - i/5)` e leitura em `lod = 5 pr (2 - pr)` (o mapeamento
  do cmgen), amostragem por importância GGX filtrada (256 amostras); irradiância 64×32 com cosseno;
  direção dominante `mix(r, n, a²)`; `E = mix(dfg.x, dfg.y, f0)`, difuso `(1 - E)`; AO do mapa do
  Material com multi-bounce (`gtaoMultiBounce`), AO especular de Lagarde e micro-sombra na luz direta.
  Refeito só quando o HDRI muda. Sem HDRI, o gradiente de três cores do Render 3D faz o papel.
- **Adaptador do Material**: cor, metálico, rugosidade, emissão, IOR + `specular` (f0 dielétrico
  `((ior-1)/(ior+1))² · 2 · specular`, o do Render 3D; 1,5 e 0,5 dão os 0,04 do Filament), clearcoat
  (+ rugosidade), sheen (+ cor e rugosidade), os 8 mapas (`MaterialMap`), o Mapping (UV, gerado,
  objeto, triplanar), bump/normal map e o modo de shading (normais, UV, flat). Ainda não:
  transmissão, subsurface, iridescência, anisotropia, opacidade (o HQ é opaco até a S8).
- **Diferenças do Filament** (registradas nos cabeçalhos): mapas equirretangulares em vez de cubemaps;
  irradiância em mapa em vez de SH L2; sem `f0ClearCoatToSurface` (não rastreado nas notas); luzes com
  os tipos e a queda do Render 3D (sol = direcional, sem o "wrap"); exposição é o multiplicador do
  Render 3D, não EV100.
- **Diferenças do Render 3D que aparecem nas capturas**: o Render 3D multiplica a irradiância por
  `ambient × 3` (azulado por padrão), o HQ usa a irradiância do HDRI como ela é, então chão e
  dielétricos saem neutros; o Render 3D soma um termo de "rim" e o HQ não.

Check: `HQTEST` ganhou "metal + clear coat + sheen" (a imagem muda e continua finita). Cena A
AFTER: ouro e aço refletem o céu em cima e o chão do HDRI embaixo, clearcoat nítido na esfera azul,
cobre do knot da cena E com a cor certa. Cozimento do HQ (CPU) 0,2-0,4 ms; os mapas de IBL custam uma
vez por HDRI.
## 8. Partículas: impostores esféricos e GPU Points (S3, S7)

**S3 (feita).** Uma PointZ só de pontos é desenhada como esferas (`shaders/hq_sphere.*`):
- **Sem cópia nem kernel por quadro**: o VAO do slot aponta direto para os buffers da PointZ na GPU
  (`P`, `PScale`, `Color`, `Active` como atributos instanciados, divisor 1; atributo ausente = buffer
  constante do valor padrão, compartilhado) e é re-apontado só quando a `revision` da PointZ muda.
  Um `glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n)` por fonte, nos dois passes.
- **Quad no plano tangente**: perpendicular ao raio olho-centro, no ponto da esfera mais próximo do
  olho, com o tamanho exato do cone da silhueta ali, `(d - r) tan(asin(r/d))` (ortográfica: `r`). A
  esfera inteira fica atrás desse plano, então a profundidade de qualquer acerto é maior que a do
  quad: `layout(depth_greater) gl_FragDepth`, e o teste de profundidade antecipado continua
  descartando impostores escondidos antes do sombreamento. (A primeira versão punha o quad no centro
  com `depth_less`, que não deixa rejeitar cedo com `LEQUAL`: a cena F custava 26,6 ms com os dois
  renderizadores; com o plano tangente, 22,4.)
- **Raio-esfera no fragmento**: silhueta e profundidade exatas, normal da esfera, sombreada pela mesma
  `hqShade()` dos triângulos (`hq_lighting.glsl`, PBR da S2 inteiro, com specular AA: pixels fora da
  esfera recebem a normal da silhueta e só são descartados no fim, para as derivadas). Um programa
  só para pré-passe e sombreamento: a profundidade é calculada pelo mesmo código nos dois.
- **Tamanho e cor como no Render 3D**: raio = `PScale × PointBaseSize` (0,02 sem `PScale`) × escala
  do modelo; `Color` multiplica a cor do Material; `Active < 0,5` esconde o ponto.
- **PointZ com triângulos** agora também desenha no HQ, direto dos buffers da GPU (`P`, `N`, `Tex` e o
  `TriIbo()` da topologia), com os limites medidos por `BoundsAsync` (sem esperar a GPU). Linhas e
  Instance PointZ continuam contados como não desenhados (instâncias: S7).

**Check**: `HQTEST` ganhou três casos: Grid PointZ 100×100 em pontos (10.000 esferas), a mesma como
polígonos (19.602 triângulos vindos da GPU) e 30 quadros com a câmera em órbita e um objeto se
movendo (a imagem acompanha, finita, sem erro GL; 0,14 ms/quadro CPU+GPU a 640×360 com 10k esferas +
knot). As cenas B, C e E (câmera em órbita) e F (pontos animados) são o teste de movimento em 1080p.

**Medidas (1920×1080, RTX 4060 Laptop, capturas da mesma sessão)**:

| Cena | Render 3D só (BEFORE) | HQ só | Os dois no patch |
|---|---|---|---|
| B partículas (órbita) | 2,5 | 1,6 | 3,2 |
| C nuvem (órbita) | 2,9 | 2,4 | 2,9 |
| F 1M de pontos animados | 17,4 | **14,4** | 22,4 |

(quadro inteiro, ms. "HQ só" = o Render 3D da cena reduzido a 16×16.) O HQ com esferas PBR de
verdade sai mais barato que os sprites chapados do Render 3D na cena de 1M.

### S7: esferas diferidas, Instance PointZ, benchmarks

**Perfil primeiro** (`INFINITE_GPUPROF`, cena F, 1M de esferas, 1080p, TAA + AO): do "Render 3D HQ"
de 12,2 ms, 5,1 ms eram do **Noise PointZ** (o nó de ruído sobre 1M de pontos, cozido dentro do HQ:
item para a P1, fora do renderizador); o HQ em si: sombreamento 3,5 + pré-passe 2,8 + TAA 0,3 +
GTAO 0,15. A 960×540 o sombreamento caiu para 2,0 e o pré-passe para 1,7: ~1,3-1,5 ms fixos de
vértice/instância por passe (1M de quads) mais o custo por pixel. LOD por tamanho projetado não
ajuda aqui (as esferas da F têm ~6 px); o desperdício era rasterizar 1M de quads de novo só para
sombrear pixels que o pré-passe já resolveu.

- **Sombreamento diferido das esferas**: o pré-passe das esferas grava também a cor do ponto e o
  slot (attachment 3, RGBA8: cor 0..1, slot/255); o sombreamento vira um passe de tela cheia por
  slot de esferas (`hq_sphere_deferred.frag`: posição da profundidade, normal do alvo do pré-passe,
  material do slot, o mesmo `hqShade()`). Cada pixel é sombreado uma vez. F: sombreamento
  3,5 → **0,21 ms**; o pré-passe subiu 2,8 → 3,3 ms (um alvo a mais); o renderizador 7,1 → **~4,2 ms**
  (−40%). Imagem equivalente (capturas B, C, E de `build/hq/s6` × `build/hq/s7`).
- **Instance PointZ** (antes ignorada pelo HQ): a forma (malha da PointZ ou da CPU) desenhada uma
  vez por ponto com `glDrawElementsInstanced`, transformações de `pop::InstanceMatrices` na GPU
  (atributos 3-6, o modelo da fonte já nelas), `Color` dos pontos (7) multiplicando a base como no
  Render 3D, matriz de normal pelo cofator (certa com escala não uniforme). Entra no pré-passe, no
  sombreamento, na sombra (ajuste pelo box assíncrono dos pontos + meia diagonal da forma) e no TAA
  (movimento de câmera; instâncias não têm transformação do quadro anterior). `HQTEST`: 10k caixas,
  120k triângulos, nada pulado. Cena nova `I_instances_100k` (100k cubos de metal sobre ruído).
- **Instâncias diferidas**: cópias de Instance PointZ cujo material não tem mapas (e é "lit") também
  vão para o passe de tela cheia (o pré-passe grava a cor da instância e o slot): 1M de cubos é
  desenhado uma vez, não duas (cubos 1M animados 20,5 → 13,4 ms). Com mapas, sombreamento forward.
- **Cena parada**: o renderizador monta uma assinatura (câmera, luzes, HDRI, parâmetros e, por fonte,
  revisões da malha/PointZ e das instâncias, matriz de modelo, material; fonte com textura nunca é
  "parada") e, enquanto ela não muda, deixa o TAA refinar 32 quadros (0,88^32: ~2% do primeiro) e
  depois devolve a última imagem sem desenhar e sem avançar a revisão (os nós seguintes também
  reaproveitam), como o cache do Render 3D. Sem TAA, já no quadro seguinte. `HQTEST`: reaproveitada
  depois de assentar, mesma imagem, redesenha quando um objeto anda.

**Benchmarks** (`perf.ps1`, 3 rodadas intercaladas, quadro inteiro em ms, 1080p, sem sombras, HDRI,
câmera parada; "animada" = Noise PointZ com velocidade 0,3; variantes geradas no scratchpad a partir
do `make_hq_scenes.py`):

| Cena | Raster (Render 3D) | HQ |
|---|---|---|
| esferas 100k, animadas | 2,1 | **2,0** |
| esferas 1M, animadas | 15,8 | **9,6** |
| esferas 2M, animadas | 30,9 | **18,0** |
| cubos (Instance PointZ) 100k, animados | 2,6 | **2,6** |
| cubos 1M, animados | 15,6 | **13,4** |
| esferas 100k / 1M / 2M, paradas | 0,9 / 0,9 / 0,8 | 1,0 / 0,9 / 0,9 |
| cubos 100k / 1M, parados | 1,0 / 1,0 | 2,0 / 1,0 |

Nas cenas animadas de 1M, ~5 ms são do próprio Noise PointZ (igual nos dois): a P1 olha isso.
Depois da P1 (cache de gradientes no curl 4D): Noise 4,88 → 1,31 ms na F, e a F do HQ inteira
9,65 → 5,68 ms por quadro (`perf.ps1 -RefExe` contra a S7).


`shaders/hq_shadows.glsl` (amostragem), `hq_shadow.frag`, `hq_sphere_shadow.*` e
`HqRendererGl::UpdateShadows()`:
- **Uma luz projeta**: a primeira direcional ou sol (ou a luz embutida), como no Render 3D. Pontual e
  spot não têm sombra (precisariam de cubemap).
- **Mapa estável**: ortográfico, ajustado aos limites de quem projeta sombra no mundo, nunca à câmera
  (mover a câmera não mexe no mapa); o tamanho sobe em degraus de 10% e a origem do mundo fica presa a
  um texel (objetos andando não fazem a sombra tremer). Peças planas e largas (chão, parede: menor
  dimensão < 1% da maior) recebem mas não alargam o ajuste, senão um chão de 30 m deixaria as
  partículas com texels de 3 cm. Profundidade `DEPTH_COMPONENT32F`, 1024/2048/4096 (`shadowQuality`,
  2048 por padrão). O mapa só é redesenhado quando muda o que ele mostra (assinatura: tamanho, matriz
  da luz, e por fonte tipo, revisão, quantidade, raio e matriz de modelo): cena parada = custo zero.
- **Bias do Filament** (`ShadowOptions`): polygon offset (inclinação 2,0; constante 0,5), normal bias
  de 1 texel (crescendo quando a luz rasante) e meio texel de bias constante em profundidade.
- **Filtro**: o PCF "LOW" do Filament (PCF de hardware + Gaussiana 3×3 em 4 amostras, Castaño 2013)
  por padrão; com `softness > 0`, PCSS-lite (busca de bloqueadores de 16 amostras no mesmo mapa sem
  comparação, penumbra pela distância bloqueador-receptor, PCF Poisson de 16 amostras girado por
  ruído). O mapa é lido por dois *sampler objects* (com e sem comparação) na mesma textura.
- **Partículas no mapa**: um *point sprite* por partícula (`GL_POINTS`, sem instanciação), de tamanho
  fixo em texels (luz ortográfica), recortado em círculo, na profundidade do centro: a metade da esfera
  virada para a luz fica na frente do disco (iluminada) e a de trás atrás (sombreada), sem
  `gl_FragDepth`, então a rejeição antecipada continua. Lê os mesmos buffers da PointZ (divisores
  trocados para 0 só nesse draw).
- **Contact shadows** (opção, **desligadas por padrão**, como `screenSpaceContactShadows` no
  Filament): 8 passos no buffer de profundidade rumo à luz, até 0,3 m, ruído entrelaçado, some na
  borda da tela; leem uma cópia da profundidade do pré-passe (sem laço de realimentação).
- **Achado de desempenho nas esferas** (vale para S3): numa nuvem densa, o quad do vizinho passa no
  teste antecipado (fica na frente da própria esfera) e rodava o sombreamento inteiro antes de perder
  no teste final. O shader da esfera agora compara a própria profundidade com a cópia do pré-passe e
  sai antes de sombrear; o specular AA (derivadas) é calculado antes de qualquer descarte.

Medidas (cena F, só HQ, 1080p, mesma sessão): sem sombras 14,9 → 13,6 ms (com a saída antecipada);
mapa 2048 +0,4 ms (era +4 com quads instanciados e `gl_FragDepth`); contact shadows +2 ms (por isso
desligadas por padrão). Cena B só HQ (câmera em órbita, sombras): 1,8 ms. `HQTEST`: sombras ligadas x
desligadas mudam a imagem; cozinhar de novo a cena parada dá a mesma imagem (mapa reaproveitado);
órbita + objeto em movimento continua ok (0,28 ms/quadro a 640×360).
## 10. GTAO (S5)

Três passes de compute logo depois do pré-passe (que já deixa a cópia de profundidade e a normal de
view), em `HqRendererGl::RunAo()`:
1. **Cadeia de profundidade** (`hq_ao_depth.comp`): nível 0 em **meia resolução** (a `resolution 0.5`
   do Filament) com o z de view de um pixel por bloco 2×2, e até 5 níveis seguintes do mesmo jeito
   (R32F). Diferença do Filament: sempre o mesmo canto do bloco, não a grade rotacionada. Com a grade
   rotacionada, o kernel reconstruía a posição a partir do centro do texel, e um chão visto em ângulo
   rasante saía em listras horizontais (linhas alternadas, achado na cena A). Agora `viewPos()`
   reconstrói a posição exata do pixel de onde veio cada amostra.
2. **Kernel** (`hq_gtao.comp`, porte do `gtaoImpl.fs`): 4 fatias × 3 passos, ruído 4×4 da Activision,
   atualização de horizonte com falloff `dist²/r² × 2` e `thicknessHeuristic` 0,004, integral do arco
   ponderada pelo cosseno, `power` 1, `intensity`. Normal lida do alvo do pré-passe (não reconstruída).
   Sem bent normals nem bitmasks (desligados no Filament também). Saída RG16F (visibilidade, z de view).
3. **Blur bilateral separável** (`hq_ao_blur.comp`): 11 amostras, Gaussiana sigma 4 (o MEDIUM do
   Filament), peso `max(0, 1 − (dz/0,05 m)²)`, pixels de fundo ignorados.

**Aplicação** (`hq_lighting.glsl`, `ssaoVisibility()`): upsample bilateral de 4 amostras com a
profundidade guardada no `.g` (o caminho HIGH do Filament; sem ele a borda de baixo das esferas
pegava o AO do chão atrás delas e saía pontilhada). Como no Filament, entra só na luz indireta:
`indirectAO = min(AO do Material, SSAO)` alimenta o IBL difuso (com multi-bounce), o AO especular
de Lagarde, sheen, clear coat e a luz "ambiente" do Render 3D. A luz direta continua só com a
micro-sombra do mapa de AO do Material.

**Parâmetros** (no fim do `VisitParams`): `ao` (ligado por padrão no HQ), `aoRadius`, `aoIntensity`,
`aoShow` (depuração: só a visibilidade, ainda com tonemap). **Raio padrão 1,0, não os 0,3 m do
Filament**: com o falloff do Filament só contam amostras até ~0,4 r, e nas cenas do Infinite (objetos
de 1-2 unidades, câmera padrão a 3) 0,3 virava um fio no contato. Medido num vinco de 90° (cubo no
chão, tonemap desligado): visibilidade ~0,7 no vinco depois do blur (teórico 0,5 antes).

**Custo** (cena F, 1080p, só o HQ, mediana de 5 rodadas intercaladas do `perf.ps1`): 12,66 → 13,2 ms
(+0,5 ms). `HQTEST`: AO ligado × desligado muda a imagem, "só AO" tem pixels ocluídos e abertos,
0,17 × 0,13 ms a 500×500.
## 11. TAA (S6)

Motivo (teste do dono, 2026-10-23): o HQ não tinha antialias nenhum (sem MSAA, sem TAA): bordas
serrilhadas, imagem "de resolução baixa" no output, e o GTAO de meia resolução piscando em objetos
pequenos. O TAA é o antialias do HQ (ligado por padrão, parâmetro `taa`).

- **Jitter e vetores de movimento** do kit compartilhado (`Render3DTemporal`: Halton(2,3) ×16,
  `JitteredProjection`, `MotionVectors`). O pré-passe grava a **velocidade** (attachment 2, RGBA16F,
  pixels sem jitter, B = 1): malhas pela matriz de modelo e câmera do quadro anterior; esferas pelo
  centro, com `P − PartVel·dt` (partículas da GPU) e a matriz anterior. O Filament só reprojeta a
  câmera; aqui objetos e partículas em movimento seguem o próprio movimento (menos ghosting).
- **Resolve** (`hq_taa.comp.glsl`, porte do `taa.mat` com os padrões do Filament): histórico
  Catmull-Rom de 5 amostras, reconstrução Lanczos-2 do centro do pixel a partir do 3×3 com jitter
  (de-ringing), caixa AABB metade cruz de 5 / metade 3×3, clipping preciso (raio contra a caixa) no
  espaço `c/(1+max(c))`, feedback 0,12. Movimento lido no pixel mais à frente do 3×3 (silhuetas
  andam com o objeto). A cobertura da geometria acompanha o histórico para o fundo liso
  (`Render3DPost::FinishTaa`); com HDRI de fundo, `Finish` normal.
- **GTAO + TAA**: a direção das fatias e o passo inicial giram a cada quadro (razão áurea sobre o
  padrão 4×4), então o histórico integra muito mais direções; a cadeia de profundidade e o AO usam a
  projeção com jitter (a mesma da profundidade).
- **Achado no caminho**: sem TAA, o fundo liso saía com alpha 0 (o alvo era limpo com alpha 0 e o
  acabamento copia o alpha do fundo). Agora limpa com alpha 1 (fundo opaco, como o Render 3D).

**Medidas** (`HQTEST`): cena parada converge (dois quadros seguidos diferem 0,00012); TAA × sem TAA
0,00112 (só bordas); depois de um objeto andar 0,08/quadro por 10 quadros e parar, 0,00108 (igual às
bordas: sem rastro). **Cintilação de objetos pequenos** (10k esferas, AO ligado, órbita lenta, média
de |f(t+1) − 2f(t) + f(t−1)|): 0,00745 sem TAA → 0,00281 com (2,7× menos). **Custo** na F (1080p,
só HQ, `perf.ps1` 5 rodadas): 11,68 → 12,42 ms (+0,74 ms). Visual: bordas de esferas e do nó suaves,
moiré das partículas da cena B bem menor (capturas `build/hq/s6`).

Feito na S8: viés de mip −1 nas texturas com TAA e nitidez pós-TAA (RCAS), ver §12. Fica para
depois: `depthDisocclusion` do Filament só se aparecer ghosting em desoclusão.
## 12. Pós, presets e parâmetros (S8)

O HQ deixou de usar o `Render3DPost` do Render 3D (que fica como estava): o acabamento é o
`HqPost` (`src/nodes/render_hq/HqPost.*`), com o bloom e o tonemap do Filament.

- **Bloom do Filament** (`hq_bloom_down.frag`, `hq_bloom_up.frag`, notas §6): um passe 2× com o
  threshold (subtrativo, `max(0, c − threshold)`, e o corte de destaques `1/(1 + max·1/1000)` do
  `highlight` 1000) e, sem TAA, a média de Karis contra vagalumes (com TAA desligada, como no
  Filament); passes 2× até a altura ficar a até 2× de 384 linhas (1080p: 540); a cadeia de
  `levels` níveis com o filtro de 9 amostras (nível par) ou o de 13 (Jimenez, ímpar); de baixo para
  cima, cada nível soma o de baixo pela tenda 3×3. Composição em HDR linear **antes do tonemap**:
  `cena + bloom · força / níveis` (modo ADD). Diferença do Filament: o upsample lê o nível de destino
  e grava a soma numa textura própria, em vez de `blend ONE, ONE` no próprio nível (mesma soma, sem
  mexer no estado de blend que os nós 2D compartilham).
- **Parâmetros do bloom** (as chaves do Render 3D, outro significado): `bloom` = força do Filament
  (0 desliga); `bloomThreshold` = o que é subtraído (1,0 = o do Filament; 0 = sem threshold);
  `bloomRadius` = número de níveis × 10 (0,6 = os 6 do Filament; até 11).
- **Tonemaps**: os cinco do Render 3D (mesmo código, mesma ordem) e mais dois do Filament: 5
  "Filament ACES (legacy)" (`ACESLegacyToneMapper`, o padrão do Filament: ACES RRT+ODT com o reforço
  de brilho 1/0,6) e 6 "Filament ACES" (`ACESToneMapper`, sem o reforço). Portados do
  `ToneMapper.cpp` com as matrizes do `ColorSpaceUtils.h` (lidos no S8: as notas não tinham as
  matrizes); a cena é Rec.709 linear, então passa para Rec.2020 e volta em volta da curva, como faz
  o color grading do Filament. O padrão do nó continua o ACES do Render 3D (1): trocar o padrão muda o
  visual de todo patch novo, decisão do dono. Saída sRGB com gama 2,2 e dither, como o Render 3D (o
  Filament usa a OETF sRGB exata).
- **Exposição**: o multiplicador do Render 3D, aplicado no sombreamento (antes do bloom, como a
  pré-exposição do Filament); não é EV100.
- **Render scale** (`upscale`, chave do Render 3D: nativo, 1,5×, 1,7×, 2×, 3×): o quadro inteiro
  (pré-passe, sombra, GTAO, TAA, bloom, tonemap) é feito no tamanho de saída ÷ razão e o FSR 1 do kit
  (`Render3DTemporal::Upscale`, EASU + RCAS) leva ao tamanho de saída, como no Render 3D.
- **Nitidez** (`sharpness`, 0-1): RCAS do FSR 1 depois do TAA, também em tamanho nativo
  (`Render3DTemporal::Sharpen`, função nova do kit; o Render 3D não a chama). 0 = sem RCAS nativo (a
  `sharpness` 0 do TAA do Filament, padrão do HQ); com upscale, `(1 − sharpness) × 2` stops, a
  conta do Render 3D.
- **Viés de mip** das texturas de material: −1 com TAA (o `lodBias` do Filament) e −log2(razão) com
  render scale (`hq_surface.frag`, `uLodBias`).
- **GTAO em resolução cheia** (`aoFullRes`, a `resolution 1.0` do Filament): a cadeia de
  profundidade e o kernel recebem o deslocamento do nível 0 (`uLevel0Shift`).
- **Presets** (no fim do corpo do nó; não são um parâmetro salvo: cada um grava os parâmetros de
  verdade, que continuam editáveis, e o menu mostra o preset que eles batem, ou "Custom"):

  | Preset | Mapa de sombra | GTAO | TAA | Render scale |
  |---|---|---|---|---|
  | Performance | 1024 | desligado | sim | FSR 1,5× |
  | Balanced (= padrão do HQ) | 2048 | meia resolução | sim | nativo |
  | High | 4096 | resolução cheia | sim | nativo |

Parâmetros novos no fim do `VisitParams`: `upscale`, `sharpness`, `aoFullRes` (patches antigos abrem
iguais: sem a chave, nativo, sem RCAS, GTAO em meia resolução).

**Check** (`HQTEST`): bloom liga × desliga (a imagem ganha luz, finita), os 7 tonemaps finitos e os
dois do Filament diferentes entre si e do ACES do Render 3D (o legacy mais claro), RCAS e GTAO cheio
mudam a imagem, e cada preset grava os seus parâmetros, mantém o tamanho de saída (o Performance
desenha abaixo dele) e tem o custo medido. Capturas (`build/hq/s8_before` × `build/hq/s8`): G e H
saem **idênticas bit a bit** (o acabamento novo reproduz o antigo sem bloom); A, B, C, E, F e I só
diferem pela animação (órbita, ruído no tempo); a D (partículas emissivas, bloom 0,8) troca o halo
largo que tingia o quadro inteiro por um brilho mais concentrado, e o HQ da D caiu de 1,8 para
1,2 ms.

**Custo** (`perf.ps1`, 3 rodadas intercaladas, quadro inteiro em ms, 1080p, RTX 4060 Laptop):

| Cena | Performance | Balanced | High |
|---|---|---|---|
| A materiais (sombras) | 1,84 | 2,02 | 2,11 |
| B partículas (sombras, órbita) | 1,57 | 1,62 | 2,25 |
| F 1M de esferas (sem sombras; ~5 ms são do Noise PointZ) | 7,98 | 9,60 | 10,02 |
| I 100k cubos de metal (sombras) | 2,73 | 3,47 | 4,12 |

Bloom antigo (`Render3DPost`) × novo, cena D, executável da S7 × S8 (`-RefExe`): **8,45 → 7,49 ms**
(−11%); A (sem bloom) 1,64 → 1,52 (ruído). Em 500×500 (`HQTEST`, CPU + GPU): Performance 0,17,
Balanced 0,22, High 0,27 ms por quadro.
## 13. Benchmarks medidos e capturas BEFORE/AFTER (S9)

**Capturas** (`capture.ps1`, quadro 150, 1920×1080): BEFORE = `examples/hq/raster` com o Render 3D
(`build/hq/s9_before`), AFTER = `examples/hq/filament` com o HQ no preset Balanced, o padrão
(`build/hq/s9_after`). Olhadas lado a lado, cena por cena:

| Cena | O que muda do Render 3D para o HQ |
|---|---|
| A materiais | metais e dielétricos do Filament (o ouro e o cromo refletem o estúdio, o vermelho e o azul saturam sem escurecer), sombras de contato suaves, o chão neutro (o IBL do HDRI no lugar do céu azulado) |
| B partículas | as esferas têm volume e oclusão entre si (GTAO), as faixas de pontos ganham relevo; no Render 3D viram um plano chapado |
| C nuvem generativa | a mesma silhueta; esferas sombreadas, bordas sem serrilhado (TAA) |
| D partículas emissivas | o bloom do Filament: o brilho fica junto da forma, sem tingir o quadro inteiro |
| E geometria + partículas | o nó de cobre igual; as partículas com sombreamento e sombra |
| F 1M de pontos | o Render 3D mostra um recorte claro e chapado; o HQ mostra cada dobra do ruído (esferas, GTAO, TAA) |
| G sombra de contato | a sombra encosta nos objetos; o vermelho e o azul mais saturados (o Render 3D os escurece) |
| H AO de contato | o GTAO escurece os cantos entre a parede, o chão e os objetos |
| I 100k cubos | instâncias de metal com sombra e AO (ver o defeito corrigido abaixo) |

**Defeito achado nas capturas e corrigido na S9**: na cena I, o chão aparecia como uma lâmina cor de
cobre entre as instâncias (existia desde a S7). No pré-passe, só as malhas diferidas (instâncias sem
mapas) gravavam o id do slot no attachment 3; o chão, uma malha forward desenhada depois, passava
na frente das instâncias sem regravar o id, e o passe diferido sombreava esses pixels de novo, com o
material das instâncias e a normal do chão. Agora toda malha forward grava o id 0 ("nenhum"). Check
novo no `HQTEST`: as instâncias vermelhas dentro de uma caixa desenhada depois delas devem dar a
mesma imagem que a caixa sozinha (diferença máxima 0,0000; sem a correção, 0,651).

**Benchmarks** (`perf.ps1 -Runs 3`, as 18 cenas intercaladas na mesma sessão, mediana do quadro
inteiro em ms, 1920×1080, RTX 4060 Laptop, VSync e limitador desligados; executável da S9):

| Cena | Render 3D | Render 3D HQ | HQ × raster |
|---|---|---|---|
| A materiais (sombras) | 3,31 | 2,90 | −12% |
| B partículas (sombras, órbita) | 2,60 | 2,74 | +5% (+0,14 ms) |
| C nuvem generativa | 5,16 | 2,55 | −50% |
| D partículas emissivas (bloom) | 8,63 | 7,57 | −12% |
| E geometria + partículas | 2,34 | 2,30 | −2% |
| F 1M de esferas | 11,86 | 5,61 | −53% |
| G sombra de contato | 1,06 | 1,04 | −2% |
| H AO de contato | 1,16 | 1,18 | +2% (ruído) |
| I 100k cubos de metal | 2,99 | 2,99 | igual |

O HQ desenha mais (GTAO, TAA, sombra filtrada, esferas de verdade) e custa o mesmo ou menos: o
pré-passe sombreia cada pixel uma vez, as esferas e as instâncias usam o passe diferido (S7) e a
cena parada é reaproveitada depois de o TAA assentar. Na B, o HQ fica 0,14 ms acima numa cena leve
em que ele roda GTAO e TAA e o Render 3D não. Na F, o Render 3D sozinho custa 10,7 ms (FRAMEPROF `render3d`). Os números das sessões
anteriores (S3-S8, P1) estão no §8, §10-§12 e na §6 da estratégia.

### S9b: retorno do teste do dono (`buves_textMotion.inf`)

O patch dele: chão de 289×273 com relevo de uma textura, 1024 cubos instanciados com um Material de
opacidade 0,364, alpha cutoff 0,843, relevo forte de uma textura que desliza (`transform` animado), sombra
4096 suave (2,8) com contact shadows, GTAO cheio, **TAA desligado**, tonemap Filament ACES legacy. O que
ele viu e o que era:

| Retorno | Causa | Correção |
|---|---|---|
| sombras "riscadas", pouco suaves | a busca de bloqueadores do PCSS girava por pixel: na borda da penumbra alguns pixels não achavam bloqueador e saíam 100% iluminados (pontos claros em diagonal, o padrão do ruído); sem TAA nada alisava o ruído do filtro | busca com o disco fixo (vizinhos concordam); o termo suave e o de contato calculados uma vez por pixel numa **máscara em tela** (`hq_shadow_mask.comp`: profundidade + normal do pré-passe) e alisados pelo blur bilateral do GTAO (para na borda de profundidade); com TAA, o ruído também gira a cada quadro |
| arestas dos cubos sem anti-aliasing, imagem "dura" | sem TAA o HQ não tinha anti-aliasing nenhum (não há MSAA) | **FXAA** (`hq_fxaa.frag`, o algoritmo FXAA 3.11 de Lottes, a opção do Filament ao lado do TAA) na imagem final sempre que o quadro não tem TAA; parâmetro `fxaa` (ligado) |
| cores piscando | o relevo de altura media a inclinação sempre a 1 texel do nível 0: de longe, a textura minimizada virava normais aleatórias de pixel para pixel (e de quadro para quadro com a textura deslizando) | a inclinação é medida no nível de mip que o pixel cobre (1 texel daquele nível, escalada de volta): de perto igual, de longe filtrada |
| (achado) os cubos opacos no HQ, quase invisíveis no Render 3D | o HQ ignorava `opacity`, `alphaCutoff` e `textureAlpha` | as regras do Render 3D (alpha = opacidade × alpha do mapa de albedo; abaixo do cutoff o fragmento cai): slot com opacidade abaixo do cutoff some (e não faz sombra, como as máscaras do Filament); opacidade < 1 desenha depois de tudo, do mais longe para o mais perto, com o blending "transparent" do Filament (difuso multiplicado pelo alpha, especular inteiro, como vidro) |
| sem as opções equirect, cubemap e dome | o HQ não tinha o 360 do Render 3D | **360** com as mesmas chaves (`panorama`, `domeAngle`, `domeTilt`): seis faces de 90° pelo HQ e o mesmo passe de reprojeção do Render 3D; as faces sem TAA (não têm histórico em comum), com FXAA |
| "a imagem está dura, pouco realista" | quase tudo acima (serrilhado, pontilhado, cintilação); além disso, no patch: a única luz de preenchimento é um HDRI noturno a 0,18 (sombras quase pretas), relevo com força 1,8-2,4, raio do GTAO 2,5 (escurece largo) e o tonemap legacy (mais contraste) | as correções acima; os ajustes de cena ficam com o dono |

O TAA continua o melhor anti-aliasing do HQ para cena sem textura deslizando: o histórico dele não
acompanha um UV animado (a textura borra), e é por isso que o FXAA existe. Checks novos no `HQTEST`: FXAA
reduz a energia de borda sem TAA; sombra suave + contato sem TAA sem mais detalhe fino que o PCF duro;
o cutoff tira o mesh (diferença 0 contra a cena sem ele) e a opacidade 0,5 deixa ver o que está dentro;
360 equirect 2:1 e dome no tamanho pedido. Exemplos A-I iguais a antes (só a animação muda).
## 14. Licenças

| Arquivo | Portado de (Filament @ 13be3649, Apache-2.0, © Google LLC) |
|---|---|
| `shaders/hq_brdf.glsl` | `shaders/src/surface_brdf.fs`, `surface_material.fs`, `surface_shading_lit.fs`, `surface_shading_model_standard.fs`, `surface_ambient_occlusion.fs`, `surface_light_indirect.fs` |
| `shaders/hq_ibl.frag.glsl` | `libs/ibl/src/CubemapIBL.cpp` (DFV_Multiscatter, amostragem GGX), `tools/cmgen/src/cmgen.cpp` (nível → rugosidade) |
| `shaders/hq_surface.frag.glsl`, `shaders/hq_lighting.glsl` | montagem de `surface_shading_model_standard.fs` e `surface_light_indirect.fs` (os termos vêm do `hq_brdf.glsl`) |
| `shaders/hq_shadows.glsl` | técnicas de `shaders/src/surface_shadowing.fs` como as notas descrevem (PCF LOW de Castaño, contact shadows); valores de `LightManager.h` `ShadowOptions` |
| `shaders/hq_ao_depth.comp.glsl` | `filament/src/materials/ssao/mipmapDepth.mat` (sem a grade rotacionada, ver §10) |
| `shaders/hq_gtao.comp.glsl` | `filament/src/materials/ssao/gtaoImpl.fs`, `gtao.mat`; valores de `Options.h` `AmbientOcclusionOptions` / `Gtao` |
| `shaders/hq_sphere_deferred.frag.glsl` | montagem de `surface_shading_model_standard.fs` (via `hqShade`, como as esferas da S3) |
| `shaders/hq_taa.comp.glsl` | `filament/src/materials/antiAliasing/taa/taa.mat`; valores de `Options.h` `TemporalAntiAliasingOptions`; jitter de `PostProcessManager.cpp` (via o kit da 0.70) |
| `shaders/hq_ao_blur.comp.glsl`, `ssaoVisibility()` em `hq_lighting.glsl` | `filament/src/materials/ssao/bilateralBlur.mat`; upsample de `shaders/src/surface_ambient_occlusion.fs` |
| `shaders/hq_bloom_down.frag.glsl` | `filament/src/materials/bloom/bloomDownsample2x.mat`, `bloomDownsample9.mat`, `bloomDownsample.mat`; valores de `Options.h` `BloomOptions`; cadeia de `PostProcessManager.cpp` `bloom()` |
| `shaders/hq_bloom_up.frag.glsl` | `filament/src/materials/bloom/bloomUpsample.mat` (caminho HIGH, tenda 3×3) |
| `shaders/hq_finish.frag.glsl` (tonemaps 5 e 6, composição do bloom) | `filament/src/ToneMapper.cpp` (`ACESLegacyToneMapper`, `ACESToneMapper`, namespace `aces`), matrizes de `filament/src/details/ColorSpaceUtils.h`, ordem de `materials/colorGrading/colorGrading.mat` |
| `shaders/hq_surface.frag.glsl` (S9b: alpha) | o blending "transparent" e o modo "masked" do Filament (`docs/Materials.md`, `surface_shading_lit.fs`: difuso pré-multiplicado, especular não) |

Sem código portado (S9b): `shaders/hq_fxaa.frag.glsl` é escrito a partir do algoritmo FXAA 3.11 de
Timothy Lottes (NVIDIA), a opção sem histórico que o Filament oferece; `shaders/hq_shadow_mask.comp.glsl`
é do Infinite-Turbo (reaproveita as funções de `hq_shadows.glsl` e o blur do GTAO).

Também listado em `THIRD_PARTY_NOTICES.md`. Nenhum código do Filament é linkado.
## 15. Problemas conhecidos e o que falta

Estado ao fim da S9 (2026-10-23). Nada abaixo bloqueia a 0.71; os itens marcados "dono" esperam uma
decisão dele.

**Decisões do dono**
- **Tonemap padrão do HQ**: continua o ACES do Render 3D (1), para um patch novo do HQ sair com o mesmo
  "look" do Render 3D. O padrão do Filament é a opção 5 (Filament ACES legacy), mais clara. Trocar o
  padrão muda todo patch novo; os salvos guardam o tonemap e não mudam.
- **Noise PointZ curl 4D (P1)**: as posições diferem do kernel anterior em até ~2e-5 (tolerância
  documentada, 1e-4 no `POPTEST`); bit a bit não dá, porque o driver arredonda as duas formas do código
  de um jeito diferente.
- **Preview 3D**: ~2 ms de GPU no patch de referência (já atualiza um quadro sim, outro não); gastar
  menos muda a UI.
- **Costura do HDRI no fundo do Render 3D** (achado nas capturas da S9, existe desde antes da 0.71):
  uma linha vertical tracejada clara no fundo das cenas B, C, E, F e I do `examples/hq/raster`. O fundo
  lê o equirretangular com `texture()` (`Geometry3DNodes.cpp`, `dirToEquirect`), e na volta do
  `atan` (u de 1 para 0) a derivada salta e o mip vai para o menor. A correção é uma linha (`textureLod`
  ou `textureGrad`), mas o Render 3D está congelado e ela muda os 3 patches do compat com HDRI. O HQ não
  tem a costura.

**Limitações do HQ (0.71)**
- Linhas de PointZ (line strips) não são desenhadas; o painel as conta como "skipped".
- Só a primeira luz direcional (ou o sol embutido) projeta sombra; um mapa só, sem cascatas.
- Render 3D FX depois do HQ deixa a imagem passar: o FX lê a profundidade e a velocidade do pipeline
  do Render 3D, que o HQ não preenche. O HQ já tem o próprio GTAO; reflexos em tela, profundidade de
  campo e motion blur para o HQ ficam para depois.
- Sem MSAA: o anti-aliasing do HQ é o TAA, ou o FXAA quando o TAA está desligado (S9b). O
  `depthDisocclusion` do TAA do Filament não foi portado (não apareceu ghosting nas cenas de referência).
- Transparência (S9b): ordenada por objeto, não por triângulo nem por instância (cópias de um Instance
  PointZ translúcido se cruzam na ordem em que foram desenhadas); objetos translúcidos fazem sombra
  inteira (como no Render 3D) e não entram no GTAO, na velocidade do TAA nem na máscara de sombra.
  `transmission`, iridescência, anisotropia, subsurface e dispersão do Material ainda não são lidos.
  Esferas (pontos de PointZ) continuam opacas.
- 360 (S9b): seis quadros completos por quadro (o custo vezes seis, sem reaproveitar cena parada); o
  GTAO e as sombras de contato veem uma face por vez e podem marcar a emenda entre faces.
- Sem RTX: o modo RTX continua só no Render 3D (decisão do dono: RTX fica para a 0.72).
- Saída sRGB com gama 2,2 e dither, como o Render 3D; o Filament usa a OETF sRGB exata.
- Particle PointZ (motor CPU, P1) abaixo de ~10 fps: a cópia por linhas cobre ~0,2 s de nascimentos;
  se a cópia pronta for mais velha que isso, aquele quadro lê as linhas direto da GPU (mesmo resultado,
  com uma espera). Com o app em tempo real isso não acontece.

**O que falta (fora da 0.71)**
- F4 (cache de estado GL): `glGetUniformLocation` por quadro no `Render3DTemporal::Upscale` / `Sharpen`
  e `glGet*` dos helpers de GPU (regra "zero glGet por quadro").
- Sombras em cascata (CSM) e sombra de mais de uma luz no HQ; reflexos em tela no HQ.
- Linhas de PointZ no HQ (tubos ou linhas com largura).
