# Связка Infinite ↔ Bitwig Studio через OSC: исследование

Дата: 2026-11-11. Только чтение кода и веб-документации; код не менялся, ничего не запускалось и не собиралось. Всё, что требует запуска, помечено «не проверено».

## Коротко

- Bitwig штатно OSC **не отправляет**. OSC даёт расширение DrivenByMoss (режим «Open Sound Control»).
- DrivenByMoss шлёт сообщения **bundle'ами** (`sendBundle`), а OSC Receive в Infinite `#bundle` **отбрасывает**. С текущим кодом связка, скорее всего, вообще не заработает. Это главный пробел.
- Тип аргумента для громкости совместим: float, диапазон 0..127 при дефолтном разрешении. Адреса точные, поэтому wildcard не нужен.
- OSC Receive отдаёт только одно значение 0..1 через `low/high`. «Play/Stop» работает как 0/1 на адресе `/play`.
- Для транспорта лучше MIDI clock/Start/Stop. Но входной MIDI clock в Infinite сейчас никуда не подключён, он только оценивает BPM.

---

## Часть 1. Код Infinite

### 1.1 OSC Receive / OSC Send

Файлы: `src/nodes/OscNodes.h`, `src/nodes/OscNodes.cpp`, кодек `src/core/OscMessage.{h,cpp}`.

| Вопрос | Факт | Где |
|---|---|---|
| Типы аргументов (приём) | `f`, `i`, `s`. Прочие теги (`T`, `F`, `d`, `h`, `b` и т.д.) → пакет отбрасывается целиком | `OscMessage.cpp` (Decode / DecodeMany, ветка «unsupported type tag») |
| Bool `T`/`F` | **Не поддерживается** (тег `T`/`F` не распознан) | `OscMessage.cpp` Decode, `else return false` |
| Несколько аргументов | Нода использует `Decode` — **только первый** аргумент. `DecodeMany` умеет много, но его использует только OSC-control API | `OscNodes.cpp:78`; `OscMessage.h:30-31` |
| Строка | Принимается кодеком, но нода её пропускает (`continue`), значение не меняется | `OscNodes.cpp:94` |
| Int | Приводится к float `static_cast<float>` | `OscNodes.cpp:93` |
| Bundles | **Не поддерживаются**: адрес должен начинаться с `/`, иначе `false`. В заголовке прямо сказано «No bundles» | `OscMessage.cpp` (`address[0] != '/'` → false); `OscMessage.h:7-8,24-25` |
| Сопоставление адресов | Только **точное** равенство строк `addr != filter`. Ни wildcard (`*`, `?`, `[]`, `{}`), ни `//` | `OscNodes.cpp:86` |
| Порт | Параметр `port`, по умолчанию 9000. Смена порта пересоздаёт сокет в `Value01()` | `OscNodes.h:35`; `OscNodes.cpp:42-99,125` |
| Интерфейс | Bind **только 127.0.0.1** (loopback). Принимает лишь с той же машины; из сети нельзя. Для Bitwig на том же Mac это нормально | `OscNodes.cpp:55` |
| Поток | Один поток на экземпляр ноды, блокирующий recv. Размер буфера **1024 байта** (более крупный датаграмм усекается → Decode упадёт) | `OscNodes.cpp:66-71` |
| Значение → граф | Поток пишет `std::atomic<float> mLastValue`. Граф тянет `Value01()` (pull-модель модуляторов) | `OscNodes.cpp:96,109-134` |
| Масштаб | `(raw-low)/(high-low)`, клип в 0..1; при `high==low` возвращает 0 | `OscNodes.cpp:128-133` |
| Сглаживание в ноде | **Нет.** Но есть сглаживание на привязке (см. 2) | — |
| Начальное значение | `mLastValue = 0.5` до первого пакета | `OscNodes.h:46` |
| Параметры для сохранения | `port`, `address`, `low`, `high` | `OscNodes.cpp:101-107` |
| OSC Send | Хост/порт/адрес, один float (`EncodeFloat`) по `Value01()`; шлёт при изменении ≥ `epsilon` или раз в `intervalMs`. Новый UDP-сокет на каждую отправку; `host` через `inet_addr` (только IPv4-литерал) | `OscNodes.cpp:140-185` |

Дополнительно: отдельный **OSC control API** (`src/core/OscControl.{h,cpp}`, обработчик `src/app/graph/OscApi.cpp`). Он слушает 127.0.0.1:9100 (`app/Startup.cpp:1707-1713`, порт переопределяется переменной окружения). Адреса: `/infinite/node/<index>/<param>`, `/infinite/create`, `/connect`, `/show/next` и т.д. (`OscApi.cpp:112,228-334`). Он тоже без bundles (`DecodeMany` отвергает `#bundle`) и **без адресов транспорта** (play/stop/tempo в OscApi.cpp отсутствуют). Транспортом управляет JSON-RPC (`app/graph/RpcTerminal.cpp:88-112`: play/stop/rewind/seek/tempo), но это не OSC.

### 1.2 Подключение выхода к параметрам, learn

- OSC Receive — `IModulator` (`OscNodes.h:21`). Его выход подключается к «пину модуляции» любого слайдера, как LFO/Random (комментарий в `NodeVocab.gen.h:194`). Модулятор говорит в 0..1, каждый приёмник мапит это на свой диапазон (`core/Modulation.h:15`).
- Привязки живут в `Modulation.h`: на привязку `(nodeIndex, paramIndex)` есть **`smoothMs`** (постоянная времени сглаживания, 0 = выкл; `Modulation.h:159-164,186-188`). Значит сглаживание громкости делается на привязке, а не в ноде.
- **OSC learn нет.** Learn есть только для MIDI: `MidiCCNode::StartLearn` (`nodes/MidiNodes.h:29`), `MidiTriggerNode::StartLearn` (`:86`), `gPerfMidiLearnIdx` (`app/AppState.cpp:656`). Адрес OSC нужно вводить руками.

### 1.3 Синхронизация с внешней DAW

| Механизм | Состояние | Где |
|---|---|---|
| MIDI clock **in** | Принимает 0xF8 и считает сглаженный BPM (`MidiClockBpm()`, `MidiClockIsPresent()`); 0xFA/0xFC лишь сбрасывают окно. **Нигде в приложении не используется для управления Transport** (кроме запрета собственного clock-out в `MidiOutNode`). Реализовано в Platform.mm (macOS), MidiWin, MidiLinux | `Platform.h:1286-1294`; `Platform.mm:5555-5584`; единственный потребитель `nodes/MidiOutNode.cpp:202`; в `docs/plans/vst-au/README.md:103` как задуманное «Follows MIDI clock in» — **не реализовано**, по крайней мере не найдено в коде |
| MIDI clock **out** | Есть: 24 ppqn + Start/Continue/Stop + SPP, флаг `clock` в MIDI Out | `MidiOutNode.h:42`; `MidiOutNode.cpp:197-220` |
| Ableton Link | **Нет** (поиск по `ableton`/`link` в `src` — ничего) | — |
| MTC | **Нет** (поиск по `MTC`/`timecode` — пусто) | — |
| Запуск транспорта извне | RPC `transport play/stop/rewind/seek/tempo` (`RpcTerminal.cpp:88-112`); плагинный режим (VST3/AU): `Transport::FollowHost` берёт tempo/play/ppq у хоста | `core/Transport.h:165-184` |
| MIDI Note/CC | Полноценно: CC, ноты, pitch bend, learn, per-device | `Platform.h:1031+`; `MidiNodes.h` |

---

## Часть 2. Сторона Bitwig (веб)

### 2.1 Штатный OSC
Штатной отправки OSC в Bitwig нет. На странице поддержки Bitwig об OSC ничего нет, а в обсуждениях на KVR OSC получают через расширение (https://www.kvraudio.com/forum/viewtopic.php?p=8572891). Пользователи просят «OSC без скрипта» (https://www.kvraudio.com/forum/viewtopic.php?p=7672560). Не проверено: вопрос про «OscModule» в Controller API. В одном из KVR-постов упоминается нативный OSC-класс в API (https://www.kvraudio.com/forum/viewtopic.php?p=8625905), но методы я не подтвердил. Это относится к написанию своих скриптов, не к штатной функции Bitwig.

### 2.2 DrivenByMoss (режим Open Sound Control)
Источники: документация https://github.com/git-moss/DrivenByMoss-Documentation (раздел OSC, файл `Generic-Tools-Protocols/Open-Sound-Control-(OSC).md`); исходники https://github.com/git-moss/DrivenByMoss (`controller/osc/OSCConfiguration.java`, `framework/osc/AbstractOpenSoundControlWriter.java`).

- Установка: Add controller → Open Sound Control. Выбрать MIDI-вход (рекомендуется виртуальный порт: он нужен только для нот, пришедших по OSC; на KVR сообщают, что без MIDI-устройства диалог может не появиться). В Preferences: «Port to receive on», «Host to send to», «Port to send to». Смена хоста/порта требует перезапуска расширения.
- Порты по умолчанию (код `OSCConfiguration.java`): **приём 8000, отправка 9000**. Дефолтный хост отправки в доке не указан (видимо localhost; не проверено).
- Разрешение значений: «Low (128)», «Medium (1024)», «High (16384)». По умолчанию Low → `MAX_VALUE`=128 (вероятно, 0..127; не проверено).
- Транспорт (отправка из Bitwig): `/play` (состояние 0/1), `/tempo/raw`, `/time/str`, `/beat/str`. В исходнике `TransportModule` на `/play` уходит `boolean`; writer конвертирует boolean в `Integer` 1/0 (не проверено в `sendOSC`, пишу по описанию). Команды в Bitwig: `/play`, `/stop`, `/restart`, `/record`, `/tempo`, `/time`.
- Дорожки: `/track/{1-8}/volume` и `/pan` `{0-MAX_VALUE}`, `/track/{1-8}/vu`, включение VU через `/track/vu` 0/1; выбранная дорожка `/track/selected/{attr}`; мастер `/master/{attr}`. Адресация — по банку из 8 дорожек, а не по имени (на KVR жалуются на громоздкие адреса).
- **Типы и фрейминг (из исходника):** `double` → **float**, `int` → int, `boolean` → int 1/0, строки — `s`. Сообщения копятся и уходят **одним bundle** (`oscClient.sendBundle(messages)` в `flush()`). Перед и после цикла обновления идёт `/update`. Подтверждено по коду; не подтверждено на проводе (нужен захват трафика).
- Нагрузка: в bundle попадает много данных (устройства, треки), размер может превысить 1024 байта. Не проверено.

### 2.3 Альтернативы
- **MIDI**: Bitwig умеет слать MIDI clock на выбранный порт (опция «Send MIDI Clock» в HW Instrument; страница Dashboard → Synchronization; https://www.bitwig.com/userguide/latest/hardware ). Точные шаги не проверены. Громкость и параметры — через MIDI CC (Remote Controls / Generic MIDI controller); Infinite это уже умеет, включая learn.
- **Ableton Link**: Bitwig поддерживает с версии 2.2 (https://www.bitwig.com/stories/bitwig-studio-22-223/). В Infinite Link нет.
- **Плагин Infinite (VST3/AU) внутри Bitwig**: `Transport::FollowHost` берёт tempo/play/ppq у хоста (`Transport.h:165-184`). Не проверено, работает ли это в Bitwig; громкость дорожки так не получить.
- **Другие OSC-расширения**: OSC4Bitwig (устаревший предшественник DrivenByMoss, https://github.com/grmcom/OSC4Bitwig), собственный скрипт на Controller API.

---

## Часть 3. Сопоставление

| Сообщение Bitwig (DrivenByMoss) | Примет ли OSC Receive сейчас | Что мешает |
|---|---|---|
| Любое (всё идёт bundle'ом) | **Нет** (скорее всего) | `#bundle` отбрасывается (`OscMessage.cpp`); плюс буфер 1024 Б |
| `/play` (0/1, int) | Нет из-за bundle; без него — да: int→float, `low=0 high=1` | bundle; нет параметра «режим триггера» |
| `/stop` | Не отправляется (по исходнику `TransportModule.flush()`), состояние приходит в `/play` | — |
| `/track/1/volume` (float 0..127) | Нет из-за bundle; без него — да, `low=0 high=127` | bundle; номер дорожки привязан к банку из 8 дорожек |
| `/track/1/pan` | То же; `low=0 high=127` (центр 63.5) | bundle |
| `/track/1/vu` (при `/track/vu 1`) | То же, но частота высокая | bundle; нужен `smoothMs` на привязке |
| `/tempo/raw` (BPM, число) | Нет из-за bundle; без него — да (`low=40 high=240`) | bundle; не управляет BPM самого Infinite |
| `/track/selected/volume` | То же, что `/track/N/volume` | bundle; wildcard не нужен |
| `/beat/str`, `/time/str` (строки) | Нет: строки игнорируются | нет string → число |

### Конкретные пробелы в коде нод
1. Нет разбора bundles (`OscMessage.cpp`, `Decode`/`DecodeMany`, `OscNodes.cpp:78`). **Критично.**
2. Буфер приёма 1024 Б (`OscNodes.cpp:67`): большой bundle усечётся.
3. Типы `T`/`F` (bool), `d` (double), `h` (int64) не принимаются: пакет отвергается целиком, а не пропускается по одному аргументу. DrivenByMoss `T/F` не шлёт (шлёт int), но другие отправители шлют.
4. Только первый аргумент (`Decode`): нельзя выбрать N-й аргумент.
5. Нет wildcard/паттернов адресов: чтобы слушать N дорожек, нужно N нод, каждая со своим сокетом/потоком. Каждая нода занимает свой порт `bind` — **два OSC Receive на одном порту (9000)**: второй bind завершится ошибкой (сокет без `SO_REUSEADDR`, это нигде не выставляется), и нода молча не получит ничего. Для 2+ дорожек и транспорта из Bitwig нужна одна нода на порту, что делает невозможным «громкость + Play» без правок. Не проверено запуском, но по `bind` без reuse это ожидаемо.
6. Нет сглаживания и «режима триггера/порога» на самой ноде (есть только `smoothMs` на привязке).
7. Нет OSC learn.
8. Нет приёма транспорта по OSC (`OscApi.cpp` без play/stop/tempo).
9. Ошибка bind (порт занят) молча игнорируется: нода просто молчит, без индикации (`OscNodes.cpp:57-61`).

---

## Часть 4. Черновик инструкции (после правок кода; без них шаги 5–8 не заработают)

1. Установить DrivenByMoss (с сайта автора, https://github.com/git-moss/DrivenByMoss-Documentation). Файл `.bwextension` положить в папку Extensions Bitwig и перезапустить Bitwig. (Не проверено, путь зависит от ОС.)
2. В Bitwig: Settings → Controllers → Add controller → Moss → Open Sound Control. (Не проверено.)
3. В настройках расширения: Port to receive on 8000, Host to send to 127.0.0.1, Port to send to 9000, Value resolution Low (128). (Хост по умолчанию не проверен.)
4. Для виртуального MIDI-порта на macOS: IAC Driver в Audio MIDI Setup, если Bitwig не показывает диалог без MIDI. (Не проверено.)
5. В Infinite: добавить ноду OSC Receive (Utility). Port = 9000, Address = `/play`, low = 0, high = 1. (Не проверено; требует поддержки bundles.)
6. Добавить вторую ноду OSC Receive, Address = `/track/1/volume`, low = 0, high = 127. (Не проверено; второй сокет на 9000 требует правки, см. пробел 5.)
7. Выход нужной ноды перетащить на пин модуляции параметра визуальной ноды. На привязке выставить smooth ~ 50–100 мс для громкости. (Не проверено.)
8. Нажать Play в Bitwig: `/play` приходит 1/0 при изменении состояния. Проверить, что значение меняется на мониторе ноды. (Не проверено.)
9. Проверка без Bitwig: `echo`-скрипт отправки одиночного OSC-сообщения (не bundle) на 127.0.0.1:9000, чтобы убедиться, что нода жива. (Не проверено; сообщения без bundle ноды уже должны приниматься.)

---

## Выводы для реализации

- **Правки кода нужны.** Без разбора bundles связка с DrivenByMoss, по исходнику, не заработает.
- **Минимум (S):** в `src/core/OscMessage.cpp/.h` добавить разбор `#bundle` (заголовок, timetag, элементы с длиной, рекурсивно) и прогонять каждый вложенный message через существующий путь. Затем увеличить буфер в `OscNodes.cpp:67` (например, до 64 КБ). Плюс тест в selftest (там уже есть клиенты OSC).
- **Дополнительно (M):** общий приёмник на порт с рассылкой по адресам (раздача по нескольким нодам на одном порту): сейчас два OSC Receive на одном порту, вероятно, конфликтуют (нужно проверить запуском). Альтернатива: `SO_REUSEADDR`/`SO_REUSEPORT` (для UDP unicast на macOS отдаёт пакеты не всем, поэтому лучше общий диспетчер). Это затрагивает `OscNodes.{h,cpp}`.
- **Желательно (S–M):** wildcard-адреса, выбор индекса аргумента, приём `T/F/d/h` без отбрасывания пакета, индикация ошибки bind, OSC learn (по аналогии с `MidiCCNode::StartLearn`).
- **Общая оценка: M** (bundles + общий приёмник + тест). Если ограничиться bundle-разбором и одной нодой на порт, получится S.
- **Транспорт:** вместо OSC `/play` лучше **MIDI clock + Start/Stop** из Bitwig: он даёт и BPM, и фазу, а приём 0xF8/0xFA/0xFC уже реализован на уровне платформы (`Platform.mm:5555+`). Но он **не подключён к `Transport`**: нужен код, который по `MidiClockBpm()` и Start/Stop ставит темп и play (по аналогии с `Transport::FollowHost`; M). OSC `/play` нужен как быстрый вариант для запуска визуальных нод.
- **Ableton Link** (Bitwig умеет с 2.2) дал бы синхронизацию без проводов, но в Infinite его нет и требует внешней библиотеки (L, лицензия GPL/коммерческая; не проверялось). Для первой версии не рекомендую.
- **Рекомендация:** (1) bundles + буфер, (2) общий диспетчер портов, (3) затем MIDI clock → Transport. Громкость и транспорт через OSC, темп и позицию — через MIDI clock.
- **Не проверено вообще:** реальные пакеты DrivenByMoss (захват трафика; тип `/play`, размер bundle), поведение двух нод на одном порту, настройки Bitwig в части MIDI clock out, хост по умолчанию.

## Источники
- DrivenByMoss, документация OSC: https://github.com/git-moss/DrivenByMoss-Documentation
- Исходники: https://github.com/git-moss/DrivenByMoss (`OSCConfiguration.java`, `AbstractOpenSoundControlWriter.java`, `TransportModule.java`, `OSCWriter.java`)
- KVR, обсуждения DrivenByMoss/OSC: https://www.kvraudio.com/forum/viewtopic.php?p=8572891, https://www.kvraudio.com/forum/viewtopic.php?p=7672560, https://www.kvraudio.com/forum/viewtopic.php?p=8625905
- Bitwig, Link в 2.2: https://www.bitwig.com/stories/bitwig-studio-22-223/
- Bitwig, руководство (hardware / MIDI clock): https://www.bitwig.com/userguide/latest/hardware
