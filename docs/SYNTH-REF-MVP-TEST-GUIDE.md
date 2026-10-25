# Synth REF: guia de teste do MVP com o MiniLab 3

Teste prático do nó **Synth REF** (firmware SMK37 OPEN V01; os firmwares FM1 OPEN e SLOOP SMK estão na seção 11) com o Arturia MiniLab 3. Referências:
`docs/SYNTH-REF.md` (arquitetura), `docs/SYNTH-REF-BANCOS.md` (bancos e perfis).

O Synth REF **nunca** grava na memória do MiniLab, não o reinicia e não o atualiza. As
configurações do controlador (seção 3 e seção 5) são feitas por você no Arturia MIDI Control
Center.

## 1. Preparar

1. Compile a versão completa: `build-windows.bat` (sem `mit`). A edição MIT não tem o Synth REF.
2. Na primeira configuração o CMake baixa o llvm-mingw 20260922 (uma vez, precisa de internet)
   e compila `synthref_smk37_open.dll`. Confirme que existe
   `<pasta do exe>\synthref\cores\synthref_smk37_open.dll` e
   `<pasta do exe>\synthref\profiles\arturia-minilab3.smk37_open.json`.
   Sem a DLL o nó mostra o erro "core file missing" e fica mudo.
3. Abra o Infinite. Crie o nó **Synth REF** (categoria **Synths**). A vista **FULL** mostra a
   tela do firmware, os botões, 8 encoders, 4 faders, as rodas PITCH e MOD, 16 pads e 37 teclas.
4. Ligue a saída de áudio do Synth REF em **Audio Out**. Deixe o patch tocando (play do
   Infinite / dispositivo de áudio ativo).

## 2. Escolher a porta MIDI

1. Conecte o MiniLab 3 por USB (cabo de dados, não só de carga).
2. No nó, na linha **MIDI**, abra o menu e escolha a porta principal do MiniLab 3, a que aparece
   com nome do tipo "Minilab3 MIDI". **Não** use as portas MCU, HUI, DIN THRU ou ALV.
3. Marque **lock** se quiser que este nó receba sempre; desmarcado, ele recebe enquanto estiver
   selecionado no editor.
4. A linha de status deve mostrar `connected`, `playing from the device` e o nome do perfil
   "Arturia MiniLab 3 -> SMK-37 (SMK37 OPEN)". Se aparecer "no profile for this device", o nome da
   porta não casa com o perfil: veja a seção 8.

## 3. Programa do MiniLab 3

O perfil assume o programa **ARTURIA** ou um programa **USER** com os CCs de fábrica do MIDI
Control Center. **Não** use o programa DAW: nele os controles falam outros protocolos e os
encoders/faders não chegam como CCs.

Como trocar de programa: use o botão de programa (**Prog**) do MiniLab 3 conforme o manual da
Arturia; **consulte o manual** para a combinação exata de botões e para saber como os programas
são numerados no seu firmware. Confirme o programa ativo pela indicação do próprio MiniLab (como ela aparece depende do modelo
e do firmware: consulte o manual), e depois pela leitura em MIDI DIAGNOSTICS (seção 6): CCs 74, 71, 76, 77 (em qualquer canal) ao girar os encoders 1-4.

## 4. Checklist

Marque cada item. Abra **MIDI DIAGNOSTICS** (botão no fim do nó) para ver as mensagens.

| # | Teste | Esperado | OK |
|---|---|---|---|
| 1 | Teclas e velocity: toque suave e forte | Som sai; teclas do painel acendem; volume acompanha a velocity | [ ] |
| 2 | Pitch strip | O PITCH do painel se move; a nota muda de altura (faixa de +-2 semitons) | [ ] |
| 3 | Mod strip | O MOD do painel se move (CC1) | [ ] |
| 4 | Pedal de sustain (se tiver) | O botão SUSTAIN do painel fica "(down)"; notas seguram | [ ] |
| 5 | 8 encoders | Cada giro faz o encoder virtual correspondente piscar (K1-K4, SELECT, PAGE, PRESETS, VOLUME) e muda o parâmetro na tela do firmware | [ ] |
| 6 | 4 faders | Mexer o fader move o fader do painel. **Âmbar** = aguardando pickup: o fader virtual fica âmbar até o físico cruzar o valor guardado; só depois ele assume | [ ] |
| 7 | Pads banco A (notas 36-43) | Pads 1-8 do painel acendem e disparam | [ ] |
| 8 | Pads banco B (notas 44-51) | Pads 9-16 | [ ] |
| 9 | Botões do painel com o mouse | SHIFT, HOME, SAVE, OCT -/+, ARP, PAD / STEP, ENV / LFO, MIX, EDIT, FX, SEQ, PLAY, REC etc. mudam a tela e os LEDs; o som responde | [ ] |
| 10 | Presets | Menu "factory project..." carrega um projeto de fábrica; com uma trilha DX7 selecionada, o menu de programas (001-128) troca o timbre | [ ] |
| 11 | LOAD FILE com um `.syx` | Banco de 32 vozes (4104 bytes): mensagem "loaded into library bank 4 (programs 97-128)"; os programas 097-128 passam a ter os nomes do banco. Veja `docs/SYNTH-REF-BANCOS.md` | [ ] |
| 12 | Salvar e reabrir o patch | O nó volta com o projeto, o banco importado, o dispositivo e o perfil | [ ] |
| 13 | Duas instâncias | Ver a seção 7 | [ ] |
| 14 | Hotplug | Ver a seção 7 | [ ] |
| 15 | MIDI DIAGNOSTICS | Ver a seção 6 | [ ] |
| 16 | Volume e medidor | O knob de volume (dB) altera o nível; o medidor mostra o pico | [ ] |
| 17 | PANIC | Corta notas presas | [ ] |

Dica para as teclas: se o teclado do firmware estiver numa trilha que não é de teclado, selecione
no painel uma trilha KEY. As notas do controlador são enviadas como MIDI no canal da trilha do
teclado do firmware.

## 5. Template "NT SYNTH REF - MINILAB 3" (configuração manual no MIDI Control Center)

O Synth REF **não** acompanha um arquivo de template binário do MIDI Control Center, porque o
formato desse arquivo não é documentado. Em vez disso, crie você mesmo um programa USER com estas
configurações. Os números abaixo são os mesmos do perfil
`arturia-minilab3.smk37_open.json`.

Passos gerais: abra o MIDI Control Center com o MiniLab 3 conectado, **faça um backup** do que já
está no controlador (função de salvar/exportar do próprio MCC), crie um novo programa
(sugestão de nome: `NT SYNTH REF - MINILAB 3`), ajuste os itens abaixo e envie ao slot USER. Os
nomes exatos de menus e campos variam com a versão do MCC; **consulte o manual do MCC**.

| Controle | Tipo / mensagem | Canal | Observação |
|---|---|---|---|
| Teclado | notas | 1 | |
| Encoder 1 | CC 74 | 1 | K1 |
| Encoder 2 | CC 71 | 1 | K2 |
| Encoder 3 | CC 76 | 1 | K3 |
| Encoder 4 | CC 77 | 1 | K4 |
| Encoder 5 | CC 93 | 1 | SELECT |
| Encoder 6 | CC 18 | 1 | PAGE |
| Encoder 7 | CC 19 | 1 | PRESETS |
| Encoder 8 | CC 16 | 1 | VOLUME |
| Faders 1-4 | CC 82, 83, 85, 17 | 1 | F1-F4, absolutos |
| Encoder principal | CC 114 (relativo) | 1 | PRESETS; shift + principal = CC 112, SELECT |
| Pads (banco A, B) | notas 36-43 (A), 44-51 (B) | 10 | pads 1-16 do SMK |
| Pitch / Mod / Sustain | pitch bend / CC 1 / CC 64 | 1 | |

**Encoders infinitos.** No modo absoluto cada encoder para em 0 e 127. Para giro sem fim:

1. No MCC, configure os 8 encoders como **Relative #1** (o mesmo que `relative64`: 64 = parado,
   acima de 64 = horário, abaixo = anti-horário).
2. Copie o perfil para `Documents\Infinite-Turbo\SynthRef\profiles\`, mude o `id` (por exemplo
   `arturia-minilab3-relative.smk37_open`) e o `name`, e troque `"input": "absolute"` por
   `"input": "relative64"` nas oito entradas dos encoders (CC 74, 71, 76, 77, 93, 18, 19, 16).
   Deixe os faders como `absolute`.
3. No nó, MIDI DIAGNOSTICS, **reload profiles**, e escolha o novo perfil no menu de perfil.

Veja `docs/SYNTH-REF-BANCOS.md`, seção 5, para o formato do perfil.

## 6. Ler o MIDI DIAGNOSTICS

Cada linha mostra canal, tipo, dois dados e o que o Synth REF fez:

```
ch1  CC           74  65  -> encoder 1 (K1)
ch1  NOTE ON      60 100  -> keyboard
ch10 NOTE ON      36 110  -> pad 1
ch1  PITCH BEND    0  72  -> passed to the keyboard track
```

| Resultado | Significado |
|---|---|
| `-> encoder N (nome)`, `-> fader`, `-> pad` | A mensagem foi mapeada pelo perfil e entregue ao controle do firmware |
| `-> keyboard` | Nota tocada na trilha do teclado |
| `-> passed to the keyboard track` | Pitch bend, mod, pressão, program change, ou CC sem mapa, repassados ao canal do teclado |
| `ignored: this Synth REF is not focused (select it, or lock the device)` | Este nó não está ativo: selecione-o no editor ou marque **lock** |
| `dropped: the notes pin is cabled, it plays the notes` | Há cabo no pin `notes`; as notas vêm só do pin |
| `dropped: not in the profile` | Mensagem sem entrada no perfil (outro canal ou outro CC) |
| `notes pin -> keyboard` | Nota que veio do pin |

A linha "Devices:" lista as portas MIDI abertas. As linhas seguintes listam as atribuições do
perfil ativo. Erros de perfil aparecem em vermelho. O botão **clear** limpa o log.

## 7. Duas instâncias e hotplug

**Lock x foco.**

1. Crie dois Synth REF (A e B) e escolha o MiniLab 3 nos dois.
2. Com **lock** desmarcado nos dois: só o nó **selecionado** no editor toca. Selecionar um nó
   que não é Synth REF não troca o foco: o último Synth REF selecionado continua ativo.
3. Marque **lock** em A: A passa a tocar sempre; B toca quando selecionado (então os dois tocam).
4. Confirme que o estado de A (projeto, timbre, faders) não afeta B: cada instância carrega sua
   própria cópia da DLL do firmware (`%TEMP%\Infinite-Turbo\synthref`).
5. Ao perder o foco, notas seguradas são soltas.

**Hotplug.**

1. Com o MiniLab tocando, desconecte o USB. O status passa a
   `not connected - plug it in (the selection is kept)`; a seleção do dispositivo é mantida.
2. Reconecte. Em até cerca de 2 s o nó verifica e mostra `reconnected`. Se mostrar
   `listed by Windows but could not be opened (another app using it?)`, feche o outro programa
   que usa a porta (DAW, MCC) e use **RESCAN**.
3. Teste de novo teclas e encoders.

## 8. Se um controle não responde

1. Abra **MIDI DIAGNOSTICS**. Se nada aparece ao mexer no controle: confirme que o cabo USB leva
   dados, que o dispositivo certo está escolhido, pressione **RESCAN**, e feche outros programas
   que usam a porta (o MCC, uma DAW).
2. Teclas tocam mas knobs não: o MiniLab precisa estar no programa ARTURIA ou USER (não DAW), com
   knobs com os CCs de fábrica, em qualquer canal (veja as linhas do perfil), ou use **MIDI LEARN**.
3. Pads errados: banco A = notas 36-43, banco B = 44-51, no canal 10.
4. `ignored: ... not focused`: selecione o nó ou marque **lock**.
5. `no profile for this device`: o nome da porta não contém nenhum `device.match` do perfil, ou
   contém um `device.exclude`. Ajuste o perfil (seção 5 de `docs/SYNTH-REF-BANCOS.md`).
6. Perfil alterado e nada mudou: **reload profiles**.
7. **MIDI LEARN** como alternativa: no modo MIDI LEARN os 8 encoders, 4 faders e 16 pads viram
   parâmetros do nó; clique com o botão direito num controle para usar o MIDI learn do Infinite.
8. Ainda travado: procedimento opcional abaixo.

## 9. Reset de fábrica do MiniLab 3 (opcional, manual)

Este procedimento é **da Arturia**, não do Synth REF, e **apaga as configurações** do controlador.
Use só se o MiniLab estiver com comportamento inconsistente. **Confirme os passos no manual da
Arturia antes de executar.**

1. No MIDI Control Center, faça um **backup** de todos os programas/templates.
2. Desconecte o cabo USB.
3. Segure **Octave Up** e **Octave Down** juntos.
4. Reconecte o USB **sem soltar** os dois botões (siga o manual para saber quando soltar).
5. Reenvie seus templates com o MCC.

Isto é diferente de atualização de firmware ou do modo de recuperação, que são outros
procedimentos da Arturia. O Synth REF nunca reinicia nem atualiza o controlador.

## 10. Registrar o resultado

Anote data, versão do build, programa do MiniLab usado (ARTURIA ou USER), perfil ativo e os
itens da tabela da seção 4 que falharam, com a linha do MIDI DIAGNOSTICS correspondente.

## 11. Escolher o firmware: FM1 OPEN e SLOOP SMK

Até aqui o guia usa o firmware padrão, **SMK37 OPEN V01**. O nó tem outros firmwares, escolhidos no
**menu suspenso no topo do nó** (nome do instrumento + firmware). Estão selecionáveis:

| Firmware | Instrumento | Estado | Perfil do MiniLab 3 |
|---|---|---|---|
| SMK37 OPEN V01 | SMK-37 Pro | Supported | `arturia-minilab3.smk37_open.json` |
| FM1 OPEN V14 | FM-1 | Supported | `arturia-minilab3.fm1_open.json` |
| SLOOP SMK 2.6 (SLOOP 2.8) | SMK-37 Pro | Experimental | `arturia-minilab3.sloop_smk.json` |

Entradas Unavailable (Stock V016, SLOOP 2.8.1 do FM-1) aparecem na lista com "(unavailable)" e
não podem ser abertas (ao escolhê-las, o nó mostra o motivo); a experimental aparece com
"(experimental)". Antes de testar, confirme que as DLLs `synthref_fm1_open.dll` e `synthref_sloop_smk.dll`
estão em `<pasta do exe>\synthref\cores` (junto de `synthref_smk37_open.dll`) e que os três perfis
estão em `<pasta do exe>\synthref\profiles`. Ao trocar de firmware, o painel muda: ele é montado
com os controles que o firmware declara. Cada firmware tem seu próprio estado salvo no patch.
Escolha o perfil do firmware certo no menu de perfil (ou "auto profile"); o status da linha MIDI
deve mostrar o nome dele.

### FM1 OPEN (FM-1)

Painel esperado: 14 botões (OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY,
REC), 7 encoders (SELECT, ALGORITHM, KNOB 1-4, PRESETS), o potenciômetro de volume como fader
**VOLUME** e 27 teclas (F3 a G5) com LEDs. O FM-1 **não tem pads**.

| # | Teste | Esperado | OK |
|---|---|---|---|
| F1 | Teclas e velocity | Som sai; as teclas do painel acendem | [ ] |
| F2 | Encoders 1-4 do MiniLab | KNOB 1-4 do painel se movem e mudam o parâmetro na tela | [ ] |
| F3 | Encoder 5, encoder 6, encoder 7 | SELECT, ALGORITHM e PRESETS; o encoder principal = PRESETS, shift + principal = SELECT | [ ] |
| F4 | Fader 1 | Move o VOLUME do painel (0-127) e o nível do som | [ ] |
| F5 | Pads do MiniLab, banco A (notas 36-43, canal 10) | Apertam, nesta ordem: PLAY, REC, SEQ, ARP, HOME, SAVE, OCT -, OCT + | [ ] |
| F6 | Pads do MiniLab, banco B (notas 44-49) | Apertam FX, SEL, ENV, LFO, EDIT, GLO | [ ] |
| F7 | LOAD FILE com `.syx`, `.fm1`, `.fm1t` | Mesmos formatos do SMK37 OPEN (veja `docs/SYNTH-REF-BANCOS.md`) | [ ] |
| F8 | Salvar e reabrir o patch | O firmware volta como estava | [ ] |

Limitação conhecida: o FM1 OPEN não informa a oitava do teclado ao painel.

### SLOOP SMK (SMK-37 Pro, experimental)

Painel esperado: 18 botões (KNOB BANK, FADER BANK, <, >, OCT -, OCT +, ARP, NOTE REPEAT, SCALE,
CHORD, GLO, BT / EDIT, PATCH / HOME, PARA / SAVE, FX, SEQ, PLAY / STOP, REC), 8 encoders (VOLUME,
TEMPO, PRESETS, TRACK, K1-K4), faders F1-F4, rodas de pitch e mod, sustain, 37 teclas e 16 pads
com luz. Os **ícones dos cartões de knob na tela são brancos (em branco)**: a arte do Felucca é de
direitos reservados e não vem junto.

| # | Teste | Esperado | OK |
|---|---|---|---|
| S1 | Teclas e velocity | Som sai (os motores são os do SLOOP, não DX7 / 303 / 808) | [ ] |
| S2 | Encoders 1-4 do MiniLab | K1-K4 | [ ] |
| S3 | Encoders 5, 6, 7, 8 | TEMPO, PRESETS, TRACK, VOLUME; encoder principal = PRESETS, shift + principal = TRACK | [ ] |
| S4 | Faders 1-4 | F1-F4 (o firmware faz o pickup) | [ ] |
| S5 | Pads (canal 10, notas 36-51) | Pads 1-16 do painel acendem e disparam | [ ] |
| S6 | Presets | Pelo próprio instrumento, com PRESETS / PATCH: **não há** menu de programas nem de projetos de fábrica no nó | [ ] |
| S7 | LOAD FILE | Não aceita arquivos neste firmware | [ ] |
| S8 | Salvar e reabrir o patch | O estado do firmware (cerca de 478 KB em base64 no patch) volta: projetos, presets de usuário, arranjo | [ ] |
| S9 | Duas instâncias, hotplug, MIDI DIAGNOSTICS | Como nas seções 6 e 7 | [ ] |

Se o MiniLab estiver no programa DAW ou o perfil errado estiver selecionado, os controles não
respondem: veja a seção 8.
