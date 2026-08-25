# Infinite para Windows x64

Este pacote porta o Infinite para Windows mantendo o editor GLFW/ImGui e os recursos do código-fonte atual. As integrações exclusivas do macOS foram substituídas por APIs equivalentes no Windows.

## Compilação rápida

Requisitos: Windows 10 ou 11 x64, Windows Package Manager (`winget`) e pelo menos 35 GB livres. A primeira compilação do OpenCV pode levar bastante tempo.

1. Execute `install-dependencies.bat` e aceite a janela do UAC. O script relança `cmd.exe` como administrador, mostra o progresso ao vivo, atualiza o `PATH` da sessão e instala Git, CMake, Visual Studio 2022 Build Tools com C++, FFmpeg, vcpkg, bibliotecas C++, Windows ML/DirectML e os modelos U2Net. A janela elevada aguarda uma tecla antes de fechar.
2. Execute `build-windows.bat`.
3. Abra `dist\Infinite-Windows-x64\Infinite.exe` ou use `run-windows.bat`.

O pacote compilado é criado em `dist\Infinite-Windows-x64.zip`.

## Instalação sem compilar

Use esta sequência somente depois de baixar o `Infinite-Windows-x64.zip` de uma GitHub Release:

1. Extraia o ZIP para uma pasta normal.
2. Execute `install-runtime.bat` uma vez. Ele instala FFmpeg e os modelos usados pelo Remove Background.
3. Execute `run-windows.bat`.

Não execute `install-dependencies.bat` para usar o binário pronto. Ele instala compiladores, vcpkg e bibliotecas de desenvolvimento e só é necessário para gerar um novo `Infinite.exe`.

## CV em botões e seletores

A revisão 29 dá entrada CV aos parâmetros discretos dos módulos sem alterar os IDs antigos dos sliders e knobs:

| Controle | Regra de CV |
|---|---|
| Checkbox, toggle e bypass | menor que 0,5 = desligado; 0,5 ou maior = ligado |
| Botão de ação | dispara uma vez na transição de baixo para alto |
| Botão com estado, como Record/Stop | o estado acompanha o sinal baixo/alto |
| Seletor, como Blend ou formato | a faixa 0..1 é dividida igualmente entre todas as opções |

Isso permite, por exemplo, ligar um `UI Button` em modo Toggle a `Output > Record Video`, usar um slider para percorrer os modos do `Blend`, automatizar formatos e acionar Restart, Clear, Reseed ou cues por MIDI, OSC, áudio ou qualquer outro modulador. Botões que abrem diálogos do Windows, como `Choose file`, `Open model`, seleção de pasta, editor de plugin e rotinas de manutenção, continuam deliberadamente sem CV porque exigem interação humana ou alteram o ambiente do aplicativo.

## Pipeline de Text e Output da revisão 31

O `Text` usa GDI+ nativo no Windows, independente do renderer de texto do JUCE.
Uma thread exclusiva recebe snapshots imutáveis e conserva somente o pedido
mais recente. Digitar ou arrastar um parâmetro nunca rasteriza o canvas
1024x1024 na UI e nunca cria uma fila atrasada. O worker produz RGBA com alpha,
antialias, fonte instalada/fallback, fill, outline, alinhamento, wrap e fit; a
thread OpenGL apenas envia o resultado pronto para a textura. O backend, fonte,
tempo e qualquer erro ficam no log com prefixo `TEXT`.

O `Output` usa três Pixel Buffer Objects persistentes e fences OpenGL. A cópia
GPU-CPU é retirada dois frames depois, sem sincronizar a UI com o frame atual.
O encoder continua em worker, mas agora também recebe o áudio temporário fora
da UI. O pacing é CFR: um atraso pontual repete a imagem anterior no encoder
sem duplicar outro buffer RGBA. Início e fim da tomada registram resolução,
FPS, frames enviados/perdidos e espera média/máxima da GPU.

## Recursos e equivalências

| Recurso original | Implementação no Windows |
|---|---|
| Renderização e interface | OpenGL 3, GLEW, GLFW e ImGui |
| Áudio de entrada e saída | JUCE com WASAPI |
| MIDI e clock MIDI | JUCE com Windows MIDI |
| Host de plugins | VST3 via JUCE, com editor, parâmetros, MIDI e estado |
| Syphon In/Out | Spout2 Receiver/Sender |
| Imagem, vídeo e câmera | OpenCV, DirectShow e FFmpeg |
| Remoção de fundo | U2Net ONNX via Windows ML + DirectML/DX12; OpenCV CPU como fallback |
| Modelos 3D | Assimp: OBJ, FBX, glTF/GLB, STL, PLY e outros |
| Texto e contornos | GDI+ assíncrono no Windows |
| OSC e controle local | Winsock2 |
| Gravação de vídeo | OpenCV e mux final pelo FFmpeg |

Na revisão 31, o encoder continua em thread dedicada e a captura GPU também é
assíncrona. A interface agenda PBOs, o worker converte para BGR e grava o vídeo
temporário, e o áudio ao vivo segue pela mesma thread de I/O. Ao parar, o
Infinite drena PBOs e filas, finaliza o vídeo, faz o mux e somente então publica
o arquivo escolhido no diálogo Save As.

Audio Units continuam sendo um formato exclusivo da Apple. No Windows, os mesmos nós de plugin usam VST3. Syphon também é exclusivo do macOS e foi mapeado para Spout2, que é a alternativa interoperável do Windows.

## VST3

O scanner procura por padrão em:

- `%CommonProgramFiles%\VST3`
- `%CommonProgramFiles(x86)%\VST3`
- `%LOCALAPPDATA%\Programs\Common\VST3`
- pastas adicionadas no painel de plugins

Cada bundle é examinado pelo `infinite-vst3-scanner.exe`, um processo auxiliar mínimo instalado ao lado do Infinite. Assim, uma falha do plugin fica isolada e não é confundida com a inicialização do aplicativo inteiro. O backend Windows registra falhas de descoberta e mantém a interface de parâmetros, MIDI, estado e janela nativa do editor.

Na primeira execução da revisão 15 ou mais recente, clique `Rescan plugins`. Ela usa uma blocklist v3 limpa para testar novamente os plugins que revisões anteriores possam ter marcado incorretamente.

## Sessão, cena e biblioteca de mídia

As configurações de entrada e saída de áudio, sample rate, buffer, oversampling, FPS, vsync, grade, zoom, minimapa, painel lateral, largura da lateral, viewport e tema são gravadas no arquivo `.inf`. Elas também são espelhadas em `%LOCALAPPDATA%\Infinite\Infinite.settings`, fazendo uma sessão nova reabrir com o último setup usado. Arraste a borda esquerda do painel `Modules/Samples/Media/Plugins` para redimensioná-lo.

O painel `Media` mostra miniaturas para imagens. A decodificação é feita apenas para as linhas visíveis, com cache limitado e texturas reduzidas, para não carregar uma biblioteca inteira na memória. Vídeos continuam identificados por um cartão `VID`.

Ao abrir o seletor de objetos com o botão direito no fundo do grafo, o campo de busca recebe o foco automaticamente e já aceita digitação.

As categorias e os módulos são ordenados alfabeticamente. Sem texto no campo de busca, cada categoria funciona como um menu sanfona e pode permanecer fechada.

## Spout

No Windows, os módulos aparecem como `syphon/spout in` e `syphon/spout out`, mas mantêm internamente as chaves `Syphon In` e `Syphon Out` para que patches antigos continuem abrindo. Eles publicam e recebem texturas por Spout2.

## Video Player

O módulo `video` tem dois outputs: `video`, para o grafo de imagem, e `audio`, para o grafo DSP. Ligue `audio` a um efeito, mixer ou diretamente a `Audio Out` para ouvir a trilha do arquivo. O playhead visual segue o relógio do áudio, incluindo buffer, sample rate, velocidade e pausa do transporte. A partir da revisão 19 ele usa o mesmo corpo visual de `image source`: preview, monitor, olho e `Choose video...`. O olho revela os controles e o monitor desliga somente o preview do node.

- `Restart` volta ao início do trim. Com `reverse` ativo, volta ao fim do trim.
- `speed` controla a magnitude de 0.05x a 4x e `reverse` controla a direção.
- `trim in` e `trim out` delimitam o trecho reproduzido e salvo no patch.
- Clique na timeline para buscar uma posição.
- Use `Shift+clique` na timeline para criar um dos quatro cues.
- Os botões `Set`, `Go` e `x` gravam, acionam e apagam cada cue.
- Ligue um modulador às entradas `cue 1` a `cue 4`. A passagem do valor de zero para qualquer valor maior que zero aciona o cue uma vez.

O áudio é extraído no carregamento pelo decoder nativo ou pelo `ffmpeg.exe` incluído na distribuição. Um arquivo sem faixa de áudio continua funcionando normalmente como vídeo. No Windows, os pixels são decodificados em uma thread de trabalho e enviados para a textura OpenGL pela thread de render, evitando bloquear a UI em seeks e arquivos pesados.

Os sliders de volume, velocidade e trim usam o mesmo componente compacto dos demais nodes. Clique duas vezes no slider para digitar um valor exato.

## Output, Projection e câmeras

`Projection` usa a resolução da entrada por padrão. Quando uma resolução de saída diferente é escolhida, `preserve source aspect` mantém a proporção e completa o quadro com preto. Desmarque essa opção somente quando o stretch for intencional.

Para definir a resolução final de uma cadeia 2D, insira `fit / resize` entre Image/Video e Output. Escolha largura, altura e `Fit`, `Fill`, `Stretch` ou `Native`. O próprio Output informa a resolução recebida; ele não redimensiona silenciosamente a imagem.

`Open output window` cria primeiro uma janela normal, decorada e redimensionável. Arraste-a para o monitor desejado e pressione F11. No Windows, o fullscreen é uma janela sem borda `HWND_TOPMOST`, não fullscreen exclusivo: ela permanece visível no projetor quando a UI recebe foco, sem roubar o foco. O cursor é ocultado somente no fullscreen; Escape volta ao modo janela. O VSync pertence apenas à janela principal, evitando uma espera extra por output.

Nodes visuais e 3D com preview possuem um monitor ao lado do olho. Desligá-lo evita a renderização do preview dentro do node, incluindo o FBO 3D, mas não interrompe o processamento exigido por conexões, Spout, viewport lateral ou output window. O estado é salvo no patch. Em notebooks híbridos, o executável também solicita a GPU de alto desempenho pelos mecanismos NVIDIA Optimus e AMD PowerXpress.

`Video` abre com seus controles expandidos e mostra `Choose video...`. `Video In` começa desligado. Clique `Refresh cameras`, selecione o dispositivo e depois marque `active`. A captura roda em background para não bloquear a interface.

## Log e recuperação automática

Em `Menu > Safety and diagnostics`, o log e o autosave podem ser ativados separadamente. O log fica em `%LOCALAPPDATA%\Infinite\Infinite.log` e também é anexado por `diagnose-windows.bat`.

Enquanto existe um grafo ativo, o autosave grava uma cópia de recuperação a cada 15, 30, 60, 120 ou 300 segundos. Se o processo não fechar normalmente, o próximo início oferece recuperar essa cópia. A recuperação não sobrescreve o projeto original e deve ser confirmada com `Save` ou `Save As`.

O `Note Sequencer` aceita de 1 a 128 passos. As setas no próprio node navegam por páginas de 16 passos.

## Remoção de fundo

Os modelos esperados ficam em `%LOCALAPPDATA%\Infinite\models`: `u2net.onnx` para qualquer sujeito e `u2net_human_seg.onnx` para pessoas. Os instaladores verificam os MD5 oficiais `60024c5c889badc19c04ad937298a77b` e `c09ddc2e0104f800e3e1bb4652583d1f`.

Na revisão 20, a inferência passou para uma thread que sempre conserva o pedido mais recente. `mask fps` controla a frequência desejada para vídeo; se o modelo for mais lento, frames antigos são descartados em vez de formar uma fila ou congelar a UI.

Na revisão 22, a categoria proprietária `NVIDIA VFX` foi removida. O node existente `Mask > Remove Background` ganhou três backends:

- `Auto GPU (DX12)`: tenta Windows ML + DirectML e troca para OpenCV CPU se a GPU, o driver ou algum operador do modelo não estiver disponível.
- `DirectML only`: exige o caminho GPU e mostra o erro real no corpo do node, útil para diagnóstico.
- `CPU`: força OpenCV DNN e facilita comparar qualidade e estabilidade.

DirectML usa DirectX 12 e funciona com GPUs compatíveis de NVIDIA, AMD e Intel. Em computadores híbridos, o Infinite escolhe para inferência o adaptador com mais memória dedicada, evitando que a RTX fique ociosa enquanto a GPU integrada processa o modelo. As sessões ONNX são persistentes e a execução continua na thread mais recente da R20, sem bloquear UI, áudio ou output.

O instalador baixa `Microsoft.Windows.AI.MachineLearning 2.2.12` do NuGet oficial. O pacote compilado leva `onnxruntime.dll` e `DirectML.dll` ao lado de `Infinite.exe`, portanto não exige conta NVIDIA, NGC, Maxine nem instalação separada no computador que executa a distribuição.

Na revisão 23, o CMake passou a localizar explicitamente o header C++ nativo `onnxruntime_cxx_api.h` dentro da pasta `winml` do pacote NuGet e a publicar essa pasta ao compilador. Isso corrige o erro C1083 da R22: o alvo CMake importado foi encontrado, mas não propagou o include do pacote 2.2.12 neste fluxo fora do MSBuild.

## Feedback

`Feedback` não é um efeito de trails independente. Ele devolve a entrada do frame anterior e existe para tornar ciclos legais no grafo. Por isso, em uma conexão linear, ele parece apenas copiar a imagem. Use `Clear loop memory` para zerar o histórico, ou use `Trails` quando quiser decay, zoom, rotação, drift e blend sem montar o ciclo manualmente.

## Opções manuais

Abra um Developer Command Prompt x64 e use:

```bat
set VCPKG_ROOT=C:\caminho\para\vcpkg
cmake --preset windows-vs2022
cmake --build --preset windows-release
```

Para desativar uma integração:

```bat
cmake --preset windows-vs2022 -DINFINITE_ENABLE_VST3=OFF -DINFINITE_ENABLE_SPOUT=OFF
```

Para compilar deliberadamente sem Windows ML/DirectML e usar somente CPU:

```bat
cmake --preset windows-vs2022 --fresh -DINFINITE_ENABLE_DIRECTML_MATTING=OFF
```

## Solução de problemas

- erro `5007` do Visual Studio Installer: use a revisão 5 deste pacote. O instalador solicita elevação UAC antes de executar opções passivas do Visual Studio.
- erro `Código 1` logo depois da elevação: use a revisão 5. O relançamento acontece por `cmd.exe` e toda a saída fica visível na janela elevada.
- `CMake não encontrado após a instalação`: use a revisão 5. Os scripts recarregam o `PATH` do sistema e do usuário e também procuram o CMake incluído no Visual Studio.
- janela elevada vazia: use a revisão 5. A revisão 4 redirecionava a saída para um arquivo; a revisão 5 mostra o progresso ao vivo.
- erro `this vcpkg instance requires a manifest with a specified baseline`: use a revisão 6. O build preserva o `VCPKG_ROOT` criado pelo instalador, impede que o Developer Command Prompt o substitua pelo vcpkg interno do Visual Studio e usa um baseline fixo.
- erro do Spout2 procurando `debug/bin/Spout.dll`: use a revisão 7. O projeto importa diretamente `Spout_static.lib`, porque o port estático do vcpkg remove a DLL mas deixa uma referência inválida no arquivo CMake exportado.
- muitos erros de `M_PI`, `Accelerate/Accelerate.h`, `gl.h included before glew.h`, `SHGetKnownFolderPath` ou `AudioProcessorListener`: use a revisão 8. Ela adiciona as definições do MSVC, usa FFT do JUCE no Windows e corrige as interfaces específicas de Windows, JUCE e OpenGL.
- o UAC foi cancelado: execute `install-dependencies.bat` novamente e confirme `Sim` na solicitação de administrador.
- elevação automática bloqueada: abra o Terminal ou Prompt de Comando como administrador, entre na pasta do projeto e execute `install-dependencies.bat`.
- `VCPKG_ROOT` ausente: execute `install-dependencies.bat` ou defina a variável antes do CMake.
- câmera indisponível: autorize o aplicativo em Configurações > Privacidade > Câmera.
- sem entrada de áudio: autorize o microfone e selecione um dispositivo WASAPI válido no Infinite.
- Spout sem imagem: confirme que emissor e receptor usam a mesma GPU e o mesmo nome de sender.
- plugin não aparece: adicione sua pasta VST3 no painel e execute uma nova varredura.
- gravação sem áudio: confirme que `ffmpeg.exe` está no `PATH` ou junto de `Infinite.exe`.
- aplicativo fecha imediatamente: execute `diagnose-windows.bat` ao lado de `Infinite.exe` e envie o arquivo `Infinite-diagnostic.log`. O diagnóstico registra o código de saída, dependências PE, GPU e falhas recentes do Windows.
- violação de acesso `0xC0000005` antes da interface ou durante `INFINITE_DSPTEST`: use a revisão 10. Ela inicializa o runtime JUCE antes dos modos headless e do GLFW, desativa o buffer dos logs e registra cada etapa em `%LOCALAPPDATA%\Infinite\Infinite-startup.log`.
- erros `OutputDebugStringA`, `EXCEPTION_POINTERS`, `WINAPI` ou `SetUnhandledExceptionFilter` ao compilar a revisão 10: use a revisão 11. O `main.cpp` agora inclui explicitamente a API Win32 usada pelo diagnóstico.
- muitos erros começando por `Mesh.h`, `Geometry3DNodes.h` ou `Polyline` na revisão 11: use a revisão 12. A instrumentação deixou de incluir `windows.h` em `main.cpp`, evitando a colisão com a função GDI `Polyline`. Esses erros não eram causados pelo JUCE nem pelos nodes 3D.
- executável da revisão 12 encerra com `0xC0000005` e não cria `Infinite-startup.log`: use a revisão 13. O `juce::AudioDeviceManager` do backend Windows deixou de ser um objeto global construído antes de `main()` e agora é criado somente depois da inicialização do JUCE.
- `Audio In` atrasa aproximadamente 5 segundos: use a revisão 14. A fila antiga tinha 262144 frames, equivalentes a 5,46 segundos em 48 kHz; a captura agora descarta o histórico atrasado e entrega o bloco mais recente.
- VST3 aparecem mas falham depois de usar a revisão 12: use a revisão 14 e execute `Rescan plugins`. Os processos de scan da R12 encerravam antes de `main()` e podiam colocar todos os plugins na blocklist. A revisão 14 começa uma blocklist v2 limpa, tolera até 30 segundos no primeiro scan e registra detalhes em `%LOCALAPPDATA%\Infinite\Infinite-vst3.log`.
- muitos VST3 continuam indo para a blocklist na revisão 14: use a revisão 15 e execute `Rescan plugins`. O scan agora roda no executável auxiliar `infinite-vst3-scanner.exe`, sem inicializar janela, grafo, áudio, Spout ou modelos em cada teste. Se o auxiliar estiver ausente, `diagnose-windows.bat` informa isso explicitamente.
- áudio quebra ao usar Airwindows, DecentSampler ou ao parar e iniciar o motor: use a revisão 16. O host prepara cada VST3 com o buffer realmente negociado pelo dispositivo, reinicializa os recursos a cada partida do motor e impede que NaN, infinito ou picos inválidos de um plugin contaminem toda a saída.
- saída de Viewport/Null fica preta em uma janela separada: use a revisão 16. Abra o menu do nó com o botão direito e escolha `Open output window`. No submenu `Output window`, selecione o display e ative `Fullscreen`. F11 alterna fullscreen e Escape volta ao modo janela.
- F11 abre fullscreen no monitor 1 mesmo depois de arrastar a janela: use a revisão 17. Ao entrar em fullscreen, o monitor agora é recalculado pela posição atual da janela.
- a interface pausa ao soltar um cabo sem completar a ligação: use a revisão 17. O menu de busca continua abrindo, mas não instancia mais todos os módulos apenas para ordenar sugestões.
- Video Player sem áudio, trim ou cues: use a revisão 17. Ligue o novo output `audio` a `Audio Out`; os quatro inputs de cue disparam quando o sinal cruza de zero para um valor positivo.
- Projection deforma a imagem, Video In trava a interface, output some atrás da UI ou 30 FPS fica em 28-29: use a revisão 18. Ela preserva aspecto, move a câmera para background, mantém o output flutuante e remove o segundo VSync.
- Infinite.exe abre um terminal, ou falta diagnóstico e recuperação de crash: use a revisão 18. O executável é GUI; o log e o autosave ficam em `%LOCALAPPDATA%\Infinite`.
- Video aparece sem preview/`Choose video...`, previews 3D derrubam o FPS, ou o output abre grande e some ao clicar na UI: use a revisão 19. Ela corrige a classificação mista de vídeo+áudio, adiciona monitor universal, decodifica vídeo fora da UI e usa fullscreen borderless topmost no monitor escolhido.
- Feedback parece não fazer nada, a lista de módulos está desordenada, Video fica largo, falta GPU/VRAM, Remove Background congela a UI ou a lateral não redimensiona: use a revisão 20.
- `Windows ML package was not found`: execute `install-dependencies.bat` da revisão 22 e depois rode `build-windows.bat` novamente.
- `onnxruntime_cxx_api.h: No such file or directory`: confirme que está usando a revisão 23. Não é necessário reinstalar as dependências se a R22 já encontrou Windows ML; reaplique o patch R23 e execute `build-windows.bat`.
- Remove Background muda de tamanho, Text fica vazio, sequenciadores ficam limitados ou nodes do painel lateral parecem sumir: use a revisão 24. Ela fixa a faixa de status, pareia máscara e frame, adiciona edição exata de nota, pagina 128 passos, enquadra novos nodes e corrige a rasterização de texto no Windows.
- controles UI ficam escondidos no olho, Feedback não nasce pelo painel lateral, bypass de Video/Audio Out não silencia, clique duplo em knob não abre valor ou Note Sequencer só alcança C3-C6 com o mouse: use a revisão 25. Ela mantém os controles no corpo do node, cria pelo editor ativo, corrige mute de fontes e terminais, restaura a entrada numérica dos knobs e amplia as barras para MIDI 0-127.
- Feedback/Output ainda parecem não nascer pelo painel lateral, a fonte continua Segoe UI ou a gravação perde FPS ao iniciar: use a revisão 26. Ela cria e enquadra o node no mesmo ciclo, inclui IBM Plex no executável distribuído e move aquecimento, conversão e escrita do encoder para uma fila assíncrona.
- Momentary funciona como Toggle, Pulse não aparece na curva, Feedback/Output não nascem em nenhum menu ou parte da UI ainda aparece em lowercase: use a revisão 27. Ela separa os IDs de categoria e módulo, corrige o ciclo físico do Button, prolonga o Pulse e aplica IBM Plex uppercase a toda a camada ImGui.
- Momentary fica ativo por apenas um frame, o ícone de bypass parece fora do centro ou Text produz preview transparente: use a revisão 28. Ela captura o mouse independentemente do Active ID do Node Editor, troca o ícone por BP centralizado e valida os pixels alpha da rasterização de texto com fallback Segoe UI.
- botões, checkboxes, bypass ou seletores não aceitam CV, `Record Video` não responde a um Toggle, ou `Blend` não responde a um slider: use a revisão 29. Os controles discretos agora recebem pins próprios e preservam a numeração das modulações contínuas existentes.
- Text continua transparente ou derruba o FPS ao mover parâmetros, ou inserir Output bloqueia drag de nodes e cabos: use a revisão 30. Ela usa o caminho nativo DirectWrite, faz upload direto do bitmap com atualização limitada a 30 Hz e corrige a pilha de estado do ImGui no Output.
- Text ainda fica transparente ou a UI cai ao digitar, ou a gravação do Output derruba frames no início: use a revisão 31. Text usa um worker GDI+ latest-only e Output usa captura OpenGL assíncrona com PBO triplo, pacing CFR e áudio fora da UI.
- erro MSVC C2027/C2338 em `TextWindowsRasterState` ao compilar a R31: aplique a revisão 31A. Ela move o construtor de `TextNode` para depois da definição completa do estado privado do worker.
- `DirectML only` falha: atualize o driver da GPU e confirme suporte a DirectX 12. Volte para `Auto GPU (DX12)` para manter o fallback CPU enquanto investiga.
- o status mostra `OpenCV CPU fallback`: o Infinite tentou DirectML e registrou a causa em `%LOCALAPPDATA%\Infinite\Infinite.log`. As DLLs `onnxruntime.dll` e `DirectML.dll` devem estar ao lado de `Infinite.exe`.

## Licenças

O código original é MIT. O build Windows usa JUCE 8, disponível sob licença comercial ou AGPLv3. A distribuição do executável deve respeitar a licença aplicável do JUCE e as licenças do VST3 SDK, FFmpeg, OpenCV, Spout2, Assimp, Windows ML/ONNX Runtime, DirectML e demais dependências. Consulte `LICENSE` antes de redistribuir.
