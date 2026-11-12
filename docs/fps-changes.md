# Ветка my/fps: меры против низкого FPS (Windows, GTX 1050 Ti)

Основа: `docs/research/fps-gpu.md`. Всё, что в `_WIN32`-коде, **не проверено на Windows** (собрать там нельзя).

## Что сделано
1. **Optimus/PowerXpress** (`src/app/Startup.cpp`, только `_WIN32`): экспорт `NvOptimusEnablement=1` и `AmdPowerXpressRequestHighPerformance=1` из exe — на ноутбуках с двумя GPU берётся дискретная. Не проверено на Windows.
2. **Мини-вьюпорты нод**: `GraphNode::showMiniViewport` по умолчанию `false` для новых нод (старые патчи хранят флаг явно — не тронуты). Перерисовка не чаще ~20 Гц (`NodeViewport::Render`, исключения: смена размера и принудительная перерисовка); если миниатюра вне видимой области ImGui — рендер пропускается (`ViewportPanel.cpp`).
3. **Readback → PBO**: новый `src/core/PboReadback.h` (кольцо из 2 PBO, задержка 1 кадр, первый вызов и разреженная выборка реже ~10 Гц читаются синхронно). Подключён в Image Analyze, Palette, Predictive Coloring. Остальные места из таблицы отчёта (ImageToPoints, GeometryFeedback, Fluid, TrackNodeBase, SpectralSynth, RemoveBg) **не тронуты** — читают по событию/редко или с float-данными; частота Analyze/Palette и так задаётся их параметром `sampleRate`.
4. **Диагностика**: на метке fps в верхней строке — тултип с `GL_RENDERER`, мс/кадр и предупреждением; в Settings → General → Graphics — предупреждение оранжевым, если рендерер Intel / Microsoft Basic / GDI Generic / llvmpipe (только Windows; наличие другой GPU не определяется — не проверено). Время кадра и fps в верхней строке уже были. GPU-таймеры по нодам (`GpuTimerRing`) работают только в bench-режимах (`INFINITE_BENCH_B2GPUNODES` и т.п.); на не-Apple они не отключены, в штатном UI их нет — **не сделано**. Performance Panel — это матрица MIDI-управления, не профайлер, туда ничего не добавлял.
5. **Качество превью** (Settings → General → Performance & Rendering, 1 / ½ / ¼, по умолчанию 1, хранится в `Infinite.general-settings`): масштабирует разрешение рендера мини-вьюпортов. На превью image-нод **не влияет** (текстура нод полного размера, это уже средний риск по отчёту).
6. Отладка: `INFINITE_FPS_LOG=1` печатает `FPSLOG mean_ms=… fps=… max_ms=…` каждые 120 кадров (после 120 прогревочных); `INFINITE_FORK_LEGACY=1` возвращает прежнее поведение п.2–3 для A/B.

## Замер (этот Mac: i9-9980HK, Debug x86_64, vsync по умолчанию, патч Mooshroom, 500 кадров)
| режим | mean ms/кадр | fps |
|---|---|---|
| legacy (`INFINITE_FORK_LEGACY=1`) | ~700 | 1.4 |
| новый | ~728 | 1.4 |

**Разницы нет.** `sample` показал: 100% времени — CPU на главном потоке в `Render3DNode::BuildSceneSignature → GeometryOpNode::MeshRevision/GetMesh → MeshOps::Array` (пересборка меша Array 7 каждый кадр из-за LFO; в Debug STL-обёртки раздувают в разы). Мини-вьюпорты, PBO и GPU тут не узкое место. Замер не отражает Release и Windows — **не проверено**. Вывод: на ПК мамы причина может быть иной (GPU/Optimus/VRAM, см. отчёт), но Array-пересборка — реальная находка: **следующий шаг** — кэш/инстансинг для `MeshOps::Array` и проверка, не вызывается ли `GetMesh` несколько раз за кадр (в стеке он вложен в `CurrentSignature`).

## Инструкция владельцу: проверка на ПК мамы
1. Поставить сборку, открыть патч Mooshroom. Навести мышь на «NN fps / NN ms» вверху справа — тултип покажет GPU. Если там Intel/Basic — выбрать Settings → Graphics processor → High performance, перезапустить.
2. Запуск из cmd с логом: `set INFINITE_FPS_LOG=1` и `Infinite.exe "путь\патч.infinite"` — в консоли строки FPSLOG. Сравнить с `set INFINITE_FORK_LEGACY=1`.
3. Попробовать Quality ½/¼, выключить мини-вьюпорты, `set INFINITE_IMAGE_RGBA8=1`.
4. Прислать: строку `GPU:` из тултипа, 3–5 строк FPSLOG для каждого режима, скриншот Диспетчера задач → GPU («3D», «Copy», «Выделенная память GPU») в момент просадки, модель CPU, версию драйвера NVIDIA.
