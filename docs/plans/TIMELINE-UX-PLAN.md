# Timeline: plano de UX (meta: edição no nível do Reaper)

2026-10-25. Continua `TIMELINE-ANALYSIS.md` (modelo/formato alinhados, gravação feita na
`feature/timeline-align`, commit 7954598c).

## 1. Princípios

1. **Primeiro igualar ao Naman, depois ir além.** Cada interação que ele já tem entra com o mesmo nome,
   a mesma tecla e o mesmo comportamento, para que os dois lados continuem conversando.
2. **Comportamento verbatim, visual nosso.** A UI dele roda sobre outro sistema visual e ImGui 1.92.
   Portamos a semântica (ferramentas, teclas, regras de drag), não os widgets.
3. **Núcleo sem UI.** Toda edição nova vira uma operação em `Arrange::Model` (testável sem janela).
   O painel só traduz mouse e teclado em operação. É assim que o Naman consegue levar de volta
   (`TIMELINE-ANALYSIS.md` 4b).
4. **Nada de "duro".** Toda zona clicável tem cursor próprio, destaque no hover e uma área de pegar
   generosa. Todo drag mostra prévia e guia de snap. Tudo desfaz com um Ctrl+Z.
5. **Cada fase numa branch, com teste.** Um self-test por comportamento (os do upstream quando
   existirem) e o gate completo antes do merge.

## 2. Onde estamos e onde está o Naman

| Interação | Nosso | Naman |
|---|---|---|
| Roda do mouse | vertical; Ctrl = zoom; Shift = horizontal | Ctrl/Cmd = zoom no mouse; pinça; roda horizontal = scroll |
| Teclas de zoom | botões − / + / FIT | `F` ajustar, `=` / `-` zoom |
| Ferramentas | Blade (B) | Select (A), Trim (T), Range (R), Blade (B), Zoom (Z), Hand (H), Pencil (P) |
| Seleção de pistas | uma pista "atual" | seleção de linhas com Shift/Cmd; Delete, Duplicate e Group agem na linha |
| Inspector | janela flutuante (duplo clique) | painel acoplado, com cartões |
| Loop | botão LOOP, Shift+arrastar na régua | + `Ctrl+L` liga/desliga, `I` / `O` marcam início e fim na agulha |
| Range em todas as pistas | não | arrastar na régua com a ferramenta Range |
| Markers e saltos | `M`, Alt+setas | igual |
| Speed / reverse por clip (UI) | dado já salvo, sem UI | inspector |
| Stems | não | Render Track / Render Group |
| Fades em clip de vídeo | não | sim |
| Session view | dados no patch, sem UI | painel completo |
| Gravar áudio numa pista | **sim (nosso)** | não |

## 3. Fases

### U0: alívio imediato ("menos duro"), 1 a 2 sessões

**Roda e zoom.** Mapa de roda configurável, com o padrão novo:

| Gesto | Ação |
|---|---|
| roda | zoom horizontal em torno do mouse (o que você pediu) |
| Shift+roda | scroll horizontal |
| Ctrl+roda | scroll vertical |
| Alt+roda | altura das pistas |
| roda horizontal / touchpad | scroll horizontal |

- Uma opção em Settings volta ao mapa antigo ou ao do Naman (Ctrl = zoom).
- O zoom fica suave (interpolado em ~80 ms) e sem salto quando muda de escala.

**Teclas do Naman:** `F`, `=`, `-`, `Ctrl+L`, `I`, `O`. Mais **zoom na seleção** (`Z` com seleção)
e **zoom ao clip** com duplo clique na régua sobre ele.

**Áreas de pegar maiores:**
- borda de trim proporcional ao zoom (8 a 14 px, nunca mais de 1/4 do clip);
- alças de fade nos cantos de cima, como nos DAWs;
- a faixa do topo não rouba mais a borda;
- cursor certo em cada zona (trim ↔, fade ⌐, mover ✥, blade │).

**Retorno visual:**
- clip sob o mouse destacado;
- **linha-guia de snap** quando encaixa (grade, borda de clip, marker, agulha);
- tooltip com posição e duração durante o drag;
- prévia "fantasma" ao mover entre pistas.

**Drag sem tranco:**
- limiar de 3 px antes de começar;
- auto-scroll quando arrasta perto da borda;
- Alt solta o snap (já existe), Shift trava o eixo.

**Teste:** um `ARRANGEUXTEST` que injeta mouse (como o `DRAGTEST`) para roda, zoom em torno do mouse,
hit zones e snap guide.

### U1: igualar ao Naman, 3 a 5 sessões

1. **Ferramentas** com o mesmo comportamento e as mesmas teclas: Select, Trim, Range, Blade, Zoom,
   Hand, Pencil, mais o seletor no toolbar. Range na régua seleciona em todas as pistas.
2. **Seleção de pistas e grupos:** Shift (faixa), Ctrl (alterna). Delete, Duplicate, Group e Ungroup
   agem sobre as pistas selecionadas.
3. **Inspector acoplado:** o painel da direita substitui a janela flutuante. Duplo clique abre e
   fecha. Uma seção por tipo (clip, pista, grupo, multi-seleção), com speed/reverse e o resto dos
   campos dele.
4. **Engine que falta:**
   - reverse no player de arquivo (o `ProcessClipBlock` dele);
   - stretch por transientes;
   - **Render Track / Render Group (stems)**;
   - fades de vídeo;
   - aviso de conflito de fonte entre pistas;
   - pausa e restart quantizados no compasso.
5. **Testes do upstream** entram com cada parte: `ARRANGEEDITTEST`, `KEYSTEST`, `MARKERTEST`,
   `WAVETEST`, `VIDEOTEST`, `RENDERTEST`, `SAMPLETEST` e `SAMPLEEXPORTTEST`.
6. **Session view:** decisão sua. Se sim, é a última peça da U1, porque usa a mesma seleção, o mesmo
   inspector e o mesmo menu de clip.

### U2: edição nível Reaper (além do Naman), 4 a 6 sessões

- **Slip:** Alt+arrastar dentro do clip desliza o áudio sob a caixa (`sourceOffsetSeconds`).
- **Ganho do clip na própria caixa:** arrastar a linha de volume. Os fades ganham **curvas**
  (linear, log, exp, S), escolhidas com clique direito na alça.
- **Split:**
  - `S` corta na agulha (ou no mouse, se a agulha estiver fora do clip);
  - com Range, corta nas duas bordas da seleção.
- **Edição por Range:** apagar, copiar, mover e duplicar um trecho de tempo em várias pistas de uma
  vez (o "razor" dos DAWs).
- **Ripple:** modos desligado / esta pista / todas. Apagar ou mover fecha o buraco.
- **Snap magnético:** encaixa em borda de clip, marker, agulha e loop, além da grade, com toggle
  rápido.
- **Copiar arrastando:** Ctrl+arrastar copia. Arrastar entre pistas checa o tipo.
- **Altura das pistas:** arrastar e Alt+roda. Minimizar e maximizar a pista com duplo clique no
  cabeçalho.
- **Crossfade automático ao sobrepor clips na mesma pista.** Hoje o modelo proíbe sobreposição
  (invariante do Naman). Duas saídas:
  - (a) crossfade só entre clips colados, sem mudar o invariante;
  - (b) permitir sobreposição.

  A (b) muda o modelo e o formato que o Naman lê, então é decisão sua (e conversa com ele).

### U3: features novas (nossas), depois da U2

- **Loop do conteúdo do clip:** esticar repete o áudio. Campo `loopContent` no clip, tag nova
  aditiva.
- **Takes em loop e comping:** gravar com loop ligado cria uma lane de takes por passada; você
  escolhe trechos de cada uma.
- **Punch in/out**, count-in, metrônomo na gravação e compensação da latência de entrada do Audio In.
- **Automação com keyframes** (inspirada no TiXL, `TIMELINE-ANALYSIS.md` seção 5):
  - lane de automação por parâmetro, com curvas e interpolação constant/linear/smooth/bezier;
  - gravação de movimento de knob;
  - Alt+clique para criar key.
- **Mapa de tempo** (mudança de BPM no meio), por último.

## 4. Ordem sugerida e entregas

| Ordem | Branch | Entrega que você testa |
|---|---|---|
| 1 | `feature/timeline-ux-u0` | roda dá zoom, bordas fáceis de pegar, guia de snap, teclas F = - I O |
| 2 | `feature/timeline-u1-tools` | ferramentas, seleção de pistas, inspector acoplado |
| 3 | `feature/timeline-u1-engine` | stems, reverse, transient stretch, testes do upstream |
| 4 | `feature/timeline-u2-edit` | slip, curvas de fade, ganho na caixa, split S, range edit, ripple |
| 5 | `feature/timeline-u3-*` | loop do clip, takes em loop, automação |

Cada branch: operações no modelo com teste, UI fina, gate completo, nota em
`TIMELINE-ANALYSIS.md` do que o Naman pode levar.

## 5. Decisões do dono (2026-10-25)

1. **Roda igual ao Naman:**
   - Ctrl+roda dá zoom em torno do mouse (e a pinça do touchpad, que no Windows chega como Ctrl+roda);
   - a roda horizontal rola para o lado;
   - a roda rola vertical, e Shift+roda rola para o lado (já era assim).

   A linha "roda dá zoom" da U0 sai.
2. **Clips como no Naman:** sem sobreposição (o invariante dele). O "crossfade" fica entre clips
   colados, que já não levam declick na junta.
3. **Session view: portar** (é o lançador de clips em grade estilo Ableton: cenas em linhas, pistas em
   colunas, cada célula toca um clip em loop quantizado). Entra no fim da U1.
4. **O "R" armado não vai para o patch.**

## 6. Progresso

- `feature/timeline-ux` (saída de `feature/timeline-align`):
  - Ícones vetoriais do Naman (`src/core/TablerIcons.h`, verbatim).
  - Toolbar refeito sobre os tokens (`tok::`) e o tema, com componentes `ArrUi` (`IconTile`, `Chip`,
    `GroupBegin/End`, `Next`, `Gap`) em grupos:
    - transporte (início, play, rec);
    - leitura de posição (clique troca compassos/tempo);
    - as 7 ferramentas;
    - snap (ímã + grade), loop e follow;
    - pistas (+Audio, +Video, Group);
    - zoom (−, +, Fit);
    - à direita: Timeline audio, monitor, Render.
  - Ferramentas com as teclas dele:
    - Select (A);
    - Trim (T): a borda mais perto, de qualquer ponto do clip;
    - Range (R): trecho em pistas, ou em todas pela régua; Delete apaga, Esc solta;
    - Blade (B);
    - Zoom (Z): clique, Alt+clique, arrastar;
    - Hand (H);
    - Pencil (P): arrastar numa pista vazia desenha um clip e abre a escolha da fonte.
  - Teclas `F`, `=`, `-`, `Ctrl+L`, `I`, `O`.
  - Borda de trim de 4 a 12 px conforme o tamanho na tela, e 4 px para fora.
  - Um cursor por zona e por ferramenta.
  - Linha-guia de snap tracejada durante o drag.
- `feature/timeline-u1` (saída de `feature/timeline-ux`):
  - **Seleção de pistas** (`gArrangeRowSel`, como a do Naman):
    - clique seleciona, Shift estende, Ctrl alterna, Esc limpa;
    - clicar num clip limpa a seleção de pistas, e clicar numa pista limpa a de clips;
    - Delete, Shift/Ctrl+D e Ctrl+G agem nas pistas quando há pistas selecionadas, nos clips quando
      não há; uma Range selecionada tem prioridade sobre as duas.
  - **Cabeçalhos das pistas** sobre os componentes `ArrUi`:
    - nome e tipo em cima;
    - power, M, S e R como chips nas cores semânticas;
    - ganho e pan em campos rebaixados;
    - medidor na borda;
    - destaque de seleção e de hover.
  - **Inspector fixo** numa coluna à direita (botão de lápis no toolbar). Mostra:
    - o clip (os mesmos campos da janela antiga);
    - uma pista: nome, on/mute/solo, ganho, pan, gravação, cor, duplicar/excluir;
    - várias pistas: duplicar/agrupar/excluir;
    - vários clips: duplicar/excluir.

    A janela flutuante só aparece se o inspector estiver escondido.
- `feature/timeline-parity` (saída da `feature/timeline-u1`, mergeada na principal), **paridade com o
  Naman**:
  - **Reverse no player de arquivo** e **speed/reverse no inspector**: `ArrangeSetClipSpeed` e
    `ArrangeSetClipReverse` verbatim. O vídeo segue a velocidade com sinal.
  - **Stems:**
    - Render track, Render group e Render selected tracks, pelo menu da pista ou do grupo e pelo
      inspector;
    - o job leva `laneScope`, e as outras pistas, de áudio e de vídeo, ficam mudas durante o render;
    - o arquivo recebe o nome das pistas.
  - **Stretch por transientes** (v0.6 1.6, verbatim): `TransientDetect.h` e `ClipTimeStretch`.
  - **Grade de lançamento** (v0.6 2.38): `MusicTime::LaunchQuantize` e `Transport::RequestTogglePlay`,
    `RequestRewind` e `ResolveLaunch`. O Space e o play usam a grade, que se escolhe no toolbar.
  - **Aviso de conflito:** o inspector avisa quando um nó toca em várias pistas.
  - **Session view:**
    - motor do Naman em `src/arrange/ArrangeSession.inl`, verbatim exceto o modo de áudio
      (`gArrangeSessionMode`);
    - o áudio e o vídeo leem `ArrangePlaybackModel()`;
    - grade nossa em `src/arrange/ArrangeSessionUi.inl`, com o mesmo design system: pistas em
      colunas, cenas em linhas;
    - clique lança e para; duplo clique numa célula vazia escolhe a fonte;
    - o menu do clip tem loop e tamanho do loop, launch trigger/gate, quantize, "then" (follow action)
      e source/clear;
    - stop por pista, Stop all (`.`), Timeline | Session no toolbar, chip Session audio;
    - teste `INFINITE_ARRANGESESSIONTEST`.
  - **Fica para depois** (o Naman também tem):
    - na Session: arrastar entre células, mover cena e pista, Record to Timeline e Send Scene to
      Timeline, inspector de slot;
    - os testes do upstream que dependem da UI dele (`EDIT`, `KEYS`, `MARKER`, `WAVE`, `VIDEO`,
      `RENDER`, `SAMPLE`, `SAMPLEEXPORT`).
