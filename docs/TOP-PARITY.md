# Infinite-Turbo × TouchDesigner TOPs: comparação e plano

Levantamento de 2026-10-13. Fontes: documentação pública da Derivative (página "TOP",
"TOP Generator Common Page", "Transform TOP", "Crop TOP"), o catálogo do Infinite pelo MCP
(`describe`) e a leitura de `src/nodes/FilterNode.cpp`, `src/core/GLUtil.cpp` e
`src/core/FilterDefs.cpp`. Não houve engenharia reversa do TouchDesigner: só a documentação dele.

Etapa 1 = imagem 2D (TOPs) e Send/Receive. Etapa 2 = 3D com POPs, em outro documento.

---

## 1. Diagnóstico: por que o 2D parece frágil

Os sintomas que você citou (Crop que não reduz a resolução, Transform sem continuidade) têm
quatro causas na base do sistema de imagem, não nos nós em si:

| # | Causa | Onde | Efeito |
|---|---|---|---|
| D1 | Todo filtro renderiza **sempre no tamanho da entrada** | `FilterNode.cpp:175` (`EnsureFbo(mOut, mInput.Width(), mInput.Height())`) | Crop não consegue diminuir a imagem; nenhum filtro consegue mudar a resolução (só o Fit e o Layout, que são classes à parte) |
| D2 | Formato de pixel **fixo em RGBA 8 bits** | `GLUtil.h:25` (padrão `GL_RGBA8`) | Faixas (banding) em blur, gradiente e feedback longo; sem HDR (valores acima de 1 são cortados); bloom e exposição perdem informação |
| D3 | Borda **sempre "clamp"**, e os shaders zeram o que cai fora de 0..1 | `GLUtil.cpp:165-166` + shader do `transform` em `FilterDefs.cpp:274` | Transform e Displace não têm Repeat/Mirror/Hold: a área fora fica preta ou transparente |
| D4 | Filtragem **sempre linear, sem mipmap** | `GLUtil.cpp` (MIN/MAG = `GL_LINEAR`) | Imagem reduzida (scale < 1, Fit pequeno) serrilha e cintila |

No TouchDesigner, todo TOP tem uma página **Common** que resolve isso de uma vez para todos os
operadores. Esse é o item mais importante do plano: ele conserta Crop e Transform e melhora todos
os outros ~60 filtros do Infinite junto.

---

## 2. Fase T0: página "Common" para todo nó de imagem

Implementar uma vez no `FilterNode` (e nas classes de imagem à parte: Blend, Layer Stack,
Switcher, Feedback, Formula), com os **padrões reproduzindo o comportamento atual** para os
patches antigos abrirem iguais.

- [x] **T0.1 Output Resolution**: Use Input (padrão) / ½ / ¼ / ⅛ / 2× / 4× / Fit / Limit /
  Custom (W×H) e um menu de presets (720p, 1080p, 4K, quadrado…). Equivale a `outputresolution`
  no TD.
- [x] **T0.2 Pixel Format**: Use Input (padrão) / 8-bit / 16-bit float / 32-bit float, e talvez
  Mono 16f/32f para máscaras. `EnsureFbo` já aceita o formato; falta expor e propagar.
  O padrão de Feedback, Trails, Reaction Diffusion e Bloom pode passar a ser 16f.
- [x] **T0.3 Extend**: Hold / Zero / Repeat / Mirror. Vira uma função GLSL única
  (`sampleExt(uv)`) no preâmbulo comum dos filtros, mais o wrap de textura.
  O padrão de cada nó continua o de hoje.
- [x] **T0.4 Input Smoothness**: Nearest / Interpolate / Mipmap (com `glGenerateMipmap` só quando
  for pedido).
- [x] **T0.5 Passes**: repete o filtro N vezes realimentando a saída (blur mais forte, erosão,
  etc.). É barato com o `AcquireScratchFbo` que já existe.
- [x] **T0.6 Channel Mask**: aplicar o efeito só em R/G/B/A selecionados (o resto passa direto).
- [x] **T0.7 Metadados no cabo de imagem**: o `ImageCable` passa a carregar formato e aspecto
  além de W×H, para "Use Input" funcionar em cadeia.
- [x] **T0.8 Inspetor**: mostrar "1920×1080 · 16f" no rodapé de cada nó de imagem (o TD mostra
  isso no info do nó; ajuda muito a depurar).

Riscos: memória de GPU (32f custa 4× o 8-bit) e compatibilidade de patch. Mitigação: padrões
iguais aos atuais, e um teste que carrega os `.inf` de exemplo e compara as imagens antes e
depois.

---

## 3. Fase T1: consertar e completar os nós que já existem

### Crop
Hoje (`FilterDefs.cpp:676`): mantém o tamanho e pinta a área cortada (transparente, cor ou zoom).
- [x] Corte real: **a saída passa a ter o tamanho da região mantida** (é o comportamento do Crop
  TOP: "a parte cortada é descartada, reduzindo a resolução").
- [ ] Unidades por borda: fração (0..1), pixels, ou aspecto.
- [ ] Permitir valores **fora** de 0..1 (crop "para fora" aumenta a tela) e usar o Extend (T0.3)
  para decidir o que aparece.
- [x] Manter os modos antigos como opção "legacy" para os patches existentes.

### Transform
Hoje: translate/scale/rotate/flip; o que sai da área vira preto.
- [x] **Extend** (Hold/Zero/Repeat/Mirror): a "continuidade" que você pediu.
- [x] **Pivot** X/Y (rotação e escala em torno de um ponto; hoje é sempre o centro).
- [x] **Transform Order** (SRT, STR, RST…).
- [ ] **Unidades** de translate e pivot: fração, pixels, aspecto.
- [x] **Background Color + Comp Over**: cor de fundo e opção de compor por cima dela.
- [x] **Limit Tiles** (U/V): quantas repetições aparecem quando Extend = Repeat ou Mirror.
- [ ] Rotação em graus na interface (hoje o uniform é radiano).
- [ ] Remover os limites atuais de 0.1..4 na escala (permitir escala negativa = flip, e zoom maior).

### Blend → paridade com o Composite TOP
Hoje: 2 entradas, 32 modos, `mix`.
- [x] **Pre-Fit** da entrada B quando os tamanhos diferem: Fill / Fit / Fit H / Fit V / Native,
  com **Justify** (esquerda/centro/direita, cima/centro/baixo).
- [x] **Size from**: entrada A / entrada B / custom.
- [x] **Transform da entrada B** (translate/scale/rotate) dentro do próprio Blend, como o
  Composite TOP faz; isso evita um Transform só para posicionar.
- [x] Operações de matte que faltam: **Inside, Outside, Atop, Xor, Under** (conferir quais dos
  32 modos já cobrem).
- [ ] Versão com **N entradas** (o Layer Stack tem 4 fixas; o Composite do TD aceita qualquer número).

### Fit
- [x] Modo **Limit** (só reduz, nunca amplia).
- [x] Input Smoothness, já vindo do T0.4.

### Switcher → Switch TOP / Cross TOP
- [x] **Index fracionário** modulável: 1.5 = metade entre a entrada 1 e a 2 (o Switch TOP do TD
  faz o blend entre vizinhos). Hoje é por tempo ou `manualSlot` inteiro.
- [ ] Mais de 4 entradas.

### Feedback
- [x] Formato próprio (16f/32f), já vindo do T0.2.
- [x] Opção de **Reset** (botão e pino de trigger), como o Feedback TOP.

### Formula (GLSL)
Hoje não tem entradas de imagem.
- [x] **Entradas de imagem** (pelo menos 4: `uIn0`..`uIn3`), igual ao GLSL TOP. É o que permite
  escrever qualquer efeito novo sem recompilar o app.
- [ ] Mais que 4 knobs, ou knobs nomeados.
- [ ] (Depois) Compute shader / múltiplas saídas, como o GLSL Multi TOP.

### Displace
- [x] Extend (T0.3); escala separada de X e Y; ponto médio (offset 0.5); escolha de canais.

---

## 4. Fase T2: nós que faltam

Prioridade pensando em VJ/performance ao vivo e no que o Infinite já tem.

### Alta
- [ ] **Cache / Time Machine / Cache Select**: buffer de N frames na GPU. Faz delay de vídeo,
  *slit-scan* e eco (Time Machine = cada pixel lê um frame diferente do cache, conforme um mapa
  em tons de cinza). O Feedback com `delayFrames` cobre só uma parte.
- [ ] **Resolution**: só muda o tamanho (com filtro e mipmap). Fica trivial depois do T0.
- [ ] **Tile**: repete/espelha a imagem N×M com offset (hoje existe o `mirror tile`, só com espelho).
- [ ] **Corner Pin** como efeito na cadeia (o Projection tem, mas é nó de saída).
- [ ] **Remap TOP**: usa uma imagem como mapa de UV absoluto (o Displace só desloca).
- [ ] **Constant**: fonte de cor sólida (com resolução e alfa). Hoje dá para improvisar com
  Shape ou Ramp.
- [ ] **Reorder / Channel Mix**: trocar e mixar canais (R←A, mono→RGB, etc.). O
  `color adjustments` tem Channel Mixer, mas não reordena alfa nem pega canais de outra entrada.
- [ ] **Math TOP**: soma/mult/min/max entre duas imagens, mais *range remap* (from-range →
  to-range), sem passar por modo de blend.
- [ ] **Luma Blur**: blur variável comandado por uma máscara (foco seletivo).
- [ ] **Optical Flow**: vetores de movimento entre frames (alimenta Displace e Datamosh e gera CV
  de movimento).

### Média
- [ ] **Limit / Clamp**: limita valores (importante quando vier o float).
- [ ] **Tone Map**: HDR → SDR (necessário depois do T0.2).
- [ ] **Anti Alias**: FXAA/SMAA para Shapes, Text e Render 3D.
- [ ] **Slope**: derivada da imagem (relevo e direção), útil para displace e normal.
- [ ] **Matte**: escolhe entre A e B por uma máscara (o `set alpha` só troca o alfa).
- [ ] **RGB Key**: key por faixa RGB (hoje há chroma key e luma key).
- [ ] **Analyze (como imagem)**: média/mín/máx reduzidos a 1 pixel, para encadear na GPU (o
  Image Analyze gera CV, não imagem).
- [ ] **Screen Grab**: captura da tela/janela como fonte.
- [ ] **NDI In / Out** (Spout já existe; NDI funciona entre máquinas na rede).
- [ ] **SVG**: arquivo vetorial como fonte (com resolução livre).

### Baixa / depende do 3D (Etapa 2)
- [ ] Depth, SSAO, Render Pass, Render Select, Texture 3D, Point File In/Select, POP to TOP.
- [ ] Web Render, Photoshop In, Substance, OpenColorIO.
- [ ] Sensores (Kinect, RealSense, ZED, Leap, OAK…): fora de escopo até alguém pedir.

### Já cobertos no Infinite (sem ação)
Blur (gaussian/box), Bloom, Edge, Emboss, Level/HSV (`color adjustments`), Lookup, Threshold,
Chroma Key, Noise, Ramp, Text, Circle/Rectangle (Shapes), Lens Distort, Mirror (`symmetry`),
Normal Map, Convolve, Displace, Feedback, Layout, Fit, Over/Under/Add/Multiply/Difference
(`Blend`), Spout In/Out, Video In, Movie File In (`Video`), Projection (corner pin e warp).

---

## 5. Fase T3: Send / Receive sem fio (estilo Pure Data)

No Pd, `[s nome]` / `[r nome]` mandam e recebem sem cabo; vários *send* podem chegar a um
*receive* e um *send* pode chegar a vários *receives*. No TouchDesigner o equivalente é o
**Select TOP/CHOP** (referencia qualquer operador pelo caminho). Hoje o Infinite só tem isso por
OSC, que passa pela rede e só carrega números.

### Proposta
- [x] Dois nós, **Send** e **Receive**, com um campo **nome do canal** (autocompletar com os
  canais existentes).
- [x] Um par para **cada tipo de cabo**: imagem, CV (modulação), notas, áudio e geometria. O
  tipo é definido pelo que se liga no Send.
- [x] **Um para muitos**: qualquer número de Receives no mesmo nome.
- [x] **Muitos para um**: o Receive escolhe como juntar:
  - CV: soma / média / máximo / último que mudou
  - Notas: merge por ordem de tempo (a mesma lógica do Note Merge)
  - Áudio: soma (como `throw~`/`catch~` no Pd)
  - Imagem: o primeiro ativo / por índice / composição aditiva / Layer Stack
- [x] **Avaliação**: o Receive vira uma aresta "invisível" no grafo. Para imagem e CV basta o
  `Pull` buscar os Sends do canal; para áudio, a topologia do `AudioEngine::SetTopology` precisa
  incluir essas arestas. **Detectar ciclos** (Send → … → Receive do mesmo nome) e quebrar com um
  frame de atraso, como o Feedback faz.
- [~] **Interface**: feito o campo do canal com seletor de nomes já usados, o nome no título do
  nó e a contagem de parceiros ("2 image sends on \"img\""). Falta: linhas tracejadas até os
  parceiros ao passar o mouse e um painel "Canais".
- [~] **Patch / MCP**: o nome do canal é salvo no `.inf` e aparece no `describe`/`set_param`
  (chave `channel`). Falta: `get_graph`/`explain` listarem as ligações sem fio.
- [ ] (Opcional) **Send global de parâmetro**: qualquer slider pode ler um canal de CV pelo nome,
  sem nó Receive, como uma expressão `ch("nome")`.

---

## 6. Ordem sugerida

1. **T0.1–T0.4** (Resolution, Format, Extend, Smoothness): é a base de todo o resto.
2. **Crop e Transform** (T1): os sintomas que você percebeu primeiro.
3. **Send/Receive** (T3): independente do T0, pode correr em paralelo.
4. **Blend/Composite, Switcher, Formula com entradas** (T1).
5. **Cache/Time Machine, Resolution, Tile, Remap, Constant, Math, Channel Mix** (T2 alta).
6. O restante do T2.
7. **Etapa 2: 3D com POPs.**

## Pendências deste levantamento
- Só li em detalhe as páginas do TD de Transform, Crop e da Common Page. Antes de implementar
  cada nó do T2, ler a página dele na documentação para fechar a lista de parâmetros.
- Conferir quais dos 32 modos do Blend já equivalem a Inside/Outside/Atop.
- Medir o custo de GPU de 16f como padrão em feedback numa máquina modesta.

---

## Status em 2026-10-13 (0.52.0)

Feito: T0 inteiro no `FilterNode` (todos os 48 filtros), T1 (Crop, Transform, Blend, Fit, Switcher,
Feedback, Formula, Displace) e T3 (Send/Receive dos 5 tipos de cabo). Teste automático
`TOPPARITYTEST` com 20 verificações (pixels, tamanhos, formatos e a compilação de todos os 48 filtros), todas passando. Detalhes em CHANGELOG-TURBO.md.

Ficou de fora desta rodada (continua aberto acima):
- Common page fora do `FilterNode`: Layer Stack, Switcher, Formula e as fontes (Noise, Ramp,
  Shape...) ainda não têm resolução/formato próprios. Blend, Fit e Feedback ganharam só as opções
  que fazem sentido neles.
- Crop: unidades em pixels e cortar "para fora" (aumentar a tela). As faixas dos sliders não
  mudaram de propósito: a faixa de um slider é o que a modulação usa, e mudá-la alteraria patches
  antigos que modulam esses parâmetros.
- Transform: unidades em pixels, rotação em graus e faixa de escala maior, pelo mesmo motivo.
- Blend com N entradas; Switcher com mais de 4; Formula com mais knobs ou compute shader.
- Send/Receive: linhas tracejadas, painel de canais, ligações sem fio no `get_graph` do MCP, e o
  "send global de parâmetro".

Mudança visível em patches antigos: a rotação do Transform em imagens não quadradas deixou de
distorcer (antes girava no espaço UV e a imagem virava um paralelogramo).
