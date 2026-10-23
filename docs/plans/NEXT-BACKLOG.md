# Infinite-Turbo next: backlog técnico pós-0.62.0

Fonte dos IDs usados em [NEXT-STRATEGY.md](NEXT-STRATEGY.md). A ordem e os marcos estão lá;
aqui ficam o conteúdo e o aceite de cada item. Notas `[estado]` foram verificadas no código em
2026-10-22.

## 0. Contexto

- Repo `C:\Users\veeja\Documents\GitHub\Infinite`. Estável: `turbo/windows-only` (v0.62.0).
  Integração: `turbo/next`.
- Mod Windows do Infinite (Naman Soni, upstream `github.com/n1m21n/Infinite`). C++, OpenGL 4.6,
  ImGui + imgui-node-editor, JUCE (áudio/VST3), FFmpeg, OpenCV, assimp, GLFW.
- Máquina de referência: RTX 4060, driver 610.88, Windows 11, MSVC 19.44. CUDA 11.8 completo
  (12.4 sem nvcc).
- Edições: full (GPL-3.0, `src/gpl`, ASIO, RTX) e MIT (`INFINITE_ENABLE_GPL=OFF`). Módulo não-MIT
  só como opcional da full.
- Build: `cmake --build build/windows-vs2022 --config Release --target Infinite` →
  `build/windows-vs2022/Release/Infinite-Turbo.exe`.
- Testes: `test-windows.bat` (`-SkipBuild`, `-Quick`) → `build/test-results/summary.txt`.
  Baseline conhecida: ROUNDTRIPTEST (copy/paste Drum Sequencer e Looper) e AUDIOPARAMSWEEPTEST
  (`.claude/skills/run-turbo-tests/known-baseline.md`).
- Patchcompat: `.claude/skills/run-turbo-tests/patchcompat.ps1` + `compare-patchcompat.py`.
- Perfil: `INFINITE_FRAMEPROF=1` (CPU/GPU por etapa, nodes lentos, Render 3D, RTXPROF);
  `INFINITE_PATCHCOMPAT=<json>` + `INFINITE_PATCHCOMPAT_PLAY=1` + `INFINITE_LOADPATCH=<inf>`
  (tempo por node PointZ); `INFINITE_DUMPNODE=<idx>` + `INFINITE_DUMPPNG=<png>`.
- Patch de referência: `C:\Users\veeja\Desktop\__infinite\collect01\3d_noise_teste 56fps.inf`
  (Grid, Noise, Image to PointZ, Transform, Copy, Instance ×2, Particle, Render 3D 1080p). Não
  modificar; testes usam cópia no scratchpad com mídias em caminho absoluto. Hoje: raster ~19 ms,
  RTX ~25 ms (0.61: ~60 ms e ~1 fps).

### Regras

- **R1 Arquivo:** `.inf` (`infinite-patch 1`, `node <idx> <Category> <TypeName>`, chaves
  `i/f/b/c/s`, `cable`, `end`) abre sem mudança. Tipos e chaves salvas nunca mudam (renomear só via
  `NodeFactory::CanonicalName`). Parâmetro novo sempre no fim do VisitParams e da lista de
  sliders (ordinal = chave de modulação).
- **R2 Upstream:** código do Naman porta-se verbatim. Módulos herdados (Particle System, Image to
  Points, Instance on Points...) ficam como estão. Mudança de performance em herdado só se
  invisível ao resultado.
- **R3 GPU:** todo PointZ na GPU o máximo possível; a CPU nunca espera a GPU por frame.
  `pop::AsyncRows` (`src/pop/PopGpu.h`), `GLUtil::AsyncReadback` (`src/core/GLUtil.h`), fences.
- **R4 Fluxo:** branch por tarefa; commit, merge, push e tag só com aval do dono. CHANGELOG,
  versão e TODO atualizados. Respostas em português.
- **R5 Equivalência:** migração CPU→GPU de operador PointZ com variável de ambiente que roda os
  dois caminhos e compara atributo a atributo (padrão do Copy PointZ); diferença 0 ou tolerância
  documentada; CPU fica como fallback.
- **R6 Ambiente:** o GateGuard bloqueia a primeira edição de cada arquivo e o primeiro Bash da
  sessão (declarar os fatos e repetir). Heredoc no Bash converte `\\n` em quebra real dentro de
  strings C: usar Edit ou script Python. `main.cpp` = fragmentos `.inl` num namespace anônimo;
  limite de aninhamento do MSVC → dispatch `DrawPortedNodeParams`.

### Mapa de código

- PointZ: `src/pop/` (PopData, PopGpu = kernels por transform feedback, PopKernelSource, PopCpu);
  operadores em `PopOps*.cpp` (`Simple(...)` = GPU, `Cpu(...)` = CPU); node `src/nodes/PopNode.*`.
- 3D: `src/nodes/Geometry3DNodes.*` (Render 3D, GpuMesh, SceneSignature); `src/nodes/Render3DPost`
  (HDR, IBL, panorama); previews `src/core/NodeViewport`.
- RTX: interface `src/nodes/RtxBackend.h`; stub `src/nodes/RtxStub.cpp`; módulo `src/rtx/`
  (RtxOptix.cpp, RtxDevice.cu, RtxKernels.cu, RtxShared.h); CMake bloco "RTX render mode";
  `cmake/RtxEmbed.cmake`.
- App: frame `src/app/frame/*.inl`; previews `src/app/ui/Preview.inl`; corpos
  `src/app/bodies/*.inl`; Collect Media `src/app/graph/CollectMedia.inl`; selftests
  `src/app/selftest/*.inl`; registro/menu `src/core/NodeFactory.cpp`, `src/core/CategoryColors.cpp`.

## 1. Base: medição, regressão, higiene de GPU

- **F1 PERFTEST:** `perf.ps1` roda um corpus (começando no collect01, mídias copiadas) com
  PATCHCOMPAT_PLAY + FRAMEPROF; grava ms de CPU/GPU por frame e por node em JSON; compara com
  baseline versionada; falha se piorar > 10%. Aceite: `test-windows.bat -Perf`.
- **F2 Tracy (BSD-3):** opcional `INFINITE_ENABLE_TRACY`. Zonas de CPU nas etapas do frame,
  `PopNode::CookIfNeeded`, `Render3DNode::CookIfNeeded`, `RunKernel`; GPU via TracyOpenGL e
  TracyCUDA. FRAMEPROF fica como modo texto.
- **F3 Marcadores:** `glPushDebugGroup`/`glObjectLabel` em kernels PointZ, passes do Render 3D,
  post e previews; `GL_KHR_debug` com callback em debug.
- **F4 GlState:** cache de programa, VAO, FBOs, buffers, viewport, blend/depth/cull/scissor;
  trocar save/restore com `glGet*` por push/pop. Aceite: zero `glGet*` por frame.
  `[estado]` `glGet*` em 21 arquivos.
- **F5 Pool de buffers:** `pop::AcquireGpuBuffer` reutiliza buffer liberado no mesmo frame (WAR na
  GPU). Carimbar com fence/frame, reutilizar após 2 frames; avaliar `glBufferStorage` imutável e
  arena por frame.
- **F6 Leituras síncronas:** ImageSpectralSynthNode e WaveTerrainNode (`glReadPixels` quando a
  textura muda); `FluidNode::MeasureDivergence` (confirmar se é só teste); `PopData::Read/Cpu()`
  em operadores `Cpu(...)` sobre entrada feita na GPU. Aceite: grep de
  `glReadPixels|glGetTexImage|glGetBufferSubData|->Cpu()|.Read(` no caminho por frame só com
  usos assíncronos ou justificados.
- **F7 Build MIT:** compilar `build/windows-vs2022-mit`, confirmar stub RTX ("RTX is not in this
  build"); matriz full/MIT no driver de testes.

## 2. PointZ 100% GPU

Trocar transform feedback (GL 3.2, máx. 4 saídas, sem escrita aleatória) por compute GL 4.3+ com
SSBOs, mantendo a API de KernelSpec. `[estado]` 86 operadores; tabela heurística inicial em
`docs/pointz-gpu-status.md` (32 GPU, 22 CPU, 5 mistos, 23 a classificar, 4 pass-through).

- **P1 Backend compute:** `RunKernel` gera compute (`local_size_x=256`), atributos como SSBO, sem
  limite de escritas, lookups lendo SSBO direto; mesmo GLSL de corpo (body, decls, preâmbulo
  fbm/curl/eulerDeg); fallback para TF. Aceite: todos os Simple idênticos sobre fixture
  determinística; tempo de GPU menor (F2).
- **P2 `src/pop/GpuAlgo`:** scan e stream compaction; radix sort float/uint; reduções
  (min/max/soma/histograma); scatter com atomics; geração de índices de topologia. Referência:
  técnicas de CUB/Thrust (Apache-2.0) ou decoupled look-back em GLSL.
- **P3 Particle PointZ na GPU:** pool fixo em SSBO; nascimento por kernel com contador atômico
  amostrando a fonte na GPU; integração (gravidade, drag, damping, turbulência, PartForce) em
  compute; morte por compactação ou lista livre; size/colour over life no mesmo kernel; laço
  "target" sem readback. Equivalência exata sem RNG, estatística com RNG (documentar; modo legado
  na CPU). Aceite: 1 M de partículas a 60 fps na RTX 4060.
- **P4 Memória:** Trail com ring buffer (já tem GpuShiftIn); Feedback por troca de ponteiros;
  Cache / Cache Select / Cache Blend com ring de N frames em VRAM, limite e aviso.
- **P5 Topologia:** Merge (concatenação + offset de índices); Delete/Select (compactação +
  remapeamento); Normal (atomics float ou fixed-point); Facet, Subdivide, Extrude, Revolve, Sweep
  caso a caso, cacheando topologia estática (como `ReplicaTopoCache` do Copy).
- **P6 Linhas:** Resample, Divide, Break, Metrics, Smooth, Thick, Curve, Accumulate; arco por
  scan, resample por busca binária.
- **P7 Análise:** Analyze, Histogram, Sort via P2; resultado de modulador por AsyncReadback com 1
  frame de atraso.
- **P8 GLSL Create / Copy / Advanced PointZ:** compute nativo para o usuário (SSBO, atomics,
  shared), contrato igual ao GLSL POP do TD.
- **P9 Topologia como buffer de GPU:** operadores GPU produzem IBO direto, count assíncrono,
  `glDrawElementsIndirect`.
- **P10 File In / Point File In:** parse em thread, upload por PBO persistente.
- **P11 `docs/pointz-gpu-status.md`:** operador, caminho (GPU/CPU/misto), equivalência
  verificada, custo medido. Atualizar a cada merge.

## 3. Render 3D raster moderno

- **R1 GPU-driven:** `glMultiDrawElementsIndirect` com comandos por compute; frustum culling (Hi-Z
  depois) na GPU; fim do loop por slot na CPU.
- **R2 Bindless:** `ARB_bindless_texture` para material/textura por instância (texMode do
  Instance PointZ).
- **R3 Sombras:** CSM direcional; cubemap/atlas para pontuais; PCSS ou EVSM; fit com bounds GPU
  assíncronos.
- **R4 Post:** GTAO, SSR, TAA com vetores de movimento, DoF, motion blur; compute sobre o HDR do
  Render3DPost.
- **R5 FSR 2/3 (MIT):** render scale; exige MV e jitter; serve também ao RTX.
- **R6 Luzes:** forward+/clustered; luzes de área LTC (MIT).
- **R7 OIT ponderado (McGuire)** para sprites e partículas.
- **R8 Sprites:** billboards em compute ou mesh shader (`GL_NV_mesh_shader` com fallback); formas
  (esfera falsa, disco, textura); tamanho em tela ou mundo.
- **R9 HDR:** RGBA16F até o Output; HDR10/scRGB no projetor; EXR (tinyexr).

## 4. RTX (OptiX) para produção

Hoje no patch de referência em 1080p: estruturas ~2,5 ms, traçado ~3 ms, denoise rápido ~6 ms,
IA ~21 ms.

- **X1 SVGF:** MV do primeiro hit reprojetado; acumulação temporal com rejeição por normal e
  profundidade, variância, à-trous existente (`rtxAtrous`). Alvo < 3 ms.
- **X2 OptiX temporal:** `OPTIX_DENOISER_MODEL_KIND_TEMPORAL`, opcional.
- **X3 OIDN 2 (Apache-2.0):** denoise alternativo.
- **X4 Render scale + upscale:** FSR (R5) ou denoiser. Meta: 60 fps em 1080p.
- **X5 Paridade de material:** clearcoat, sheen, subsurface aproximado, iridescência, specular,
  transmission roughness; mapas normal/roughness/emission; cor e textura por instância.
- **X6 Luzes:** malhas emissivas com NEE; ReSTIR DI; área; importance sampling do HDRI (CDF 2D).
- **X7 IAS incremental:** `OPTIX_BUILD_OPERATION_UPDATE`; buffers da arena com registro
  persistente por fonte.
- **X8 Curvas OptiX** para PointZ de linhas (Line Thick, Trail).
- **X9 Câmera:** DoF e motion blur (motion transforms); paridade com o Camera node.
- **X10 Robustez:** mensagens para driver antigo, VRAM, cena vazia; limite de VRAM com aviso;
  arena e FrameMem limitados; RTXTEST ampliado.
- **X11 DLSS RR:** só via interop GL↔Vulkan ou backend Vulkan (A3). Documentar viabilidade antes.
- **X12 Vulkan ray query** atrás de `RtxBackend`, para AMD/Intel. Depende de A3.

## 5. Arquitetura

- **A1 Desmontar o main.cpp:** TUs reais por domínio (`app/frame`, `ui`, `bodies`, `graph`,
  `selftest`), cabeçalhos explícitos, estado global num objeto `App`. Passos pequenos, suíte verde
  a cada passo. `[estado]` 422 linhas + 51 `.inl` (~58 mil linhas).
- **A2 Grafo de cook explícito:** ordem topológica por mudança de grafo; frame graph de recursos
  de GPU; cook separado de UI; job system (enkiTS zlib ou Taskflow MIT) para nodes sem GL.
- **A3 RHI:** buffers, texturas, pipelines, fences, timestamps; primeiro sobre GL 4.6, depois
  Vulkan 1.3. Referências: NVRHI, Diligent, bgfx, sokol. ADR: migrar tudo ou ilhas Vulkan.
- **A4 Gerenciador de recursos de GPU:** registro central com refcount e vida por frame;
  estatística de VRAM na UI.
- **A5 Shaders em arquivos:** `shaders/*.glsl` com `#include`, embed no build, hot reload em
  debug, validação com glslang; SPIR-V opcional.
- **A6 Pastas:** `src/pop`, `src/render`, `src/rtx`, `src/video`, `src/audio`,
  `src/nodes/<domínio>`, `third_party/`; separar upstream verbatim de turbo;
  `docs/ARCHITECTURE.md`.
- **A7 Testes:** doctest ou Catch2 para unidades puras (math, PopCpu, parser de patch); golden
  images com tolerância para Render 3D, PointZ e RTX.
- **A8 Qualidade:** clang-tidy/clang-format no estilo atual (3 espaços, chave na linha seguinte);
  avisos como erro nos módulos novos; aviso de `WIN32_LEAN_AND_MEAN` em `miniz.c`.

## 6. Upstream e formato

- **C1 Sincronização:** script que lista commits novos do upstream, aplica nos herdados e marca
  conflitos; mapa herdados × turbo; patchcompat com o build do upstream antes de cada merge.
- **C2 `docs/patch-format.md`:** `infinite-patch 1`, chaves desconhecidas preservadas, tipos
  `i/f/b/c/s`, cable, uid, pos, flags, mod-range, caminhos relativos do Collect Media. Teste:
  patch do Turbo abre no upstream sem perda nos nodes comuns.
- **C3 Corpus:** pasta de `.inf` (upstream + usuário) usada pelo patchcompat; ok → crash/hang
  bloqueia merge.
- **C4 Ordinais × modulação:** sliders condicionais do Render 3D deslocam ordinais. Chave por nome
  com fallback e conversão. `[estado]` `mod` já salva `saveKey` (`Patch.cpp:467`) e o loader lê
  por chave; MIDI learn, gestos e Performance ainda salvam ordinais; o upstream tem
  `FixedParamOrdinal`/`RegisterHiddenParam`.

## 7. Vídeo e mídia

- **V1 Decodificação por hardware zero-cópia:** `[estado]` D3D11VA já existe
  (`VideoDecodeWindows.cpp:172`), com download/upload; falta interop (WGL_NV_DX_interop2 ou
  CUDA↔GL).
- **V2 HAP:** `[estado]` decodificação BC na GPU já existe (`src/core/HapDecode.cpp`). Falta medir
  com muitos clipes no VMPC e no Clip Matrix e DXV.
- **V3 Gravação por hardware:** NVENC/AMF via FFmpeg com leitura assíncrona do Output (PBO).
- **V4 Rede e apps:** Spout (medir cópias), NDI assíncrono, avaliar NDI 6 (só full).
- **V5 TODO:** relink de mídia ausente; pastas como mídia; Collect Media com sequências de imagem.

## 8. UX ligada à performance

- **U1 Painel:** ms de CPU/GPU por node, destaque dos caros, VRAM.
- **U2 Previews:** expor taxa, desligar previews 3D, LOD (1 de cada N pontos).
- **U3 Modo show:** sem previews e painéis, cook só do caminho até os Outputs, vsync travado no
  projetor.

## 9. Licenças

MIT/BSD/Apache/zlib: livres nas duas edições (Tracy, enkiTS, Taskflow, FSR, OIDN, tinyexr,
glslang, NVRHI, LTC, Catch2/doctest). Proprietários só como opcionais da full (OptiX, headers
buscados no configure; DLSS/Streamline; NDI). GPL só em `src/gpl`. Cada dependência em
`THIRD_PARTY.md` com versão, licença e edição.
