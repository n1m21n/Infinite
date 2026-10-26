# Timeline (Arrangement): análise antes de mexer em código

2026-10-25. Nada de código foi alterado. Base: `turbo/windows-only` (e821cb17), `upstream/main`
(33de7576, 2026-10-26) e TiXL `main` (MIT).

## 1. Resumo

- A nossa timeline não funciona como as pistas de um DAW. Cada clip é um **portão** sobre um nó vivo
  do grafo: ele abre e fecha a saída do nó no tempo. Só os clips criados arrastando um arquivo
  (Audio File) são reposicionados pela agulha. Quase todo o comportamento que você estranhou vem
  daí, somado a três armadilhas de UX (seção 3).
- O port do Naman é de 2026-10-02 (0.43), com correções até a 0.49 (10-06). De lá para cá o upstream
  acrescentou: speed/reverse por clip, Render Track/Group (stems), lock de posição em todo clip de
  áudio, Session view (lançador de clips), stretch por transientes, as teclas de DAW e **11
  self-tests da timeline**. Nós temos zero.
- O que você quer, e nem o upstream tem: gravar áudio direto numa pista, loop do conteúdo do clip,
  automação com keyframes. O Naman adiou explicitamente "loop-in-clip" e "keyframes/automation"
  (`docs/plans/arrangement/overhaul-prompt.md:852` no upstream). Essas partes serão nossas, e o TiXL
  serve de referência para a automação.
- PointZ: já temos o desenho certo (Particle PointZ + nós que somam em `PartForce`) e cerca de 5 das
  18 forças do TiXL. Faltam as que interagem com malha, SDF, imagem e outras partículas
  (seção 6). É trabalho independente da timeline.

## 2. Como a timeline funciona hoje

Código: `src/arrange/ArrangeModel.{h,cpp}` (modelo, verbatim do upstream),
`src/arrange/ArrangeUi.inl` (4690 linhas, a nossa UI, motor e render), `src/audio/AudioEngine.cpp`
(`RunArrangeLookahead`, `MixArrangeTerminals`).

- **Modelo:** lanes (vídeo/áudio) com clips em ticks (960 por semínima), markers, loop, grupos.
  Cada clip aponta para um nó (`srcUid`).
- **Áudio:** só toca com **TIMELINE ON** (`gArrangeTimelineMode`). Nesse modo os Audio Out do canvas
  ficam mudos (menos os "live"). Para cada lane e cada nó-fonte, a engine pega o **buffer de saída do
  próprio nó** e aplica as janelas dos clips: ganho, fades, declick de 3 ms, pan, ganho e pan da lane
  (`AudioEngine.cpp:462`).
- **Posição:** só reposiciona a fonte quando `seekSource` é verdadeiro, e
  `seekSource = ArrangeClipIsAudioSample(c)`, ou seja, só clip criado por drop de arquivo
  (`ArrangeUi.inl:732`). Loopers, synths, samplers e Audio File ligados à mão seguem tocando de onde
  estão. O clip só decide se o som passa.
- **Render:** `RENDER...` gera MP4/MOV/WAV offline. Força TIMELINE ON, desliga o loop e roda o grafo
  bloco a bloco (`ArrangeUi.inl:4379-4552`). Exige a engine de áudio ligada. Não exporta stems.
- **Loop:** botão LOOP ou Shift+arrastar na régua. O Transport dá a volta no limite do bloco de áudio
  (`Transport.cpp:163`).
- **Trim:** arrastar a borda do clip. A zona é de 6 px para dentro (`kArrangeEdgePx`), e as alças de
  fade no topo (9 px) têm prioridade sobre ela.

## 3. Os quatro problemas que você relatou

Ainda não reproduzi nenhum. Dependo de saber como as pistas foram gravadas (perguntas na seção 8).
As causas estão em ordem de probabilidade.

### 3.1 "A timeline não tocou as 3 tracks"

**O que o dono fez (relato de 2026-10-25):**
1. Usou "Add to Timeline" nos três nós; os clips caíram na mesma pista.
2. Criou mais 2 pistas de áudio e arrastou um clip para cada uma.
3. Ligou TIMELINE ON, deu play e tocou os instrumentos; a forma de onda apareceu nos três clips.
4. Voltou ao início: só a pista 01 tocou, e o export saiu só com ela.

**Causa (pelo código):** a timeline **não grava**. A forma de onda que aparece é a "live waveform"
(upstream WP8, `ArrangeUi.inl:882` e `DrawArrangeClipWave`, `:1052`). Ela desenha o que *passou*
pelo portão do clip enquanto tocava, com um cache por clip que fica na tela depois. Parece uma
gravação, mas nenhuma amostra foi guardada. Depois do rewind, o clip abre o portão de novo e só se ouve
o que o nó produz sozinho:
- a pista 01 tem uma fonte que toca sozinha com o transport (sequenciador, looper, sample);
- as outras duas eram instrumentos tocados à mão, e sem você tocando ficam em silêncio.

O render (`ArrangeRenderBegin`) roda o mesmo grafo offline, então sai igual.

Isso põe **gravar numa pista (F3)** como a primeira coisa a fazer, e não a última. A forma de onda ao
vivo também precisa deixar claro que não é gravação (por exemplo, hachurada com o texto "live").

Os itens abaixo continuam valendo.

1. **"Add to Timeline" põe tudo na mesma pista, um depois do outro.** `ArrangeAddNodeToTimeline`
   (`ArrangeUi.inl:1494`) usa a *primeira* lane de áudio e o primeiro compasso livre a partir da
   agulha, com clip de **1 compasso**. Três nós adicionados assim viram compasso 1, 2 e 3 na mesma
   pista, em sequência e não em paralelo, cada um com só 1 compasso.
2. **TIMELINE OFF por padrão, e volta a OFF a cada load ou reset** (`ArrangeUi.inl:314, 342`). Soltar
   um arquivo na timeline não liga o modo. Com OFF, os Audio File criados pelo drop não estão ligados
   a nenhum Audio Out: silêncio. Já os nós ligados no canvas tocam todos juntos, ignorando os clips. O
   upstream tem a mesma armadilha (`gAudioMode` manual).
3. **A fonte não é reposicionada** (seção 2). Com Looper ou synth, o clip abre a saída, mas o loop
   toca na fase dele, e nada recomeça quando o clip entra. O upstream já mudou a regra: "nunca decidir
   comportamento por `sampleDropped`; todo clip de áudio trava a posição" (skill
   `timeline-arrangement-architecture` §2). Nós ficamos na regra antiga.
4. **Drop de arquivo liga o sync de tempo quando detecta BPM** (`ArrangeUi.inl:1950+`). Numa gravação
   livre, um BPM detectado errado estica o áudio e muda o tamanho do clip.
5. **A cadeia de efeitos some no modo timeline.** O clip lê a saída do nó-fonte, não o Audio Out.
   Looper → Reverb → Audio Out toca sem o reverb, e o Audio Out fica mudo.

### 3.2 "Não exportou o bounce"

- A palavra "bounce" não existe na nossa UI. O caminho é `RENDER...` → formato WAV → "Add to
  queue...". O upstream também tirou o bounce por clip.
- Não temos **Render Track / Render Group** (stems), que o upstream tem (`laneScope` em
  `src/app/panels/ArrangeRender.cpp`).
- Se a timeline não tocava ao vivo (3.1), o render sai com o mesmo conteúdo: em sequência, 1
  compasso cada, sem efeitos.
- O render de WAV nunca foi testado aqui. O upstream tem `INFINITE_ARRANGERENDERTEST` e
  `INFINITE_ARRANGESAMPLEEXPORTTEST`, nós não. Precisa reproduzir.

### 3.3 "Não consegui fazer um loop"

- O loop de região existe, mas só aparece no botão LOOP (que cria 4 compassos na agulha) ou com
  Shift+arrastar na régua. É difícil de descobrir.
- **Loop do conteúdo do clip não existe.** Esticar um clip além do arquivo dá silêncio: o drop força
  `af->loop = false` (`ArrangeUi.inl:1932`). Esticar um clip de Looper não repete nada, porque a fonte
  é livre. O upstream só tem loop por clip na Session view (`SessionSlot::loop/loopTicks`), não na
  timeline.

### 3.4 "Não consegui diminuir o tamanho da track depois de pronta"

- Se for o **comprimento do clip**: o trim existe, mas a zona de pegar é estreita (6 px), o cursor só
  muda em cima dela, e o topo do clip pega o fade. Também dá para mudar no campo Length do inspector
  (duplo clique no clip).
- Se for a **altura da pista**: tem mínimo (`kArrangeRowMinH`).
- Preciso que você confirme qual dos dois era.

## 4. O nosso port e o upstream do Naman

| | Nosso (0.43–0.49) | Upstream hoje |
|---|---|---|
| Modelo `ArrangeModel` | verbatim de 10-02 | + `speed`/`reverse` por clip, `SessionSlot`/`Scene`, `FindSlot`, `LaneOfClip` (+~150 linhas) |
| UI | `ArrangeUi.inl` (4690), ImGui 1.90.9 | `src/app/panels/Arrange*.cpp` (~12.800), design system próprio e ImGui 1.92.9 |
| Lock de posição | só clips de drop | todo clip de áudio (nó decide se aceita) |
| Stems | não | Render Track / Render Group |
| Session view | não (temos Clip Matrix e Scenes) | sim: lançador estilo Ableton, follow actions, Record to Timeline |
| Speed/reverse por clip | não | sim (v0.6 3.14) |
| Stretch por transientes | não | sim (v0.6 1.6) |
| Teclas de DAW | parcial | sim (v0.6 2.3) |
| Pausa/restart quantizados no compasso | não | sim (v0.6 2.38) |
| Fades em clip de vídeo | não | sim (43d7ed00) |
| Aviso de clip no master | não | sim (v0.6 1.13) |
| Conflito de fonte entre lanes (áudio/vídeo) | não | avisado no inspector |
| Self-tests | **0** | 11 (`ARRANGE`, `AUDIO`, `EDIT`, `KEYS`, `MARKER`, `RENDER`, `SAMPLE`, `SAMPLEEXPORT`, `VIDEO`, `WAVE`, `SESSIONVIEW`) |

**Como portar:**

- **Modelo, persistência e media import: verbatim** (regra R2). Isso mantém os patches compatíveis
  nos dois sentidos, inclusive patches do upstream com speed/reverse e session.
- **Semântica de motor: portar** (lock de posição geral, speed/reverse, stems, conflitos), adaptando
  ao nosso `AudioEngine`.
- **UI: não portar verbatim.** Ela depende do design system e do ImGui 1.92 deles. Os recursos entram
  na nossa `ArrangeUi.inl`, como já fizemos na 0.43. O `UpstreamCompat.inl` cobre só helpers de corpo
  de nó.
- **Testes primeiro:** adaptar os 10 `ARRANGE*` do upstream ao nosso harness. Eles viram o gate da
  timeline e a reprodução dos seus bugs.
- **Session view:** decisão sua (seção 8). É grande (~2.900 linhas) e se sobrepõe a Clip Matrix e
  Scenes.

## 4b. Alinhamento com o Naman (regra do dono, 2026-10-25)

**O upstream grava áudio na timeline?** Não. O modelo é o mesmo nosso (portão sobre nó vivo + forma de
onda ao vivo), então o seu caso da seção 3.1 dá o mesmo resultado lá. O "Record to Timeline" da Session
view (`src/app/panels/ArrangeSession.cpp:480-530` no upstream) grava os **lançamentos** de clips
como clips na timeline, não o áudio. Nem a timeline nem a Session view dele têm pista armada ou captura
de áudio.

**Estamos alinhados?**
- O modelo e o formato do patch da timeline vieram dele verbatim (0.43). Ele acrescentou campos e não
  mudou os que existiam.
- O motor e a UI divergiram: a nossa UI é própria, e a dele foi reescrita sobre o design system e o
  ImGui 1.92.
- Ele já trouxe ideias do Turbo para a versão dele, reimplementando do zero (a Session view diz "The
  idea comes from Ricardo Palmieri's Infinite-Turbo fork"; VMPC; crédito no ASIO). O caminho realista
  de volta é o mesmo: ele lê o nosso desenho e o código das partes sem UI e traz para o lado dele.

**Regras para tudo que fizermos na timeline:**
1. **Alinhar primeiro (F2 antes de recurso novo):** `ArrangeModel` e `Patch` iguais aos dele. Assim
   as nossas adições entram sobre a mesma base e o diff que ele lê é só o recurso.
2. **Núcleo portável e sem UI:** operações em `Arrange::Model`, captura e escrita de áudio na
   engine/nós (o upstream já tem `AudioFileWriter` portável), nada de API Windows no núcleo. A UI fica
   fina, em `ArrangeUi.inl`.
3. **Patch aditivo:** só tags novas ou campos no fim das linhas existentes (o leitor dele aceita
   campos a mais no fim; o comportamento com tag desconhecida será conferido antes de definir o
   formato). Patch nosso abre no dele sem perder nada que ele conheça, e patch dele abre no nosso.
4. **Mesmos nomes e lugares:** `Clip`/`Lane`/`SessionSlot`, ticks, `ArrangeEdit`, testes
   `INFINITE_ARRANGE*`. Cada recurso ganha um handoff curto em `docs/plans` com o desenho, os casos
   de teste e os pontos de integração, para ele reimplementar ou fazer cherry-pick.

## 4c. Progresso: alinhamento (branch `feature/timeline-align`, 2026-10-25)

Feito:
- `src/arrange/ArrangeModel.{h,cpp}` = `upstream/main` 33de7576, verbatim: `speed`/`reverse`,
  `SessionSlot`/`Scene`, `FindSlot`/`LaneOfClip`/`FindClipOrSlot`, clamp de ganho.
- `Patch.h/.cpp`: `ClipRecord::speed/reverse` e campos de slot, `SceneRecord`, `Data::scenes`,
  `sessionQuantize`. Lê e escreve `clipspeed`, `slotclip`, `sessionslot`, `scene`, `scenemeta` e
  `sessionmeta`, e lê o `clip` legado (segundos → ticks depois do `transport`), tudo como o upstream.
- `ArrangeUi.inl`: a conversão entre modelo e patch é a do upstream (`ArrangeClipToRecord`/
  `ArrangeClipFromRecord`). O clip legado é resolvido pelo remap do load. O undo compara speed,
  reverse, bypass, slots e cenas.
- Motor: **todo clip de áudio trava a posição** (regra do upstream). O `speed` vai para a janela do
  clip e para `SetClipSamplePosition`. A waveform estática respeita o speed. Entram
  `ArrangeAudioRebuildIfStale()` e `gAudioTopologyRebuildCount` (nomes do upstream).
- Testes (`src/app/selftest/ArrangeTests.inl`, no `driver.ps1`):
  - `ARRANGETEST` (do upstream): ops do modelo, fuzz de 2000 ops, round trip em ticks, legado, undo,
    New, entrada malformada, paridade em texto com speed/reverse/session, ids, mix.
  - `ARRANGEAUDIOTEST` (do upstream): clips colados, onset no sample, clip desligado, pause, seek,
    rebuild no meio do clip, mute/solo/pan/ganho, modo resetado no New/Open.
  - `ARRANGEMULTITRACKTEST` (nosso, o caso da seção 3.1): 3 pistas em paralelo audíveis ao vivo e
    no render WAV.

Bugs reais que os testes portados acharam no nosso port (corrigidos):
- **Áudio da timeline um bloco adiantado:** o mix lia `Beats()`, que já é o fim do bloco depois do
  `AdvanceAudioClock`. Agora lê `Transport::BlockStartBeats()`, como o upstream.
- **Undo reaproveitava id de clip:** o undo voltava o `nextId`. Agora ele só cresce.
- **`PushArrangeUndoSnapshot(gArrange)` gravava snapshot vazio** (o argumento é o próprio
  `gArrange`). Agora a função trata esse caso.

Diferenças que ficaram de propósito:
- O undo daqui recria o grafo, então o teste confere o nó pelo uid, não pelo ponteiro.
- O New começa com a timeline vazia; o upstream cria 4+4 pistas.
- O `PatchJson` (MCP) ainda não tem a timeline. O teste confere o formato de texto, que é o que o
  RPC usa.

Falta (F2):
- **Motor:** `reverse` no player de arquivo (o nosso `ProcessClipBlock` ainda não lê ao contrário;
  o dado já é salvo e carregado).
- **Testes que dependem de coisas que o upstream tem e nós não:**
  - `ARRANGESAMPLETEST` e `ARRANGESAMPLEEXPORTTEST` dependem do player e do import assíncrono do
    upstream (`gClipSampleDriftReseeks`, `ArrangeImportMediaFile`);
  - `ARRANGERENDERTEST` depende da fila de render do upstream;
  - `ARRANGEEDITTEST`, `KEYSTEST`, `MARKERTEST`, `WAVETEST` e `VIDEOTEST` dependem da API de seleção
    e drag do painel dele.

  Cada um entra junto com a parte correspondente.
- **UI da Session view:** decisão do dono. Os dados já passam pelo patch, então nada se perde.

## 4d. Gravação numa pista (feito, mesma branch)

**Uso:**
1. Clique no **R** da pista de áudio e escolha o nó que ela vai gravar (Audio In, synth, Mixer...).
2. Clique em **REC** no toolbar.
3. Toque. Parar ou pausar o transport encerra o take.
4. O take vira um clip de áudio na pista, no beat em que foi tocado, e substitui o que estava embaixo.

**Desenho** (pensado para o Naman poder levar: núcleo sem UI, nomes do upstream):
- **Motor (`AudioEngine`):**
  - `ArrangeRecordTap` é um anel SPSC (`AudioCaptureRing`) com `enabled`/`started`/`startBeat`.
  - `ArrangeRecorder` liga a pista ao buffer da fonte.
  - `RunArrangeRecorders` roda depois do mix das pistas: escreve o bloco no anel e marca o
    `BlockStartBeats()` do primeiro bloco. A monitoração é automática (enquanto grava ou com o
    transport parado), pelo ganho e pan da pista.
- **App (`ArrangeUi.inl`, bloco "record to timeline"):**
  - `ArrangeSetRecordArm`/`ArrangeClearRecordArm` (armar só vale na sessão, não vai para o patch).
  - `ArrangeRecordStart`/`Stop`/`Drain`/`Commit`.
  - Os taps vêm de um pool fixo que nunca é liberado, para não deixar ponteiro solto numa topologia
    antiga.
  - O commit cria um Audio File privado (volume 1, loop off) e um clip Sample (`sampleDropped`,
    sync off, `sampleBpm` = tempo da gravação) via `PlaceOverwrite`, num passo de undo.
- **Patch:** nenhuma linha nova. O take é um clip Sample comum, que o upstream já lê.
- **Teste:** `INFINITE_ARRANGERECORDTEST`. Grava um Oscillator, apaga o oscillator e toca o take: o
  nível sai igual (0,5215 contra 0,5216).

**Limites desta versão (próximos passos):**
- Take linear: o loop fica desligado durante a gravação. Takes em loop (punch e comping) ficam para
  depois.
- Sem compensação da latência de entrada do Audio In. Fontes internas (synths) ficam exatas.
- Os WAVs vão para a pasta de gravações; o "Save with media" leva junto.
- Armar não é salvo no patch (decisão de formato fica com o dono).

## 5. O que o TiXL traz para a timeline

Do help oficial (help.tixl.app/using/Timeline):

- **Animar um parâmetro:** Alt+clique no parâmetro cria o primeiro keyframe. Mudar o valor cria
  outros. Um indicador mostra que o parâmetro está animado. Alt+clique na curva insere um key.
- **Dope sheet + editor de curvas** abaixo das pistas. Componentes de vetor (XYZ/RGBA) podem ser
  isolados. O arrasto trava em tempo ou valor, e Ctrl solta. Há interpolação por key (inclusive
  Constant) e tangentes editáveis.
- **Retime de keys:** um indicador de seleção na régua estica ou desloca os keys selecionados.
  Alt+clique cria alças de TimeWarp para retime não uniforme.
- **Marcar batidas ao vivo:** keys Constant + Shift+C durante o play cria um key de valor+1 a cada
  toque, para disparar eventos.
- **Clip:** *slip* (arrastar a fonte sob uma janela fixa), *time remap* (Alt+arrastar a borda muda a
  velocidade, com marca vermelha), Source Offset/Duration no trim, flag de **soundtrack principal**
  (alimenta waveform, FFT e export).

**Proposta de automação (nossa, inspirada no TiXL):**

- **Modelo:** `AutomationLane` presa a uma lane, ou um tipo de lane novo. Guarda o alvo
  (`nodeUid` + **nome** do parâmetro, não ordinal, por causa do C4) e keys
  `{tick, valor, interp: constant/linear/smooth/bezier, tangentes}`. Chaves novas no fim do patch
  (R1).
- **Motor:** a automação entra como um **modulador**, assim reaproveita o caminho que já leva
  modulação ao áudio e o bypass por clip. Primeiro na taxa de frame, depois, se for preciso, com
  precisão de bloco nos parâmetros de áudio.
- **UI:** Alt+clique para criar key, gravação de movimento de knob (write/touch/latch, como num DAW),
  curva desenhada na própria lane, dope sheet, Shift+C para batidas, retime da seleção.
- **Clips:** slip com Alt+arrastar dentro do clip (`sourceOffsetSeconds`). O time remap vem pronto
  com o speed/reverse do upstream.

## 6. PointZ: forças do TiXL e as nossas

O TiXL (`Operators/Lib/Assets/shaders/particles/*.hlsl`, MIT) roda cada força como um compute sobre o
buffer de partículas. Várias recebem um **campo composto** (SDF/ruído) injetado no shader
(`/*{FIELD_FUNCTIONS}*/`). O nosso equivalente: nós PointZ dentro do loop do Particle PointZ somando
em `PartForce`.

| TiXL | Nós | Proposta |
|---|---|---|
| TurbulenceForce | **tem**: Noise PointZ curl em `PartForce` | só um preset |
| ReconstructiveForce | **tem**: Force Target PointZ | — |
| CustomForce (template) | **tem**: GLSL PointZ em `PartForce` | template de força no menu |
| SwitchParticleForce | **tem** em parte: Select/Switch + máscara | — |
| (atração/repulsão) | **tem**: Force Radial PointZ | — |
| (colisão com plano) | **tem**: Collide PointZ | — |
| DirectionalForce | parcial (gravidade no Particle) | **Force Directional**: vento com variação e rajadas |
| VelocityForce | parcial (`PartDrag`) | **Force Velocity**: limite de velocidade, arrasto, alinhar orientação |
| TextureMapForce / ImageForce | parcial (Lookup Texture + Math) | **Force Image**: imagem projetada pela câmera vira força ou giro |
| FieldDistance / FieldVolume / SdfForce | Field PointZ só dá peso e distância | **Force SDF**: gradiente do campo, prende na superfície, orbita |
| MeshVolumeForce / FollowMeshSurface | não | **Force Mesh**: ponto mais próximo no triângulo (força bruta em malha pequena, SDF assado em 3D para malha grande), atrai, repele, quica |
| VectorFieldForce | não | **Force Vector Field**: textura 3D ou pontos com vetor em grade |
| CollisionForce (partícula × partícula) | não | **Collide Particles**: hash espacial na GPU com o radix sort do P2 (`GpuAlgo`) |
| VerletRibbonForce | não | **Ribbon PointZ**: restrição de comprimento em linhas do Trail, primeiro ponto fixo |
| RandomJump / AxisStep / SnapToAngles | não | **Force Step**: movimento quantizado/"glitch" (kernels baratos) |

Cada port segue a skill `pointz-gpu-port` (GPU primeiro, equivalência CPU, R5) e cita a origem MIT
no cabeçalho do arquivo.

## 7. Plano em fases

Cada fase numa branch própria → `turbo/next` → `windows-only`, com o gate da timeline passando.
Pelo relato da seção 3.1, a gravação numa pista armada (item de F3) sobe para logo depois da F0.

- **F0, reprodução e testes:** adaptar os `ARRANGE*` do upstream. Criar um teste do seu caso: 3 pistas
  em paralelo, play, render WAV, RMS por pista. Hoje ele deve falhar.
- **F1, correções e UX rápida:**
  - Ligar o áudio da timeline sozinho ao dar play no painel ou ao soltar mídia, com indicador claro
    (direção de UX: sua).
  - "Add to Timeline" cria uma lane nova por nó, quando a primeira já tem clip, com comprimento do
    tamanho da fonte.
  - Lock de posição em todo clip de áudio (regra do upstream).
  - Sync de BPM só com detecção confiável.
  - Zona de trim maior, cursor visível, fade sem roubar a borda.
  - Loop mais fácil de achar.
- **F2, sincronizar com o upstream:** modelo verbatim, Render Track/Group, speed/reverse, stretch por
  transientes, teclas de DAW, fades de vídeo, clamp de ganho, aviso no master, conflitos.
- **F3, o essencial de DAW (nosso):**
  - Gravar numa pista armada: entrada Audio In ou qualquer nó, WAV na pasta do projeto, clip na
    agulha, sync desligado.
  - Takes com loop (punch).
  - Loop do conteúdo do clip.
  - Bounce in place e stems.
- **F4, automação e keyframes** (seção 5).
- **F5, forças PointZ** (seção 6). Mexe só em `src/pop`, então pode andar em paralelo, numa sessão
  própria.

## 8. Perguntas para o dono

1. Como você gravou as 3 pistas? REC do Audio Out e depois arrastou os WAV? Três Loopers? Outra
   coisa? Tem o `.inf` salvo?
2. O botão TIMELINE estava ON?
3. "Diminuir o tamanho da track" era o comprimento do clip ou a altura da pista?
4. Loop era da região (o transport volta) ou repetir o conteúdo do clip?
5. Portar a Session view do Naman, ou ficar com Clip Matrix e Scenes?
6. O áudio da timeline deve ligar sozinho (F1)?
