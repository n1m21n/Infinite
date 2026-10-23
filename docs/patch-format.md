# Formato de patch `.inf` (C2)

Referência do formato que o Infinite-Turbo lê e grava. Fonte da verdade: `src/core/Patch.cpp`
(`WriteStream` grava, `ReadStream` lê). Este documento descreve o que o código faz em 0.62; muda
junto com ele. Regra R1: um patch antigo abre sem mudança; tipos e chaves salvas nunca mudam.

## Estrutura

Texto, uma linha por registro, tokens separados por espaço. Ordem gravada:

```
infinite-patch 1
# turbo-mod-range
node <idx> <Category> <TypeName>
  uid <n>                      (só quando != 0)
  pos <x> <y>
  flags <showParams> <bypassed> <miniViewport> <advanced> <preview> <colorTag>
  <t> <key> <valor>            (um por parâmetro, ver abaixo)
end
cable <dst> <slot> <src> [<srcOutput>]
geo   <dst> <slot> <src> [<srcOutput>]
aud   <dst> <slot> <src> [<srcOutput>]
note  <dst> <slot> <src> <srcOutput>
mod   <dst> <param|key> <src> <srcOutput> <polarity> <depth> <centre> <inMin> <inMax> <outMin> <outMax> ...
pal | expr | glob | midimap | midismooth | perf* | transport | gesture* | stream* | clip* |
marker | trackgroup | arrange* ...
```

- `infinite-patch` + versão: `kMagic` / `kVersion = 1`. Não subir a versão: chaves e linhas novas
  entram de forma que leitores antigos ignorem.
- `# turbo-mod-range` (`kTurboModMarker`, 0.60): diz que os tokens depois de `<centre>` numa
  linha `mod` são o range mapper do Turbo (`inMin inMax outMin outMax`). Sem o marcador (Turbo
  antigo e todo patch do upstream) o formato é decidido pela forma das linhas
  (`ApplyModTails`); o upstream grava `lo hi [enabled [curve [smoothMs]]]` em unidades do destino.
- Tokens opcionais no fim de uma linha (ex.: `srcOutput` em `cable`, 0.44) ficam de fora quando
  valem o padrão; leitores antigos ignoram tokens a mais.

## Parâmetros de node

Gravados por `Patch::SaveParams` através do `VisitParams` do node (classe `Writer`):

| Prefixo | Tipo | Valor |
|---|---|---|
| `f` | float | `%.9g` (`FloatToString`) |
| `i` | int | decimal |
| `b` | bool | `0` / `1` |
| `c` | cor | três floats `r g b` |
| `s` | texto | uma linha; quebra de linha e barra invertida escapadas (`EscapeLine`) |

A chave salva é `<prefixo> <nome>`; o nome é o que o node passa ao visitor e nunca muda.
Parâmetro novo entra no FIM do `VisitParams` (e da lista de sliders), com padrão que reproduz o
comportamento antigo quando a chave falta.

## Referências por número ou por nome

Desde 0.60 (upstream) uma referência em `mod`, `expr` e afins pode ser número (índice do node,
ordinal do parâmetro, saída) ou palavra (id do node, nome de slot ou saída, chave de controle):
`ReadRef`. O Turbo grava a chave do parâmetro (`saveKey`) quando existe e o ordinal quando não.

Ainda gravam ordinais: `midimap`, `midismooth`, `perf`/`perftarget`, `gesture` (ver C4).

## Caminhos de mídia

Parâmetros `s` com caminho de arquivo. Depois do Collect Media (0.61) os caminhos ficam relativos
à pasta do patch (`<nome>_media/...`); caminhos absolutos continuam válidos.

## Compatibilidade: o que vale hoje (verificado em 2026-10-22)

- Linhas de tipo desconhecido: o leitor é uma cadeia `if (tag == ...)`; uma linha que nenhum ramo
  reconhece é ignorada de propósito (`Patch.cpp:1369`) e também some no próximo save.
- **Chaves de parâmetro desconhecidas NÃO são preservadas.** `LoadParams` entrega ao node só as
  chaves que o `VisitParams` dele pede, e o próximo save grava só essas. Um patch do upstream com
  chaves que o Turbo não lê perde essas chaves se for salvo no Turbo (e vice-versa). O patchcompat
  já lista essas chaves por patch. Pendência do C2: guardar as chaves não lidas por node e
  regravá-las no save.
  Exemplo real no corpus (2026-10-22): `Clip Matrix` (`c0_0_mode`, `c0_2_fon`...) em 2 patches e
  `Output` (`offlineFps`, `offlineDurationSeconds`, `offlinePrerollFrames`) em 4.
- Tipo de node desconhecido: comportamento a documentar (a verificar no `LoadPatch`).

## Teste de compatibilidade

`.claude/skills/run-turbo-tests/patchcompat.ps1` abre um corpus de `.inf` e grava, por node,
tamanho de saída, miniatura, contagens de geometria, valor de modulador, tipos e chaves
desconhecidas; `compare-patchcompat.py` compara dois builds (ok -> crash/hang bloqueia merge).
