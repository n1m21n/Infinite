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

Cenas em `examples/hq/` (geradas por `scripts/make_hq_scenes.py`; HDRI procedural de estúdio
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
## 9. Sombras e contact shadows (S4)

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
## 12. Pós, presets e parâmetros (S8)
## 13. Benchmarks medidos e capturas BEFORE/AFTER (S9)
## 14. Licenças

| Arquivo | Portado de (Filament @ 13be3649, Apache-2.0, © Google LLC) |
|---|---|
| `shaders/hq_brdf.glsl` | `shaders/src/surface_brdf.fs`, `surface_material.fs`, `surface_shading_lit.fs`, `surface_shading_model_standard.fs`, `surface_ambient_occlusion.fs`, `surface_light_indirect.fs` |
| `shaders/hq_ibl.frag.glsl` | `libs/ibl/src/CubemapIBL.cpp` (DFV_Multiscatter, amostragem GGX), `tools/cmgen/src/cmgen.cpp` (nível → rugosidade) |
| `shaders/hq_surface.frag.glsl`, `shaders/hq_lighting.glsl` | montagem de `surface_shading_model_standard.fs` e `surface_light_indirect.fs` (os termos vêm do `hq_brdf.glsl`) |
| `shaders/hq_shadows.glsl` | técnicas de `shaders/src/surface_shadowing.fs` como as notas descrevem (PCF LOW de Castaño, contact shadows); valores de `LightManager.h` `ShadowOptions` |
| `shaders/hq_ao_depth.comp.glsl` | `filament/src/materials/ssao/mipmapDepth.mat` (sem a grade rotacionada, ver §10) |
| `shaders/hq_gtao.comp.glsl` | `filament/src/materials/ssao/gtaoImpl.fs`, `gtao.mat`; valores de `Options.h` `AmbientOcclusionOptions` / `Gtao` |
| `shaders/hq_ao_blur.comp.glsl`, `ssaoVisibility()` em `hq_lighting.glsl` | `filament/src/materials/ssao/bilateralBlur.mat`; upsample de `shaders/src/surface_ambient_occlusion.fs` |

Também listado em `THIRD_PARTY_NOTICES.md`. Nenhum código do Filament é linkado.
## 15. Problemas conhecidos e o que falta
