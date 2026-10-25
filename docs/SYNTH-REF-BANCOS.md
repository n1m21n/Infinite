# Synth REF: bancos, timbres e perfis sem recompilar

Este guia mostra como colocar bancos DX7, projetos e perfis de controlador no Synth REF sem
recompilar nada. Documentação técnica: `docs/SYNTH-REF.md`. Teste prático com o MiniLab 3:
`docs/SYNTH-REF-MVP-TEST-GUIDE.md`.

## 1. Pasta do usuário

```
Documents\Infinite-Turbo\SynthRef\
    (seus bancos .syx, projetos .fm1, timbres .fm1t)
    profiles\          (seus perfis de controlador .json)
```

- A pasta é criada automaticamente no primeiro uso.
- No nó, o botão **BANKS FOLDER** abre essa pasta no Explorer.
- Os botões **LOAD FILE** e **SAVE PROJECT** abrem a caixa de arquivo já nessa pasta.
- Os perfis que acompanham o programa ficam em `<pasta do exe>\synthref\profiles`; não edite
  esses, copie para a pasta do usuário (seção 5).

## 2. Formatos aceitos (botão LOAD FILE)

| Arquivo | Tamanho exato | Para onde vai |
|---|---|---|
| `.syx` banco DX7 de 32 vozes | **4104 bytes** (`F0 43 0n 09 20 00` + 4096 bytes de dados + checksum + `F7`) | Banco **4** da biblioteca do firmware = programas **97-128**. Os bancos de fábrica (programas 1-96) não são tocados |
| `.syx` voz DX7 única | **163 bytes** (`F0 43 0n 00 01 1B` + 155 bytes + checksum + `F7`) | Dentro da trilha **DX7 selecionada** (substitui o timbre dela) |
| `.fm1` | no máximo o tamanho de um projeto do firmware (o núcleo valida) | Projeto do firmware (formato do workbench do SMK37 OPEN); vira o projeto de trabalho |
| `.fm1t` | tamanho da estrutura de timbre do firmware | Timbre no formato do workbench do SMK37 OPEN |

**Qual firmware aceita quais arquivos**

| Firmware | LOAD FILE |
|---|---|
| SMK37 OPEN V01 | `.syx` (voz única e banco de 32 vozes), `.fm1`, `.fm1t` |
| FM1 OPEN V14 (FM-1) | os mesmos: `.syx` (voz única e banco de 32 vozes), `.fm1`, `.fm1t`; o formato de estado salvo no patch também é o mesmo (SRS1) |
| SLOOP SMK 2.6 (SLOOP 2.8) | **nenhum**: LOAD FILE não aceita arquivos. Os presets são escolhidos no próprio instrumento (PRESETS / PATCH) e o estado inteiro do firmware (projetos, presets de usuário, arranjo, patches FM6, ajustes, MIDI learn, memória de samples) fica salvo no patch (formato próprio "SRL1", cerca de 478 KB em base64) |

A tabela e as observações abaixo valem para SMK37 OPEN e FM1 OPEN. Nesses dois, o formato do
`.fm1` e do `.fm1t` é o do workbench do respectivo firmware.

Observações:

- Qualquer arquivo acima de 1 MiB é recusado. Um `.syx` com outro tamanho é recusado com a
  mensagem informando quantos bytes ele tem.
- O checksum do `.syx` é conferido; arquivo com checksum errado é recusado pelo núcleo do firmware.
- Para a voz única: **selecione antes uma trilha DX7** no painel (a linha de presets mostra
  `T<n> DX7` quando a trilha selecionada é DX7). Com outra fonte selecionada (303, 808, PSG,
  PLUCK, SAMPLE) o nó responde "refused by the firmware (-4: select a DX7 track first)".
- A mensagem do resultado aparece em cinza abaixo do cabeçalho do nó ("DX7 32-voice bank loaded
  into library bank 4 (programs 97-128)", por exemplo). Depois de importar um banco, a lista
  de programas é atualizada e o menu de programas DX7 mostra os novos nomes (programas 97-128).
- Códigos de recusa: `-2` formato ou checksum inválido, `-3` o firmware não aceitou a gravação,
  `-4` a trilha selecionada não é DX7.
- **Importação de samples (WAV para o banco de samples do firmware) ainda não está implementada.**

## 3. As importações ficam salvas no patch

O estado completo do firmware (projetos, biblioteca DX7 com o banco que você importou, projeto de
trabalho) é gravado dentro do patch do Infinite. Ao reabrir o patch, o banco importado continua
lá, sem precisar do arquivo `.syx`. Cada instância de Synth REF tem seu próprio estado.

Para guardar o projeto de trabalho fora do patch, use **SAVE PROJECT** (grava um `.fm1`).

## 4. Procedência e licença: leia antes de distribuir

- As **128 vozes DX7 de fábrica** que vêm nos firmwares SMK37 OPEN e FM1 OPEN são Apache-2.0 e
  acompanham o núcleo. O SLOOP SMK traz amostras CC0 e dados próprios compilados no núcleo.
- Bancos `.syx` de terceiros (por exemplo, as cópias de cartuchos ROM/VRC da Yamaha encontradas
  em repositórios na internet) têm **procedência desconhecida**. Eles podem ser material
  protegido por direitos autorais.
- Por isso o Synth REF **nunca inclui** esses bancos no programa ou no repositório. Você os
  importa por conta própria, para uso pessoal.
- **Não redistribua** bancos de origem desconhecida (nem dentro de patches publicados: o patch
  carrega o banco importado dentro do estado). Para compartilhar um patch, importe apenas bancos
  que você pode redistribuir.
- Bancos criados por você, ou com licença livre explícita, podem ser compartilhados normalmente.

## 5. Escrever seu próprio perfil de controlador

Um perfil diz quais mensagens MIDI do seu controlador acionam quais controles do instrumento
(modo **AUTO MAP**). É um arquivo JSON; não precisa recompilar.

1. Copie `<pasta do exe>\synthref\profiles\arturia-minilab3.smk37_open.json` para
   `Documents\Infinite-Turbo\SynthRef\profiles\` e dê um novo nome ao arquivo.
2. Edite o campo **`id`** para um valor novo e único. (Se você mantiver o mesmo `id` de um perfil
   que acompanha o programa, a sua cópia **substitui** o original.)
3. Ajuste **`name`** (texto mostrado no nó) e **`mode`** (anotação livre sobre o modo do
   controlador).
4. Em **`device.match`** coloque pedaços do nome da porta MIDI do seu controlador (sem diferenciar
   maiúsculas/minúsculas) e em **`device.exclude`** os pedaços das portas que não devem casar
   (portas MCU, DIN THRU, etc.). Para ver o nome exato, abra **MIDI DIAGNOSTICS** no nó: a linha
   "Devices:" lista as portas abertas.
5. Ajuste canais e números:
   - `keyboard.channel`: canal do teclado do controlador (1-16).
   - `pads`: `channel`, `first_note`, `count`, `first_id` (id do primeiro pad, começando em 0).
   - `controls[]`: uma entrada por mensagem, com `cc` (ou `note`), `channel`, `target`
     (`encoder`, `fader`, `button`, `pad`, `pedal`, `pressure`, `keyboard`), `id` (começa em 0) e
     `input`.
   - `input`: `absolute` (0-127 de posição), `relative64`, `relative2c` ou `relative16`
     (encoders infinitos; `relative64` é o "Relative #1" da Arturia).
6. Ids por firmware:
   - SMK37 OPEN: encoders `id` 0-7 = K1, K2, K3, K4, SELECT, PAGE, PRESETS, VOLUME; faders `id`
     0-3 = F1-F4.
   - FM1 OPEN: encoders `id` 0-6 = SELECT, ALGORITHM, KNOB 1-4, PRESETS; fader `id` 0 = VOLUME;
     botões `id` 0-13 = OCT-, OCT+, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC
     (o FM-1 não tem pads: o perfil do MiniLab usa os pads para apertar botões).
   - SLOOP SMK: encoders `id` 0-7 = VOLUME, TEMPO, PRESETS, TRACK, K1-K4; faders `id` 0-3 = F1-F4.
7. Salve o arquivo e, no nó, abra **MIDI DIAGNOSTICS** e clique em **reload profiles**. Se houver
   erro de sintaxe ou de campo, ele aparece em vermelho ("profile error: ...") e o perfil não é
   carregado. A lista de linhas do perfil ativo mostra o que foi mapeado.
8. No menu de perfil do nó, escolha "auto profile" (o primeiro perfil que casa com o dispositivo)
   ou o seu perfil pelo nome.

Esquema completo, campo por campo: `docs/SYNTH-REF.md`, seção 6.

Dica: um encoder configurado como absoluto para em 0 e em 127. Para um giro infinito, configure o
encoder como relativo no controlador e use `"input": "relative64"` (ou o que o seu controlador
enviar) no perfil.
