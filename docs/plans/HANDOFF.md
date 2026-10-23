# Passagem de trabalho: turbo/next (0.70)

Para a próxima sessão ou IA. Leia nesta ordem: este arquivo, `NEXT-STRATEGY.md` (seção 8,
checklist), `NEXT-BACKLOG.md` (o que cada ID significa), `CHANGELOG-TURBO.md` (seção 0.70).

## Estado (2026-10-22, 0.70 fechada no turbo/next)

- Branch `turbo/next`, ~60 arquivos alterados, **nada commitado**. Versão já 0.70.0 (CMakeLists,
  README, PACKAGE_MANIFEST). Falta: o dono testar no app; depois merge em `turbo/windows-only`,
  commit e tag `v0.70.0` **só com o aval dele**.
- Gate (2026-10-23, com Render 3D FX revisado): 73 checks + MIT 8/8, compat 43 patches sem
  regressão (3 diferem de propósito: HDRI agora de pé), perf −23,6% contra `build\perf-ref`.
  DRAGTEST injeta mouse: se falhar no gate e passar sozinho, foi mouse real/foco durante o teste.
- Escopo fechado da 0.70: M0; M1 menos C4/A1/F4/U2; P1, P3; P5/P6 de linhas (Delete, Merge,
  Normal, Facet, Subdivide, Extrude, Revolve, Line Smooth/Divide/Resample por contagem); M4 parcial
  (MV de câmera, TAA, FSR 1, render scale); R1 (cull/LOD na GPU, também na sombra).
- O que foi para a 0.71 está no topo da seção 0.70 do `CHANGELOG-TURBO.md`.

## Regras do dono

- Responder em português. Código e comentários em inglês, estilo do arquivo (3 espaços, chave na
  linha seguinte, comentário `// Turbo 0.70 (ID): por quê`).
- Autonomia: decidir questões técnicas e seguir, sem parar para perguntar. Parar só para commit,
  push, formato de arquivo que afeta o upstream, ou direção de UX.
- R1: patches `.inf` abrem sem mudança. Parâmetro novo sempre no FIM do `VisitParams`; quando a
  ausência da chave deve significar o comportamento antigo, usar `PopDef::savedBefore` (PointZ) ou
  `INode::BeforeLoadParams`.
- R2: código herdado do Naman fica verbatim. R5: toda migração CPU→GPU com teste de equivalência.

## Como validar (gate)

```
powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\run-turbo-tests\driver.ps1 -Perf -Compat
powershell -NoProfile -ExecutionPolicy Bypass -File .claude\skills\run-turbo-tests\driver.ps1 -Mit -Quick
```
Rápido, durante o trabalho: `-SkipBuild -Only GPUALGOTEST,PARTICLEGPUTEST,POPTEST,POPTFTEST,POPEQUIVTEST`.
Build: `build-windows.bat nopack` (o driver chama). Rodar o gate em segundo plano; leva ~15 min.

Ferramentas: `INFINITE_GPUPROF=1` (GPU ms por grupo), `INFINITE_FRAMEPROF=1`,
`INFINITE_GLDEBUG=sync` (erro/aviso GL com arquivo:linha), `perf.ps1 -PatchFiles a.inf,b.inf`.
Variantes de teste do patch de referência em `build\perf\corpus\3d_noise_teste 56fps\`
(`ref_particles_gpu.inf`, `stress_1M_gpu.inf`, `msaa_s1/s2.inf`).

## Armadilhas que já custaram tempo

- **Nunca editar fonte enquanto um build roda em segundo plano** (o build pega o meio da edição).
- GateGuard bloqueia a 1ª edição de cada arquivo: declarar os fatos e repetir. Em lote paralelo,
  uma edição pode passar e outra não: conferir e reaplicar a bloqueada logo.
- Heredoc no Bash engole barras invertidas (vira `\t`, `\r`): escrever com Write/Edit ou Python
  com `chr(92)`. PowerShell: nomes de variável não diferenciam maiúsculas (`$Patches` = `$patches`).
- Tempo de CPU alto num node (FRAMEPROF) pode ser a CPU esperando a GPU: confirmar com GPUPROF.
- Kernels PointZ: corpo pode usar `precise` (o fallback TF o define como vazio); SSBO sempre
  `float[]`, nunca `vec3[]`; padrão de equivalência bit a bit = `gpu::RowStencil` + `ApplyStencil`.

## Próximos passos (0.71), por valor medido

1. F4 GlState: um cache de estado GL que elimine os `glGet*` por chamada (GpuAlgo `Scope`, PopGpu
   `StateGuard`, Render 3D) - regra do dono "zero glGet por frame".
2. Qualidade do RTX em cena animada. Feito (2º teste do dono): acumulação temporal reprojetada
   (`rtxReproject`, vetores de movimento do primeiro hit com câmera e matriz anterior de cada
   instância), HDRI enfim visto pelo RTX (era RGB, o CUDA não mapeia) e na orientação certa, cores e
   rim iguais ao raster, menos grão. Também feito (to-do do 2º teste): motion blur nos dois modos
   (`motionBlur`; raster por `Render3DMotion`, RTX por `rtxMotionBlur`), partículas seguidas por
   `PartId` (anel na GPU) na acumulação e no blur, AI em movimento = modelo temporal do OptiX, AI
   em render scale 2x = upscaler temporal 2x do OptiX, HDRI revisado (EXR e `.hdr` entram de baixo
   para cima pelo próprio carregador; preview, fundo, raster e RTX coerentes). No compat, os 3
   patches do corpus com HDRI (3d_noise_teste_56fps x2, paartCubes) mudam por isso - esperado,
   falta o aval visual do dono. Falta:
   - **RTX fica de lado até a 0.72** (decisão do dono depois do 3º teste: o raster entrega imagem
     melhor; o path tracer em tempo real de ~5 ms/frame, 4-8 amostras e filtro simples não chega perto
     de um render offline, e o enxame de espelhos minúsculos em movimento é o pior caso). Para a 0.72,
     avaliar o caminho do NVIDIA RTXPT (github.com/NVIDIA-RTX/RTXPT, v1.8.1): ReSTIR DI/GI (RTXDI),
     denoiser NRD (ReLAX/ReBLUR) e DLSS Ray Reconstruction/SR via Streamline - é o que faz path
     tracing em tempo real ficar limpo. Ele é DX12/Vulkan (framework Donut), não OptiX: seria um
     módulo RTX novo em Vulkan com interop de memória com o GL (EXT_memory_object/semaphore), só na
     edição full, licenças do RTXPT/NRD/RTXDI/DLSS a conferir (não são MIT). Alternativa mais barata:
     o RTX como "render final" offline (centenas de amostras por frame na exportação).
   - Modo híbrido (primeiro hit em raster + reflexo/sombra/AO traçados), o caminho de Unreal/Notch
     para cena que se mexe - entra na mesma avaliação da 0.72.
   - Feito (3º teste, opção A do dono - raster primeiro): nó **Render 3D FX** depois do render (AO,
     reflexos em tela, profundidade de campo, motion blur até 8 frames), TAA com movimento de objeto
     (passe de velocidade nas mesmas listas de cull/LOD), lista de fontes (GDI + JUCE; dropdown no
     Text PointZ). Falta medir o custo do FX e da velocidade no patch do dono e o aval visual dele;
     motion blur com reconstrução de silhueta (McGuire) se o simples não bastar; R3 (sombras CSM)
     ainda dentro do Render 3D.
   - Opção de "look" para o RTX parecer o raster em enxame denso de espelhos (o raster ignora o
     inter-reflexo entre cópias; o RTX não) - só se o dono pedir.
   - `RegisterTex` faz `glGetTexLevelParameteriv` por textura por frame (regra "zero glGet"): detectar
     realocação de textura sem perguntar ao GL precisa do cache de estado do F4.
   - Fontes nos nós de texto: feito (ver acima); conferir no teste do dono que a lista aparece e que o
     JUCE desenha as famílias que o GDI lista (uma que o JUCE não ache cai no padrão dele).
3. R1 resto: um único `glMultiDrawElementsIndirect` para todos os slots, Hi-Z; R3 (sombra mais barata).
   O LOD já é um impostor exato (raio-caixa por pixel); para formas que não são caixa, impostor
   genérico (esfera / octaedro).
4. Áudio: fixtures para os 16 nós "idle no rig genérico" do AUDIOPARAMSWEEPTEST (Drum Sequencer,
   MPC, Sampler, Granular, Looper... precisam de sample, gravação ou pattern carregados).
3. M4 resto: X1 SVGF com `Render3DTemporal::MotionVectors`; MV de objeto.
4. PointZ: P2 sort, faces de Subdivide/Extrude na GPU, Line Resample por comprimento, contagem
   de `PartDeath` sem readback (um frame atrasada ou indirect).
5. Pendentes do dono: C4, U1-U3, A1/A2; M6 (V1-V5, C1).
