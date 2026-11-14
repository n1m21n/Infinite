# Исследование: падения FPS до 4 при малой загрузке CPU (Windows, GTX 1050 Ti)

Только чтение кода и сведений о машине; исходники не менялись, приложение из исходников не собиралось, ничего не измерялось. Всё, что не проверено в коде или на машине, помечено «не проверено».

## 1. Машина владельца (поправка: это ПК с Windows, не Mac)

Просадка до 4 FPS была на ПК под Windows (официальная сборка). Сведения о Mac из первой версии отчёта к делу не относятся и удалены.
- GPU: NVIDIA GeForce GTX 1050 Ti, 4 ГБ GDDR5, Pascal (2016; ~1,9 TFLOPS FP32, шина 128 бит, ~112 ГБ/с). Есть CUDA.
- Модель CPU, объём ОЗУ, разрешение и масштаб экрана, настольный это ПК или ноутбук: неизвестно. Нужно узнать у владельца.
- Видеопамять 4 ГБ и 128-битная шина — главное ограничение: слабое место GTX 1050 Ti — пропускная способность памяти и VRAM, а не арифметика шейдеров.

## 2. Конвейер кадра

Кадр собран в `DrawFramePump` ([StageFramePump.cpp](src/app/frame/StageFramePump.cpp)): `glfwPollEvents` → ImGui `NewFrame` (строка 489) → стадии `Stage*.cpp` → `CookFrame.cpp` → `StagePresent.cpp` (swap, ограничитель кадров, `gLastFrameMs`).

**Как готовятся ноды.** Кэш по номеру кадра, а не по dirty-флагу.
- `CookIfNeeded(frameId)` пропускает повторный вызов в том же кадре ([FilterNode.cpp:258](src/nodes/FilterNode.cpp:258), [Geometry3DNodes.cpp:2040](src/nodes/Geometry3DNodes.cpp:2040)). Это защита от двойной готовки, а не пропуск неизменившегося.
- Готовка идёт от стоков: `Output` тянет вход через `ImageCable::Pull()` ([ImageCable.h:38](src/core/ImageCable.h:38)), каждый узел вызывает `CookIfNeeded` у входов. Ноды без пути к Output обычным циклом не готовятся.
- Исключения, которые готовятся всегда ([CookFrame.cpp:671-730](src/app/frame/CookFrame.cpp:671)): Palette, FieldPixel/FieldPrimitive/FieldElement, Sketch3D, Vmpc, Scene3D при открытом 3D View, все смонтированные дети FieldGraph (даже скрытые). Их превью и мини-вьюпорты живут независимо от Output.
- Выключенные (`bypassed` / `blockOff`) ноды не готовятся ([ImageCable.h:34](src/core/ImageCable.h:34)).
- Dirty-механизм только зачаточный. `INode::OutputRevision` по умолчанию возвращает «всегда новое» ([INode.h:160](src/core/INode.h:160)), и ноды, которые его переопределили, дают downstream-кэшу `FilterNode` пропустить перерисовку. Сколько нод переопределяют ревизию — не проверено. Разумно считать, что большинство перерисовывается каждый кадр.
- Видимость на канвасе не учитывается: нет явного culling по `ImGui`-области. Не проверено, прячет ли `imgui-node-editor` готовку у нод вне экрана. По коду готовка от этого не зависит.

**Что считается каждый кадр.** Весь граф от Output: фильтры, 3D, симуляции, видео. Параметры модуляции и жесты применяются в том же кадре.

## 3. Разрешение рендера

- Размер текстуры ноды **не зависит от Retina/масштаба окна**. Он задаётся параметрами и входом: Render3D по умолчанию 1024×1024 (`width`/`height`, [Geometry3DNodes.h:688](src/nodes/Geometry3DNodes.h:688)), потолок стороны 8192 (`GLUtil::TargetSide`, [GLUtil.h:31](src/core/GLUtil.h:31)); `FilterNode` берёт размер входа ([FilterNode.cpp:275](src/nodes/FilterNode.cpp:275)).
- Превью на ноде рисуется как `kPreviewSize = 190` pt ([AppShared.h:16](src/app/AppShared.h:16)), то есть 190 pt × 2 = 380 px на Retina. Текстура при этом полного размера (например 1024²): ImGui лишь масштабирует её при показе. Отдельного half-res превью нет — не нашёл.
- Рабочий формат изображений **`GL_RGBA16F`** (8 байт на пиксель) ([GLUtil.cpp:292](src/core/GLUtil.cpp:292)). Переменная окружения `INFINITE_IMAGE_RGBA8` переключает на 8-битный `RGBA8`: вдвое меньше трафика памяти и пропускной способности. Это самый дешёвый рычаг без правки кода.
- Окно создаётся через `glfwCreateWindow(1600, 1000)` ([Startup.cpp:1178](src/app/Startup.cpp:1178)) с профилем Core 3.3; `NSHighResolutionCapable` включён ([Info.plist.in:21](cmake/Info.plist.in:21)), значит UI рисуется в нативном Retina 3072×1920. Масштаб UI считает `ApplyUiScale` ([Startup.cpp:49](src/app/Startup.cpp:49)).
- Настройка качества/разрешения: в Settings → General есть раздел «Performance & Rendering» ([SettingsWindow.cpp:876](src/app/panels/SettingsWindow.cpp:876)), но там только Target FPS (0/30/60/120), Vsync и выбор GPU (на macOS недоступен). Глобального масштаба рендера нет.
- **Vsync включён по умолчанию** (`gVsync = true`, [AppState.cpp:317](src/app/AppState.cpp:317)). Ограничитель кадров ([StagePresent.cpp:72](src/app/frame/StagePresent.cpp:72)) работает только при выключенном Vsync и не при активной проекторе/экспорте.
- Поведение в фоне: ни `glfwWaitEvents`, ни снижения частоты при потере фокуса в рабочем пути нет (грепом не нашёл). Окно в фоне рисуется на полной нагрузке — не проверено на практике.

## 4. Самые дорогие места по GPU

**Многопроходные эффекты и 3D** (каждый кадр, если на пути к Output):
- `Render3DNode`: MSAA по умолчанию индекс 2 → 4× (`samples = 2`, [Geometry3DNodes.h:759](src/nodes/Geometry3DNodes.h:759), renderbuffer [Geometry3DNodes.cpp:1834](src/nodes/Geometry3DNodes.cpp:1834)), PBR (Cook-Torrance/GGX), тени, прозрачность, motion blur (второй проход скоростей, [строка 2105](src/nodes/Geometry3DNodes.cpp:2105)), AO и DoF в постобработке ([Render3DPost.cpp:325-413](src/nodes/Render3DPost.cpp:325)). Чем больше Render3D в патче, тем быстрее растёт стоимость; на 1024² с 4× MSAA и float-буфером это существенно для 5300M и тем более для UHD 630.
- Блюр/блум и подобные в `FilterNode` с промежуточным scratch-буфером; цепочки таких фильтров на RGBA16F. Feedback ([FeedbackNodes.cpp](src/nodes/FeedbackNodes.cpp)), Fluid ([FluidNode.cpp](src/nodes/FluidNode.cpp)), reaction-diffusion/симуляции ([SimulationNodes.cpp](src/nodes/SimulationNodes.cpp)) — несколько проходов за кадр и накопление состояния.
- Path trace ([Render3DPathTrace.cpp](src/nodes/Render3DPathTrace.cpp)): `glFinish` на строке 1787 и до 4096 сэмплов (`PathTraceSampleCount`); при неподвижном виде заменяет растр.
- Видео-декод и камера: `VideoInNode`, `VideoSourceNode` — загрузка кадра на GPU каждый кадр; детали декода не изучал — не проверено.
- Удаление фона (ML): `RemoveBgNode` — синхронный `glReadPixels` всего кадра, потом инференс ([RemoveBgNode.cpp:143](src/nodes/RemoveBgNode.cpp:143)); по виду кода запрос уходит в фоновую очередь (`FrameRequest`), но чтение блокирует. Как часто — зависит от узла, не проверено.

**Синхронные чтения GPU→CPU** (все без PBO, если не указано иное):

| Место | Что | Каждый кадр? | PBO |
|---|---|---|---|
| [AnalyzeNodes.cpp:282](src/nodes/AnalyzeNodes.cpp:282) | Image Analyze, `size²`×RGBA8 | да, при подключенном узле (частоту не проверял) | нет |
| [PaletteNode.cpp:229](src/nodes/PaletteNode.cpp:229) | палитра | да (Palette готовится всегда, CookFrame:671) | нет |
| [PredictiveColoringNode.cpp:361](src/nodes/PredictiveColoringNode.cpp:361) | цветовой анализ | по готовке | нет |
| [GenerativeNodes.cpp:369](src/nodes/GenerativeNodes.cpp:369) | `glGetTexImage` для ImageToPoints | по пересборке | нет |
| [GeometryFeedbackNode.cpp:284-286](src/nodes/GeometryFeedbackNode.cpp:284) | `glGetTexImage` позиции+цвета, float | по готовке (не проверено) | нет |
| [SimulationNodes.cpp:1275-1282](src/nodes/SimulationNodes.cpp:1275) | полный readback состояния частиц ×4 | вероятно по запросу (не проверено) | нет |
| [SimulationNodes.cpp:1133-1182](src/nodes/SimulationNodes.cpp:1133) | границы частиц (1×1) | каждый кадр | **да**, с `glClientWaitSync(…,0,0)` — не блокирует |
| [WaveTerrainNode.cpp:808](src/nodes/WaveTerrainNode.cpp:808) | превью/FFT-террейн | ограничено ~15 Гц ([строка 744](src/nodes/WaveTerrainNode.cpp:744)) | нет |
| [ImageSpectralSynthNode.cpp:993](src/nodes/ImageSpectralSynthNode.cpp:993) | спектр | по готовке (не проверено) | нет |
| [TrackNodeBase.cpp:313](src/nodes/TrackNodeBase.cpp:313) | трекинг | по готовке (не проверено) | нет |
| [FluidNode.cpp:672](src/nodes/FluidNode.cpp:672) | readback float | не проверено | нет |
| [OutputNode.cpp:724-870](src/nodes/OutputNode.cpp:724) | запись видео | только при записи | **да**, тройная буферизация |
| [NdiOutNode.cpp:141](src/nodes/NdiOutNode.cpp:141) | NDI | только при выводе | да |
| [Render3DPathTrace.cpp:1787,1842,1870](src/nodes/Render3DPathTrace.cpp:1787) | `glFinish` + readback | при path trace | нет |
| [ExportImage.cpp](src/app/graph/ExportImage.cpp), [Rpc.cpp:1048](src/app/graph/Rpc.cpp:1048) | экспорт/скриншот | по запросу | нет |

Каждое `glReadPixels` в память CPU синхронно ждёт, пока драйвер дорисует всё накопленное в очереди (особенно тяжело на macOS), то есть **останавливает конвейер CPU↔GPU**: CPU стоит, GPU работает, потом наоборот. На Windows/NVIDIA драйвер буферизует до нескольких кадров, и такое чтение сбрасывает эту очередь; в диспетчере задач это выглядит как «GPU 3D высокая, CPU низкий».

## 5. Диагностика, которая уже есть

- **FPS и мс в верхней строке** ([StageMenuBar.cpp:1122](src/app/frame/StageMenuBar.cpp:1122)) плюс CPU-метка; `gLastFrameMs` меряется после swap, то есть включает ожидание GPU ([StagePresent.cpp:106](src/app/frame/StagePresent.cpp:106)).
- **Performance Panel** (`gPerfPanelOpen`, иконка «Performance mode» на левой рейке, [StageMenuBar.cpp:38](src/app/frame/StageMenuBar.cpp:38); код [PerfPanel.cpp](src/app/panels/PerfPanel.cpp)). Что именно он показывает по нодам — подробно не читал, не проверено.
- `CookProbe` ([CookProbe.h](src/core/CookProbe.h)) обёртывает каждую готовку для статистики по нодам; `GpuTimerRing` ([BenchReport.cpp:30-200](src/core/BenchReport.cpp:30)) — кольцо `GL_TIME_ELAPSED`.
- **Важная оговорка:** на macOS GPU-таймеры сами блокируют CPU, а в части режимов читают ноль ([BenchReport.cpp:35](src/core/BenchReport.cpp:35), [BenchFrameTail.h:9](src/core/BenchFrameTail.h:9)). Отключаются `INFINITE_BENCH_GPUTIMERS=0`. Поэтому «GPU мс» из бенчмарка на Mac искажает картину; надёжнее брать ступенчатое отключение нод.
- Бенч-режимы по env: `INFINITE_BENCH_B2/B3/B4/B5STAGES/B6/B8/B9…` ([StageFramePump.cpp:43-60](src/app/frame/StageFramePump.cpp:43)), `INFINITE_BENCH_B2GPUNODES` (GPU по типу ноды), `INFINITE_EXITAFTER=N`, `INFINITE_NO_SINGLE_INSTANCE`.
- Документы: [docs/quality/README.md](docs/quality/README.md) (в нём упомянут бенчмарк на «базовом M2 8 ГБ»), [BENCHMARK_REPORT.md](docs/quality/BENCHMARK_REPORT.md), ссылка на `docs/plans/perf/README.md` в [BenchFrameTail.h](src/core/BenchFrameTail.h) — такого каталога в репозитории нет. **Целевым железом проекта была Apple Silicon; Intel + AMD в бенчмарках не фигурирует** (по формулировке README; сами результаты не разбирал).

## 6. Почему CPU простаивает

- Весь кадр — на одном главном потоке: события, ImGui, готовка графа, GL-вызовы, swap. Единственный GL-контекст у главного потока. Аудио живёт в своих потоках, запись видео частично в фоне (`StopRecordingAsync` с отдельным потоком финализации).
- Простой возникает в трёх местах:
  1. Vsync: `glfwSwapBuffers` ждёт дисплей. На 4 FPS это не причина, а следствие.
  2. Драйвер OpenGL на macOS буферизует команды и блокирует вызов, когда GPU отстаёт. Блокировка проявляется в `glReadPixels`/`glGetTexImage` и в самом swap.
  3. Синхронные чтения из §4: CPU ждёт готовности кадра.
- Если GPU занята на 100%, а CPU ~0% — классика «ограничение по GPU» (fill-rate / пропускная способность памяти) или цепочка стоп-стартов.
- Что можно вынести на CPU-потоки: анализ пикселей (Analyze/Palette/Predictive) после асинхронного PBO-чтения; декод видео (уже частично нативный через `Platform`); построение геометрии и физика частиц для CPU-режима; ML-инференс RemoveBg (уже вынесен в запрос). Всё, что трогает GL, остаётся на главном потоке без отдельного контекста.

## 7. Платформа

- На macOS OpenGL заморожен на 4.1 и устарел, работает поверх Metal. Проект запрашивает Core **3.3** ([Startup.cpp:1140](src/app/Startup.cpp:1140)): никаких compute-шейдеров, `glMemoryBarrier`, SSBO, `GL_ARB_buffer_storage` и т. п. (в 4.1 их нет), то есть все «симуляции» сделаны фрагментными шейдерами и ping-pong текстурами — дороже compute.
- В коде и документации проекта о Metal-бэкенде **ничего не нашёл** (по `Metal` в ARCHITECTURE/DECISIONS/STACK — пусто). Кроме Metal-таймеров, нативный Metal используется лишь косвенно (AVFoundation, `Platform.mm`). Переписывание на Metal/bgfx/Vulkan(MoltenVK) — работа на месяцы (XL), для форка не оправдана.
- Для Windows/NVIDIA это не ограничение (драйвер даёт GL 4.6); для форка важно только на macOS. Общий вывод: снижать пиксели и убирать синхронные чтения.

## Вероятные причины 4 FPS (по убыванию вероятности)

1. **Слишком много/слишком тяжёлых пикселей в графе** для 128-битной шины 1050 Ti: ноды 1024² и больше в RGBA16F, 4× MSAA и AO/DoF в Render3D, цепочки блюр/блум. Проверка: `INFINITE_IMAGE_RGBA8=1`, Render3D → samples = 1× (MSAA off), размер 512²; смотреть FPS. Если FPS подскочил в разы — это оно.
2. **Рабочий формат RGBA16F** удваивает трафик памяти на слабой шине (на Pascal 16F-блендинг и фильтрация поддерживаются, но пропускной способности мало). Код: [GLUtil.cpp:292](src/core/GLUtil.cpp:292). Проверка: тот же `INFINITE_IMAGE_RGBA8`.
3. **Синхронные readback каждый кадр** (Image Analyze, Palette, Predictive, ImageToPoints, GeometryFeedback). Код — таблица §4. Проверка: удалить/выключить эти ноды по одной (`bypassed`), смотреть FPS и CPU-метку; идеальный признак — резкий скачок после выключения одного узла.
4. **Ноды, готовящиеся «вхолостую»**: Palette, Field*, Sketch3D, дети FieldGraph, Scene3D при открытом 3D View готовятся независимо от Output ([CookFrame.cpp:671-730](src/app/frame/CookFrame.cpp:671)). Проверка: закрыть 3D View и отсоединить/выключить не нужные field-ноды.
5. **Симуляции (Fluid, reaction-diffusion, particles, Feedback)** с большим числом шагов за кадр и float-текстурами. Проверка: bypass по одной.
6. **Path trace / motion blur / тени** включены в Render3D. Проверка: `PathTraceActive`/motion — выключить в параметрах ноды.
7. **Видео-декод, камера, ML (RemoveBg)** в патче. Проверка: заменить источник статичной картинкой.
8. **Приложение на встроенной графике** (если это ноутбук с Optimus). Экспорта `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` в коде нет (поиск по `src/`, `CMakeLists.txt` пуст), выбор GPU идёт через реестр `UserGpuPreferences` (см. §8). Проверка: строка «Graphics» в Настройках. Для настольного ПК с одной картой неактуально.

## 8. Windows: что в коде (`src/platform/win/*`)

- Файлы: [PlatformWin.cpp](src/platform/win/PlatformWin.cpp), [MediaWin.cpp](src/platform/win/MediaWin.cpp), [SpoutGLBridge.cpp](src/platform/win/SpoutGLBridge.cpp), `Infinite.manifest`.
- **Контекст OpenGL:** тот же код окна: запрос Core **3.3**, forward-compat ([Startup.cpp:1134-1142](src/app/Startup.cpp:1134)). На NVIDIA драйвер даёт 4.6 (комментарий в коде: десктопные драйверы «over-grant»). Compute-шейдеры в принципе доступны, но код их не использует.
- **Vsync:** то же `glfwSwapInterval` через `ApplyCanvasSwapInterval` ([Core.cpp:624](src/app/graph/Core.cpp:624)); `wglSwapInterval` напрямую не вызывается. Включён по умолчанию. Если в панели NVIDIA vsync заставлен «вкл.», настройка приложения его не отключит — не проверено.
- **Выбор GPU:** `GpuPreferenceSupported()` на Windows возвращает `true`; пишет `GpuPreference=` в `HKCU\...\DirectX\UserGpuPreferences` для exe, вступает в силу при следующем запуске ([PlatformWin.cpp:1662-1676](src/platform/win/PlatformWin.cpp:1662)). `NvOptimusEnablement` и `AmdPowerXpressRequestHighPerformance` **не экспортируются**: на ноутбуках с Optimus по умолчанию может запуститься встроенная графика.
- **DPI:** в манифесте строк с DPI нет, в `src/` и `cmake/` `PerMonitorV2` не нашёл. Режим DPI задаёт GLFW (в 3.3+ вызывает DPI-awareness сам — не проверено в используемой версии). `GLFW_SCALE_TO_MONITOR` включён ([Startup.cpp:1155](src/app/Startup.cpp:1155)); при 200% окно имеет вдвое больше физических пикселей, UI рисуется нативно. Для 4K-монитора на 1050 Ti это заметная нагрузка только на UI.
- **Видео-декод:** Media Foundation Source Reader, `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS` включён «по возможности» ([MediaWin.cpp:271-298](src/platform/win/MediaWin.cpp:271)); декод в отдельном потоке (строка ~811). То есть аппаратный декод (на 1050 Ti — NVDEC) вероятен, но реально ли он сработал на данном файле — не проверено. Кадр затем заливается в GL-текстуру на главном потоке.
- **Удаление фона (ML):** в `RemoveBgNode` комментарий «inference happens on CPU pixels» ([RemoveBgNode.cpp:97](src/nodes/RemoveBgNode.cpp:97)) верен лишь в смысле формата данных. На Windows `Platform::SubjectMask` использует **ONNX Runtime + DirectML на GPU** с откатом на CPU ([PlatformWin.cpp:898-932](src/platform/win/PlatformWin.cpp:898), [CMakeLists.txt:155-176](CMakeLists.txt:155)). Значит, сегментация конкурирует с OpenGL за ту же 1050 Ti и за VRAM. Запрос асинхронный, с интервалом `refreshFrames` ([RemoveBgNode.cpp:242](src/nodes/RemoveBgNode.cpp:242)), но чтение кадра `glReadPixels` синхронное.
- **Spout:** `SpoutGLBridge` (OpenGL-интероп через общую текстуру) — стоимость при подключённых Spout-нодах не изучал, не проверено.

## 9. Пересчёт гипотез под GTX 1050 Ti

- **Слабо:** пропускная способность памяти (RGBA16F, MSAA 4× с float-буфером, цепочки блюра, много проходов); fill-rate для 1080p/4K; 4 ГБ VRAM; нет тензорных/RT-ядер (не нужны).
- **VRAM:** один RGBA16F 1024² — 8 МБ, 2048² — 32 МБ, 4096² — 128 МБ; MSAA 4× добавляет цвет+глубину ×4 на каждый Render3D; `Filter`/scratch/feedback/симуляции держат свои FBO. Патч из десятков нод 2048²+ легко приближается к 4 ГБ. При переполнении драйвер WDDM выгружает ресурсы в системную память через PCIe, а FPS падает до единиц при почти пустом CPU — **очень похоже на описанное**. В коде есть учёт `Bench::GpuMem` по категориям ([BenchReport.h:324-362](src/core/BenchReport.h:324), бенч B9), но ограничения по VRAM и защиты от переполнения не нашёл.
- **Не так страшно:** арифметика шейдеров на простых фильтрах; синхронные чтения мелких картинок (size² в Analyze/Palette) — на NVIDIA они дешевле, чем на macOS OpenGL-over-Metal, но всё же ломают конвейер; сам OpenGL-драйвер NVIDIA для 3.3 зрелый.
- **Новая гипотеза 0 (самая правдоподобная):** переполнение 4 ГБ VRAM → выгрузка в RAM. Проверка: Диспетчер задач → GPU → «Выделенная память GPU» близка к 4,0/4,0 ГБ, «Общая память GPU» растёт.
- DirectML-сегментация (RemoveBg) на той же карте добавляет и VRAM, и нагрузку.

## 10. CUDA: честно

- В коде CUDA/NVENC/NVDEC **не используется** (поиск по `src/` и `CMakeLists.txt` пуст). Единственный GPU-вычислитель вне OpenGL — DirectML для ML.
- Шейдеры OpenGL (фильтры, 3D, симуляции) CUDA быстрее не сделает: они делают растровую работу, упирающуюся в память, и перенос на CUDA даст то же железо тех же ТFLOPS плюс стоимость интеропа (`cudaGraphicsGLRegisterImage`, синхронизация) и привязку к NVIDIA. Для форка, где важна переносимость и паритет, нереалистично.
- Где CUDA могла бы помочь теоретически: ML-инференс (вместо DirectML — ORT с CUDA EP; но 1050 Ti старая, выигрыш невелик), анализ пикселей без чтения на CPU (редукция на GPU), декод видео через NVDEC. Всё это заменимо без CUDA: асинхронные PBO и GPU-редукции на GLSL. Рекомендация: CUDA не трогать.

## Как измерить на Windows (для владельца)

1. **Диспетчер задач → Производительность → GPU:** смотреть график «3D» (если ~100% — упор в GPU), «Copy» (высокий — трафик в/из RAM, признак выгрузки VRAM), «Video Decode» (>0 — аппаратный декод работает), «Выделенная память GPU» (упор в 4 ГБ — гипотеза 0) и «Общая память GPU». На вкладке «Процессы» включить столбец «Ядро GPU»/«Память GPU» для процесса Infinite.
2. **Какая карта:** Настройки Windows → Система → Дисплей → Графика → Infinite → «Высокая производительность»; в самом приложении строка «Graphics» в Settings → General; в панели NVIDIA: «Управление параметрами 3D» → вертикальная синхронизация и «Предпочитаемый GPU» для программы.
3. **Оверлей:** NVIDIA App/GeForce Experience → оверлей производительности (FPS, загрузка GPU, VRAM); либо MSI Afterburner/RTSS.
4. **Бисекция по патчу:** bypass нод по одной (самые тяжёлые 3D/симуляции первыми), закрыть 3D View; записывать FPS и VRAM после каждого шага. `set INFINITE_IMAGE_RGBA8=1` перед запуском из cmd; Render3D samples=1×, размер 512².
5. **PresentMon** (Intel, свободный): `PresentMon.exe --process_name Infinite.exe --output_file pm.csv` — покажет время кадра, `msBetweenPresents`, `msGPUBusy`/ожидание; **Nsight Graphics** (захват кадра, список draw-вызовов и время по каждому); Nsight Systems для главного потока и ожиданий.
6. **В сборке из исходников** (Windows): релизная конфигурация, `INFINITE_BENCH_B6`/`B9MEMORY`, смотреть B9 (VRAM по категориям). GPU-таймеры на NVIDIA работают штатно (оговорка про Apple GL к Windows не относится).
7. **Что прислать:** файл патча (расширение и путь в меню File), скриншот графа, список параметров Render3D (width/height/samples/AO/DoF/motion/path trace), открыт ли 3D View, подключены ли Spout/видео/камера/RemoveBg, модель CPU, ОЗУ, разрешение и масштаб экрана Windows, версия драйвера NVIDIA, скриншоты Диспетчера задач (вкладка GPU) в момент просадки, строка «Graphics», `pm.csv` от PresentMon (10–20 секунд).

## Как измерить (общее и macOS)

**В официальной сборке (без исходников):**
1. Запомнить какие именно ноды и сколько их в проблемном патче (скриншот графа), размер Render3D (width/height), samples.
2. Settings → General → «Graphics»: записать `GL_RENDERER` (Intel или AMD).
3. Включить Performance Panel (иконка на рейке) и смотреть время по нодам; записать, какая нода сверху.
4. Метод бисекции: последовательно bypass-ить ноды (самые тяжёлые 3D/симуляции первыми), фиксируя FPS после каждой. Отдельно закрыть 3D View.
5. Запуск из терминала с переменными, не меняя сборку: `INFINITE_IMAGE_RGBA8=1 /Applications/Infinite.app/Contents/MacOS/<бинарник>`; затем с отключённым Vsync (Settings → Vsync off, Target FPS Unlimited) — чтобы увидеть чистое время кадра, а не кратное 60.
6. Activity Monitor → Window → GPU History (⌘4) и вкладка GPU у процесса: подтверждает, что GPU на 100%.
7. Для скриншота потока: Instruments → «Metal System Trace» или «Time Profiler» (подсветит `glReadPixels`, `glFinish`, `glClientWaitSync` в стеках главного потока); `sample <pid> 5` из терминала даёт то же без Instruments.

**В сборке из исходников:**
1. Релизная конфигурация (без Debug/асан), иначе CPU-цифры бессмысленны.
2. `INFINITE_BENCH_B6` / `B2GPUNODES` для стадий и GPU по типам нод; помнить оговорку про таймеры на Apple GL (§5) и сравнить с `INFINITE_BENCH_GPUTIMERS=0`.
3. Временные счётчики вокруг каждого `glReadPixels` из таблицы §4 (в форке, не в этом отчёте).
4. (macOS) Xcode → Product → Profile → Metal System Trace / GPU Frame Capture.

**Какие сведения о патче нужны:** какой именно патч (файл `.inf` или аналог), список нод и их параметры (Render3D width/height/samples/AO/DoF/path trace), открыт ли 3D View, есть ли видео/камера/RemoveBg, значения FPS до/после bypass, строка «Graphics» и подключён ли внешний монитор.

## Что оптимизировать

**Быстрые меры без правки кода** (всё — настройки или env):
- Эффект высокий, S, риск нулевой: `INFINITE_IMAGE_RGBA8=1` при запуске (8-бит вместо 16F; пропадёт HDR-диапазон).
- Высокий, S: в Render3D выставить samples = 1× и размер 512² (или как нужно в итоге), выключить AO/DoF/motion blur/path trace на время правки.
- Высокий, S: закрыть 3D View, bypass неиспользуемых нод (Palette, Field*, Sketch3D).
- Средний, S: масштаб UI/окно меньше (Retina 3072×1920 → меньшее окно снижает пиксели UI).
- Средний, S: проверить, что рисует дискретная AMD, а не Intel (см. «Graphics»); подключить питание, отключить режим экономии энергии.
- Низкий, S: Vsync off + Target FPS 30 для стабильного кадра при нагрузке.

**С правкой кода (форк):**

| Мера | Эффект | Объём | Риск |
|---|---|---|---|
| Глобальный «масштаб рендера» (множитель размеров всех FBO/превью, ½ по умолчанию для превью) | высокий | M | средний: меняет размеры текстур, ломает зависимые ноды/сериализацию |
| Отдельное меньшее превью-разрешение на ноде (рендер превью с пониженным размером) | высокий | M | средний |
| Заменить синхронные `glReadPixels` в Analyze/Palette/Predictive/ImageToPoints/GeometryFeedback на PBO с задержкой в 1–2 кадра (по образцу [SimulationNodes.cpp:1133](src/nodes/SimulationNodes.cpp:1133) и OutputNode) | высокий | M | низкий–средний: данные на кадр позже |
| Реальные dirty-флаги: `OutputRevision` для всех нод + пропуск перерисовки, если вход не менялся | высокий (статичные графы) | L | средний: тонкая корректность |
| Пропуск готовки нод, не видимых и не ведущих к Output (кроме нужных исключений) | средний | M | средний |
| Снижение частоты Analyze/Palette (например, 15–30 Гц) | средний | S | низкий |
| Дефолт Render3D: MSAA 1×–2× в превью, полный MSAA только при экспорте | средний | S | низкий |
| Троттлинг рендера в фоне (окно без фокуса) и при «простое» (нет изменений) | средний | S–M | низкий |
| Вынос анализа пикселей и подготовки геометрии в рабочий поток после PBO-чтения | средний | M | средний |
| Профайлер-обёртка вокруг каждого readback (счётчик мс в Performance Panel) | диагностика | S | низкий |
| Контроль бюджета VRAM: показ расхода в UI, предупреждение, авто-снижение размеров/MSAA при приближении к 4 ГБ | высокий (на 1050 Ti) | M | низкий–средний |
| Экспорт `NvOptimusEnablement` и `AmdPowerXpressRequestHighPerformance` (Windows, ноутбуки) | высокий для Optimus-ноутбуков, иначе нулевой | S | низкий; только Windows |
| MSAA/AO/DoF в живом режиме отключаемые по качеству, полный вариант при экспорте | средний | S–M | низкий |
| Windows: проверить, что MF-декод действительно аппаратный, и логировать это | низкий–средний | S | низкий |
| Metal-бэкенд / переход на другой API (CUDA также не нужна, см. §10) | очень высокий потенциал | XL | высокий; не нужен для форка |

Паритет Windows теперь важен: все меры в коде делаются платформенно-нейтрально (GL-код общий); правки в `src/platform/win/*` (Optimus, MF) нужны отдельно, на macOS и Linux они не требуются.

## Открытые вопросы

- Какой патч и какие ноды в нём (без этого причины 1–7 — гипотезы).
- Какая GPU рисует (не проверено).
- Сколько нод реально переопределяют `OutputRevision` (не проверено).
- Как часто выполняются `GeometryFeedback`, `Fluid`, `TrackNodeBase`, `ImageSpectralSynth`-readback (не проверено).
- Прячет ли `imgui-node-editor` готовку за экраном — не проверено.

## Разбор патча «3D Mooshroom» (`3D Mooshroom-12-11-2026.infinite`, ПК с GTX 1050 Ti)

Формат: `cable dst dstPort src srcPort`, `geo dst 0 src 0`, `mod узел параметр LFO …`. Цепочка: Sketch 1 → bloom 5 → Geometry 3 (текстура) → Transform 7 → Transform 9 → Null 8 → Render3D 11 → Output 12; Transform 6 выключен (`flags 1 1 1 0`, второе поле — bypassed, [Patch.h:26](src/core/Patch.h:26)). Модуляторы: LFO 14 → Transform 7, LFO 13 → Render3D 11.

**Главный вывод: по коду этот патч на бумаге не тяжёлый.** Суммарная оценка GPU-работы — единицы миллисекунд, а не 250 мс (4 FPS). Значит, ни одна из гипотез из списка сама по себе 4 FPS не объясняет; причину нужно искать измерением (шаги ниже). Оценки ниже — арифметика по коду, не замеры.

| № | Гипотеза | Вердикт | Вклад |
|---|---|---|---|
| 1 | Мини-вьюпорт у каждой 3D-ноды | механизм подтверждён, но дёшево | малый |
| 2 | Много треугольников | опровергнуто (≈20 тыс.) | малый |
| 3 | Модуляция пересобирает меш каждый кадр | подтверждено, но дёшево | малый |
| 4 | Sketch → bloom | CPU-часть заметна, bloom дешёвый | малый–средний (CPU) |
| 5 | Render3D 1080p, MSAA | MSAA 4×, а не 2× (поправка) | малый–средний |
| 6 | Тела параметров | подтверждено, пренебрежимо | ничтожный |
| 7 | CPU ≈ 0 | объясняется ожиданием GPU/драйвера | — |

1. **Мини-вьюпорты.** `flags … 1 …` третье поле = `showMiniViewport` ([Patch.h:26](src/core/Patch.h:26)); рисуется для узла, если флаг включён и есть источник геометрии ([StageNodeBodies.cpp:427](src/app/frame/StageNodeBodies.cpp:427), `HasUsefulMiniViewport` исключает только ParticleSystem, [ViewportPanel.cpp:105](src/app/panels/ViewportPanel.cpp:105)). У всех 3D-нод (3, 7, 9, 8; у 11 тоже `1`) это включено. Устройство: 190×190 px (`kPreviewSize`, [ViewportPanel.cpp:26](src/app/panels/ViewportPanel.cpp:26)), один направленный свет, без теней, HDRI и MSAA ([NodeViewport.h:3-12](src/core/NodeViewport.h:3)), собственный FBO на узел. **Кэш есть:** перерисовка только при смене ревизии меша, модельной матрицы, материала, камеры, текстуры или размера ([NodeViewport.cpp:562-662](src/core/NodeViewport.cpp:562)). В этом патче всё меняется каждый кадр (анимированная текстура Sketch→bloom меняет `SurfaceTextureRevision`, LFO меняет меш), поэтому кэш не срабатывает: ~4–5 перерисовок по ~20 тыс. треугольников в 190² px за кадр. Это порядка десятых долей мс каждая. Вердикт: правильно, но на 4 FPS не тянет. Замечание: комментарий в [ViewportPanel.cpp:13](src/app/panels/ViewportPanel.cpp:13) говорит «выключено по умолчанию», а поле по умолчанию `true` ([GraphNode.h:57](src/core/GraphNode.h:57)) — расхождение, не проверял, какое значение применяется при спавне.
2. **Геометрия.** `shape 23` = «Arrow 3D» (список [Geometry3DNodes.cpp:22-29](src/nodes/Geometry3DNodes.cpp:22), ветка [строка 1206](src/nodes/Geometry3DNodes.cpp:1206)). `detail 24` для стрелки не используется; `sides 64`: цилиндр + конус ([Mesh.cpp:4464](src/core/Mesh.cpp:4464)) ≈ 350–400 треугольников (оценка, точно не считал). **Transform 7 — не «Transform», а «Array»**: имена операций `0 Transform, 1 Array …` ([GeometryOpNodes.cpp:14-17](src/nodes/GeometryOpNodes.cpp:14)); Array физически копирует меш `count` раз в один большой ([Mesh.cpp:468-505](src/core/Mesh.cpp:468), максимум 256), а не инстансы: 58 × ~380 ≈ 22 тыс. треугольников, ~11 тыс. вершин. **Transform 9 — обычное преобразование**: `count 5` там игнорируется (только Array читает `count`, [GeometryOpNodes.cpp:298](src/nodes/GeometryOpNodes.cpp:298)). Итог на выходе ≈ 20–25 тыс. треугольников — для 1050 Ti пренебрежимо.
3. **Модуляция.** `mod 7 2 14 … -85.94 85.94`: диапазон ±85.9437° совпадает с ползунком «rot / step» у Array ([GeometryParams.cpp:1593](src/app/bodies/GeometryParams.cpp:1593)), то есть, вероятно, это `rotStep` (соответствие индекса параметра №2 не проверено напрямую). `rotStep` входит в сигнатуру ([GeometryOpNodes.cpp:176-190](src/nodes/GeometryOpNodes.cpp:176)) → Array пересобирается на CPU каждый кадр, ревизия растёт, меш заливается на GPU заново, перерисовываются мини-вьюпорты ниже по цепочке и сам Render3D. Объём — около 1 МБ в кадр: дёшево. `mod 11 12 13 … ±180`: по диапазону это `camAzimuth` (в патче `-180`); камера меш не трогает.
4. **Sketch → bloom.** Sketch выполняется программно на CPU (`mEngine.Run`, затем `glTexSubImage2D` 1024²×RGBA8 каждый кадр при `animate`, [SketchNode.cpp:138-169](src/nodes/SketchNode.cpp:138)) — это единственная заметная нагрузка на главный поток; сколько мс — не измерял. Bloom: три прохода по 11 выборок на 1024² в RGBA16F ([FilterDefs.cpp:591-640](src/core/FilterDefs.cpp:591)) ≈ 35 млн выборок — доли миллисекунды. `uRadius 7.58` лишь растягивает шаг выборок, число выборок не меняет; `headroom` — работа в линейном/HDR без клампа.
5. **Render3D.** `samples 2` — это **индекс**: `{Off,2x,4x,8x}`, `kSampleCounts = {0,2,4,8}` ([Geometry3DNodes.cpp:102](src/nodes/Geometry3DNodes.cpp:102)); значит **4× MSAA** (сам заголовок: «index into SampleNames(), so 4x», [Geometry3DNodes.h:759](src/nodes/Geometry3DNodes.h:759)). 1920×1080: renderbuffer цвет+глубина ≈ 66 МБ, лимит 512 МБ не срабатывает ([Geometry3DNodes.cpp:1822](src/nodes/Geometry3DNodes.cpp:1822)). `supersample 1` = один проход (суперсэмплинг и так только в офлайн-рендере, [строка 2081](src/nodes/Geometry3DNodes.cpp:2081)). `engine 0` — растр. AO, DoF, motion blur, bloom, тени выключены. Стоимость на 1050 Ti — порядка 1–3 мс.
6. **`showParams`.** Тела параметров — это ImGui на CPU; на GPU — несколько тысяч вершин. Ничтожно.
7. **CPU ≈ 0.** Главный поток однопоточный: на 8–12-поточном CPU он даёт максимум 8–12% общей загрузки, а при 4 FPS большую часть кадра ждёт драйвер/GPU. Это согласуется с «GPU ≈ 100%, CPU ≈ 0» только если GPU действительно занят; но Диспетчер задач мог показывать другой движок/другую GPU (см. шаги ниже).

**Что остаётся неизвестным и объясняло бы 4 FPS (по убыванию правдоподобия, всё не проверено):**
- (a) Приложение работает не на 1050 Ti (Диспетчер задач → «ГП 0» может быть Intel; ноутбук с Optimus — нет экспорта `NvOptimusEnablement`, §8), либо драйвер NVIDIA в режиме низкой производительности/устаревший.
- (b) Окно/Output очень большое (4K-экран, 200% масштаб) и/или лишние окна: проектор, 3D View, Viewport-панель с 1920×1080 ([`arrange … 1920 1080 60`] в патче).
- (c) Нагрузка от других частей приложения, не вошедших в это чтение: Audio Texture 15, Audio In 16 и «studio» (WAV на `D:\`, которого на другом ПК может не быть).
- (d) Переполнение VRAM другими программами (браузер, игры), а не патчем.
- (e) Включённые принудительно настройки в панели NVIDIA (vsync, сглаживание/анизотропия на приложение) — множатся на все FBO.

### Что менять в ЭТОМ патче на ПК мамы (без правки кода), по шагам

Перед каждым шагом смотреть FPS в верхней строке и «3D»/«Copy» GPU в Диспетчере задач.
1. **Проверить, что работает 1050 Ti**: Настройки → General → «Graphics: …» должно содержать GeForce GTX 1050 Ti. Если нет — Windows → Система → Дисплей → Графика → Infinite → «Высокая производительность», перезапуск. Ожидание: если там Intel, FPS сразу вырастет в разы.
2. **Render3D 11**: `width/height` 1920×1080 → 960×540; `samples` → Off. Ожидание: если FPS заметно растёт, виноват рендер сцены.
3. **Выключить мини-вьюпорты** у 3, 7, 9, 8 (кнопка вьюпорта на ноде). Ожидание: небольшой прирост (десятые доли мс на узел по оценке); значительный прирост опроверг бы мою оценку.
4. **Остановить Sketch**: `animate` выкл. (или временно отсоединить bloom от Geometry 3). Ожидание: меньше CPU-работы и перерисовок мини-вьюпортов; на GPU эффект малый.
5. **Отключить LFO 14** (убрать модуляцию Array) — меш перестанет пересобираться.
6. **Отключить Audio Texture/Audio In/studio-узлы** и закрыть Viewport/3D View/Arrange-панели.
7. **Vsync off + Target FPS Unlimited**, чтобы видеть реальное время кадра, а не кратное 60.
8. Если после 1–7 FPS всё ещё ≈4: замерить PresentMon (см. «Как измерить на Windows») и прислать цифры и скриншот Диспетчера задач (вкладка GPU: «3D», «Copy», «Выделенная память GPU»).

### Правки кода в форке (системно)

| Мера | Эффект | Объём | Риск |
|---|---|---|---|
| Рисовать мини-вьюпорт только для узлов в видимой области и не чаще ~15–30 Гц; для анимированных текстур не перерисовывать всё | средний | S–M | низкий |
| Сделать `showMiniViewport` по умолчанию выключенным и привести комментарий к коду ([GraphNode.h:57](src/core/GraphNode.h:57)) | средний | S | низкий (меняет поведение новых узлов) |
| Кэш `Array`: при смене только `rotStep` не копировать меш на CPU, а применять матрицы в вершинном шейдере (инстансинг) | средний | M | средний |
| Профайлер на уровне узла для GPU-времени на Windows/NVIDIA (`GL_TIME_ELAPSED` работает штатно) с показом в Performance Panel | диагностика | S | низкий |
| Настройка «Preview quality» (масштаб разрешения превью и мини-вьюпортов) | средний–высокий | M | средний |
| Sketch: растеризация в рабочем потоке, обновление текстуры через PBO | средний (CPU) | M | средний |
| Предупреждение при выборе GPU/Optimus (`NvOptimusEnablement`) | высокий для Optimus-ноутбуков | S | низкий |
