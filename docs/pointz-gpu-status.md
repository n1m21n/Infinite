# PointZ: estado GPU por operador (P11)

Tabela viva do eixo PointZ da 0.70 (ver `docs/plans/NEXT-STRATEGY.md`). Atualizar a linha do
operador em todo merge que mexer nele.

**Estado inicial (2026-10-22): classificação heurística por grep** no bloco de cada operador em
`src/pop/PopOps*.cpp` (script de varredura, não leitura do cook). `GPU` = só chamadas de kernel
(RunKernel, KernelSpec, AsyncRows, GpuShiftIn); `CPU` = só acesso a dados na CPU (`Cpu()`,
`Read(`, vetores); `misto` = os dois; `a classificar` = o cook está num helper fora do bloco.
2026-10-22 (noite): os 23 "a classificar" foram lidos à mão; `(lido)` marca essas linhas.
Uma linha passa a valer quando alguém lê o cook, mede e tira o `(heur.)`.

P1 (2026-10-22): `POPEQUIVTEST` cozinha todos os operadores com os dois backends de kernel;
todo kernel deu diferença 0 entre transform feedback e compute.

Colunas: **Caminho** (GPU / CPU / misto / pass-through); **Equivalência** (variável de ambiente
e diferença máxima, R5); **Custo** (ms de CPU / GPU no patch de referência ou num fixture, via
perf.ps1 ou Tracy); **Item** do backlog que migra o operador.

Resumo inicial: CPU 22, misto 5, a classificar 23, GPU 32, pass-through 4 (total 86).
Resumo depois da leitura (2026-10-22): CPU 39, GPU 36, misto 7, pass-through 4 (total 86).

| Operador | Arquivo | Caminho | Equivalência | Custo CPU / GPU | Item | Notas |
|---|---|---|---|---|---|---|
| Accumulate PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Analyze PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Cache Blend PointZ | PopOpsB3.cpp | CPU (heur.) | - | - | | |
| Delete PointZ | PopOpsB.cpp | misto (lido): máscara e topologia na CPU, gather dos atributos na GPU | linhas idênticas (GPUALGOTEST) | - | P5 | modo "por atributo" ainda lê 1 atributo na CPU |
| Extrude PointZ | PopOpsB.cpp | linhas: GPU com entrada feita na GPU (estêncil por topologia, deslocamento por passo num kernel, normais GpuFaceNormals); faces: CPU (MeshOps) | linhas: P e topologia bit a bit, N ≤ 2e-7 (GPUALGOTEST) | - | P5 | faces ainda na CPU |
| Facet PointZ | PopOpsB.cpp | GPU no modo "unique points" com entrada feita na GPU | P idêntico, normais dif. 0 (GPUALGOTEST) | - | P5 | modo "consolidate" ainda na CPU |
| File In PointZ | PopOpsB3.cpp | CPU (heur.) | - | - | | |
| Histogram PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Line Break PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Line Metrics PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Line PointZ | PopOpsGen.cpp | CPU (heur.) | - | - | | |
| Line Resample PointZ | PopOpsB2.cpp | "por contagem": GPU com entrada feita na GPU (comprimentos somados na GPU, busca binária, ApplyStencil); "por comprimento": CPU (o número de pontos depende do comprimento) | ≤ 1e-5 (sqrt e divisão da GPU; GPUALGOTEST) | - | P6 | modo por comprimento na CPU |
| Line Smooth PointZ | PopOpsB2.cpp | GPU com entrada feita na GPU (vizinhos por topologia, um kernel por passe) | P bit a bit (GPUALGOTEST) | - | P6 | |
| Line Thick PointZ | PopOpsB2.cpp | CPU (heur.) | - | - | | |
| Normal PointZ | PopOpsB.cpp | GPU no modo suave com entrada feita na GPU (adjacência na CPU, uma vez por topologia) | dif. 0 (GPUALGOTEST) | - | P5 | modo "tangente de linha" ainda na CPU |
| Pattern PointZ | PopOpsGen.cpp | CPU (heur.) | - | - | | |
| Point Generator PointZ | PopOpsGen.cpp | CPU (heur.) | - | - | | |
| Primitive PointZ | PopOpsB.cpp | CPU (heur.) | - | - | | |
| Revolve PointZ | PopOpsB.cpp | GPU com entrada feita na GPU (estêncil por topologia, matriz por anel num kernel, Tex de tabela, normais GpuFaceNormals) | P, Tex e topologia bit a bit, N ≤ 2e-7 (GPUALGOTEST) | - | P5 | |
| Sort PointZ | PopOpsB.cpp | CPU (heur.) | - | - | | |
| Text PointZ | PopOpsPlatform.cpp | CPU (heur.) | - | - | | |
| Topology PointZ | PopOpsB.cpp | CPU (heur.) | - | - | | |
| Copy PointZ | PopOpsB.cpp | misto (heur.) | - | - | | |
| GLSL Create PointZ | PopOpsB3.cpp | misto (heur.) | - | - | | |
| Normalize PointZ | PopOpsA.cpp | misto (heur.) | - | - | | |
| Particle PointZ | PopOpsB3.cpp, ParticleGpu.cpp | GPU (`engine` = GPU; CPU legado para patches antigos) | GPU = CPU: contagem e ids exatos, atributos 1.2e-7 (PARTICLEGPUTEST) | ref.: 0,06 ms GPU (antes 3,5) | P3 | com PartDeath, contagem lida da GPU (4 bytes por frame) |
| Trail PointZ | PopOpsB3.cpp | misto (heur.) | - | - | | |
| Box PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Cache PointZ | PopOpsB3.cpp | GPU (lido) | - | - | P4 | guarda referências aos buffers (`ShallowCopy`) |
| Cache Select PointZ | PopOpsB3.cpp | misto (lido) | - | - | P4 | seleção por referência; o blend entre quadros lê na CPU (`Read`) |
| Circle PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Convert PointZ | PopOpsB.cpp | misto (lido) | - | - | P9 | refaz só a topologia na CPU; atributos seguem na GPU |
| Curve PointZ | PopOpsB.cpp | CPU (lido) | - | - | P6 | `Load` |
| Feedback PointZ | PopOpsB3.cpp | GPU (lido) | - | - | P4 | guarda referências aos buffers (`ShallowCopy`) |
| File Out PointZ | PopOpsB3.cpp | CPU (arquivo) (lido) | - | - |  | grava arquivo: leitura da GPU é inerente |
| Geometry to PointZ | PopOpsA.cpp | CPU (origem) (lido) | - | - |  | converte malha de node herdado (CPU) |
| Grid PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Instance PointZ | PopOpsB3.cpp | GPU (lido) | - | - |  | marca instâncias por referência (`ShallowCopy`) |
| Line Divide PointZ | PopOpsB2.cpp | GPU com entrada feita na GPU (RowStencil por topologia) | P bit a bit (GPUALGOTEST) | - | P6 | |
| Merge PointZ | PopOpsB.cpp | GPU (lido): cópias de buffer na GPU com entrada feita na GPU; topologia na CPU | linhas, atributos e faces idênticos (GPUALGOTEST) | - | P5 | CPU quando um atributo tem formatos diferentes |
| Plane PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Point File In PointZ | PopOpsB3.cpp | CPU (arquivo) (lido) | - | - | P10 | parse e upload no cook |
| Rectangle PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Select PointZ | PopOpsB.cpp | GPU (lido) | - | - |  | só escolhe e renomeia buffers, sem transferência |
| Sphere PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Sprinkle PointZ | PopOpsB.cpp | CPU (lido) | - | - | P5 | `Load` (amostra faces na CPU) |
| Subdivide PointZ | PopOpsB.cpp | linhas: GPU (estênceis por topologia, WeightedRows); faces: CPU (MeshOps) | linhas: P bit a bit (GPUALGOTEST) | - | P5 | faces ainda na CPU |
| Table to PointZ | PopOpsB3.cpp | CPU (origem) (lido) | - | - |  | converte dados de tabela da CPU |
| Torus PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Tube PointZ | PopOpsGen.cpp | CPU (gerador) (lido) | - | - | P5 | monta na CPU e sobe uma vez; só recalcula quando um parâmetro muda |
| Attribute Combine PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Attribute Convert PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Attribute PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Blend PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Dimension PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Field PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Force Radial PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| GLSL Advanced PointZ | PopOpsB3.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| GLSL Copy PointZ | PopOpsB3.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| GLSL PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| GLSL Select PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Group PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Image to PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Limit PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Lookup Attribute PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Lookup Channel PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Lookup Texture PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Math Combine PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Math Mix PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Math PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Noise PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Phaser PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Point PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Projection PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Quantize PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Random PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| ReRange PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Signal to PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Texture Map PointZ | PopOpsA2.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Transform PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Trig PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| Twist PointZ | PopOpsA.cpp | GPU (heur.) | TF = compute, dif. 0 (POPEQUIVTEST) | - | | |
| In PointZ | PopOpsA.cpp | pass-through (heur.) | - | - | | |
| Null PointZ | PopOpsA.cpp | pass-through (heur.) | - | - | | |
| Out PointZ | PopOpsA.cpp | pass-through (heur.) | - | - | | |
| Switch PointZ | PopOpsA.cpp | pass-through (heur.) | - | - | | |
