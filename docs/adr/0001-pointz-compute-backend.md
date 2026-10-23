# ADR 0001: backend compute para os kernels PointZ (P1)

- Estado: **implementada atrás de flag** (2026-10-22). Passos 1–3 da ordem abaixo feitos:
  `BuildComputeSource`, `RunKernelCompute`, `INFINITE_POP_BACKEND` e `POPEQUIVTEST`. Equivalência:
  diferença máxima 0 em todos os ~40 kernels exercitados (patch de referência, POPTEST, PORTEDTEST).
  Perf A/B: frame 18,4 → 14,05 ms com compute (15,3 ms com TF). Passo 4 feito no mesmo dia:
  **compute é o padrão**; TF é o fallback, testado pelo `POPTFTEST`; o corpus de patches renderiza
  idêntico à v0.62.0.
- Itens: P1 (este), destrava P2, P3, P5, P8 e P9.
- Código: `src/pop/PopGpu.{h,cpp}` (`RunKernel`, `BuildKernelSource`), `src/pop/PopKernelSource.cpp`.

## Contexto

Os operadores `Simple(...)` descrevem o trabalho como um `KernelSpec`: GLSL do corpo, atributos
lidos (`reads`), escritos (`writes`) e de acesso aleatório (`lookups`). Hoje o `RunKernel` gera um
vertex shader e roda por transform feedback (GL 3.2). O que o código mostra:

| Limite atual | Onde | Efeito |
|---|---|---|
| No máximo 4 `writes` | `RunKernel`: "between one and four attributes" | operadores maiores viram 2 kernels ou ficam na CPU |
| Sem escrita aleatória | transform feedback escreve 1:1 por ponto | não dá para fazer scan, compactação, scatter (P2), partículas com nascimento/morte (P3), topologia na GPU (P5, P9) |
| `glGenTextures` + TBO por lookup a cada chamada | `RunKernel`, laço dos lookups | alocação e liberação de objeto GL por kernel por frame |
| `StateGuard` salva estado com `glGet*` | `RunKernel` | espera na fila do driver com a otimização em thread (F4) |
| Entradas como vertex attributes | `glVertexAttribPointer` por leitura | limite de atributos, e o driver trata os buffers como dados de vértice |

O contexto já é GL 4.6 (Startup.inl tenta 4.6 → 3.2), e o PointZ já usa compute com fallback
(redução de bounds, `BoundsComputeResources`). O caminho de 3.2 continua necessário só como recuo.

## Decisão

1. **Mesma API, outro backend.** `KernelSpec`, `KernelUniform`, `KernelEnv` e `RunKernel` não
   mudam de assinatura; nenhum dos operadores `Simple(...)` é reescrito. O corpo GLSL (`body`,
   `decls`, preâmbulo com fbm/curl/eulerDeg) é o mesmo texto.
2. **Geração do compute shader** (`#version 430`, `layout(local_size_x = 256)`):
   - cada atributo de `reads`, `writes` e `lookups` é um SSBO `float[]` com o passo dos seus
     componentes (`p[id*3+k]`), **nunca `vec3[]`**: std430 alinha `vec3` em 16 bytes e quebraria o
     layout compacto que o resto do PointZ usa;
   - `id = int(gl_GlobalInvocationID.x)`, `if (id >= uCount) return;`
   - locais inicializados do SSBO ou do valor padrão quando a entrada não tem o atributo (como
     hoje); ao fim do corpo, as escritas voltam aos SSBOs de saída;
   - `fetch_<nome>(int i)` lê o SSBO do lookup direto (sem TBO);
   - uniforms e built-ins (`uTime`, `uBeat`, `uDelta`, `uFrame`, `uSeed`, `uImage`) iguais.
3. **Saídas** pelo pool (`AcquireGpuBuffer`, já com reuso atrasado do F5). Depois do dispatch,
   `glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
   GL_BUFFER_UPDATE_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT)`, porque os consumidores leem como
   vertex attribute (render), SSBO (próximo kernel) e cópia (AsyncRows).
4. **Sem limite de 4 escritas** no caminho compute. O limite continua só no recuo por transform
   feedback, que divide o spec em grupos de 4 quando precisar.
5. **Escolha do backend:** `INFINITE_POP_BACKEND=compute|tf|both`.
   - Padrão `compute` quando o contexto é ≥ 4.3 e o programa compila.
   - Um spec que falha ao compilar em compute cai para TF uma vez (cache do resultado) e avisa no
     log.
   - `both` roda os dois e compara (abaixo).
6. **Estado GL:** o caminho compute não salva e não restaura com `glGet*`. Ele liga só o que usa
   (programa, bindings de SSBO) e devolve o programa anterior que o chamador informar, o mesmo
   padrão do `RunBoundsCompute`. Isso adianta o F4 para o PointZ.

## Equivalência (regra R5)

- `INFINITE_POP_BACKEND=both`: cada `RunKernel` roda TF e compute sobre a mesma entrada, lê os dois
  resultados (síncrono, só neste modo) e imprime por operador a diferença máxima por atributo.
- Self-test novo `POPEQUIVTEST`: um fixture determinístico (grade 64×64 com P, N, Tex, Color e um
  atributo de 1 componente), que roda cada operador `Simple(...)` com parâmetros fixos nos dois
  backends. Falha se algum atributo diferir mais que a tolerância.
- **Tolerância:** meta 0. A troca de estágio (vertex → compute) pode mudar a fusão de
  multiplicação e soma (fma) no compilador do driver; se acontecer, a tolerância fica em 1e-6
  relativo por componente, documentada por operador em `docs/pointz-gpu-status.md`. Operadores com
  RNG por id usam o mesmo hash, então o resultado não deve mudar.
- O caminho TF fica como recuo e referência até o fim do M3.

## Consequências

- Habilita P2 (scan, compactação, sort, scatter com atomics), P3 (partículas com pool na GPU), P5 e
  P9 (topologia gerada na GPU), P8 (GLSL do usuário com SSBO).
- Some a criação de textura por lookup a cada chamada e o `StateGuard` com `glGet*` no PointZ.
- Exige GL 4.3. Máquinas só com 3.2 continuam no TF; a matriz de testes não tem uma, então o
  recuo é testado forçando `INFINITE_POP_BACKEND=tf` numa rodada da suíte.
- Medição: zones do Tracy por kernel (F2) e o A/B do `perf.ps1` contra `build\perf-ref`.

## Alternativas descartadas

- **Manter TF e só subir o limite de escritas** (`GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS`
  passa de 4 na NVIDIA): não resolve escrita aleatória, que é o que P2, P3 e P5 precisam.
- **Reescrever os operadores um a um em compute:** vários lugares para manter, e R5 por operador
  em vez de um teste único. O gerador central mantém um único caminho de equivalência.
- **CUDA para o PointZ:** amarraria o PointZ à NVIDIA e à edição full (o módulo RTX já é opcional).
  O plano é PointZ funcionando nas duas edições e em qualquer GPU GL 4.3.

## Ordem de implementação proposta

1. `BuildComputeSource` ao lado do `BuildKernelSource`, com teste unitário do texto gerado (A7).
2. `RunKernelCompute` e o seletor `INFINITE_POP_BACKEND`.
3. O modo `both` e o `POPEQUIVTEST`: os ~36 operadores GPU equivalentes.
4. Padrão `compute`; gate completo; A/B do perf; tabela P11 atualizada com a equivalência.
5. Só então P2 em cima do backend.
