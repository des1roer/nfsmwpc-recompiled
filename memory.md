# Передача работы: APK NFS Most Wanted с игрой → `rel/`

Задача пользователя: собрать Android-APK nfsmwpc-recompiled **с файлами игры внутри**
и положить в `C:\dev\nfsmwpc-recompiled\rel`. Образец процесса — соседний
`C:\dev\nfsu2-recompiled` (его `memory.md` и `android/app/build.gradle`).

Состояние на 2026-11-10: lift всех модулей готов, launcher/gradle доработаны,
идёт (или упала/закончилась) сборка `assembleDebug`. Лог: `work/gradle_build.log`.

## 1. Какая копия игры

- **Используется `C:\games\Need for Speed Most Wanted`** (указал пользователь).
- `C:\games\NeedforSpeedMostWanted` НЕ годится: там speed.exe 1.0 с SecuROM
  (7 250 800 байт, SHA `2cf8bdbf…`, секции `stxt774/stxt371`, EP в заглушке).
  SecuROM не снимать — вопрос закрыт.
- speed.exe в выбранной папке: 6 029 312 байт, SHA-256
  `325304a2cdd5d1d5789c09c90d249774743d1b273889b1093339ac922d56a968`.
  Эталон проекта (`config/reference.json`) — `80774c2e…253c`. Хэш другой, но:
  секции/EP/SizeOfImage идентичны, таблица релокаций идентична (240 976 байт,
  114 755 HIGHLOW), 300 импортов, все 20 хуков `config/hooks.txt` на границах
  инструкций, override и forced seeds в каталоге. Вывод: тот же 1.3, другой
  NoCD-кряк; разница в нескольких байтах, где именно — неизвестно (эталона нет).
  Если в игре будет краш, который у апстрима не воспроизводится, — подозревать это.
- server.dll из этой папки: SHA `c4fdd0e9…` (именно он поднят и должен лежать на телефоне).
- d3dx9_26.dll взят из `C:\Windows\SysWOW64` (ImageBase 0x400000, SizeOfImage 0x24a000 —
  совпадает с `guest_modules.inc`).
- Языки в копии: только `English.bin` и `Russian.bin`.

## 2. Что сгенерировано (всё gitignored, пересоздавать НЕ нужно)

Последовательность (в README она описана неполно — вот рабочая):

```bash
cd C:/dev/nfsmwpc-recompiled
G="C:/games/Need for Speed Most Wanted"
# speed.exe
python scripts/analyze_binary.py "$G/speed.exe" work
python scripts/data_seeds.py work/speed.analysis.exe work/seeds_data.json
python scripts/run_lift.py catalog work/speed.analysis.exe work/seeds_data.json work/catalog0.json   # ~5 мин
python scripts/code_seeds.py work/speed.analysis.exe work/catalog0.json work/seeds_code.json --merge work/seeds_data.json
python scripts/run_lift.py catalog work/speed.analysis.exe work/seeds_code.json work/catalog.json    # ~5 мин
python scripts/run_lift.py lift work/speed.analysis.exe work/catalog.json src/recomp/gen
# server.dll
python scripts/prepare_server.py "$G/server.dll"                       # -> src/recomp/gen_server
# d3dx9_26.dll (база 0x00B00000)
python scripts/prepare_module.py C:/Windows/SysWOW64/d3dx9_26.dll 0x00b00000 work/d3dx/d3dx-relocated.dll work/d3dx/seeds.json
python scripts/run_lift.py catalog work/d3dx/d3dx-relocated.dll work/d3dx/seeds.json work/d3dx/catalog.json --seeds-only   # >10 мин
#   work/d3dx/exclude.json сделан вручную (скрипта в репо нет): записи каталога, начинающиеся
#   с 00 00 или содержащие arpl/insb/insd/outsb/outsd/bound/aaa/aas/in/out/int3/... ,
#   минус экспорты и вызываемые/прыжковые цели -> 2133 записи
python scripts/run_lift.py lift work/d3dx/d3dx-relocated.dll work/d3dx/catalog.json src/recomp/gen_d3dx --prefix d3dx_ --exclude work/d3dx/exclude.json
# звуковые банки
python scripts/abk_module.py "$G" work/abk/abk.dll work/abk/seeds.json src/recomp/gen_abk/abk_blocks.c
python scripts/run_lift.py catalog work/abk/abk.dll work/abk/seeds.json work/abk/catalog.json --seeds-only
python scripts/run_lift.py lift work/abk/abk.dll work/abk/catalog.json src/recomp/gen_abk --prefix abk_
```

Результаты: speed.exe 41 263 функции / 104 файла / 0 lift failures / 71 unimpl
(апстрим: 41 262 / 72); server.dll 1 770 функций, 0 failures; d3dx 0 failures
(unimpl — 3DNow!-ветки и excluded); ABK 66 блоков / 78 функций, 0 failures.

## 3. Изменения в коде (НЕ закоммичены, `git diff`)

- `android/app/build.gradle`: перенесён механизм nfsu2 — `-PnfsGameData=DIR`
  (или `nfs.gameData` в `local.properties`) → задача `syncGameData` кладёт игру в
  `build/game-assets/nfsmw` → `assets/nfsmw`, `noCompress ''`. Исключены Redist/,
  Support/, scripts/ (x86 ASI), unins*, *.URL, *.ico, dinput8.dll, *_inst.exe и т.п.
  `d3dx9_26.dll` добавляется из `-PnfsD3dx`/`nfs.d3dx`, иначе из `%SystemRoot%\SysWOW64`.
  Без `nfsGameData` сборка как раньше (code-only).
- `LauncherActivity.java`: распаковка `assets/nfsmw` → `/sdcard/nfsmw` при первом
  запуске (копия кода nfsu2: маркер `.bundle_installed` = lastUpdateTime, докачка по
  размеру, `.part`-файлы, прогресс в status); язык игры «Русский» в списке;
  `ensureLanguage()` — если сохранённого языка (по умолчанию Spanish) нет в
  LANGUAGES, выбирается Russian (при ru-локали) / English / первый найденный.
- `Lang.java`: переводы новых строк (es/en/pt).
- `.gitignore`: `.idea` (это было до меня). `work/patch_launcher.py` — скрипт, которым
  вносились правки в Java (уже применён, повторно не запускать — упадёт на assert).

## 4. Сборка APK (где остановился)

```bash
cd C:/dev/nfsmwpc-recompiled/android
export ANDROID_HOME=C:/Android/Sdk      # local.properties в MW нет, не создавал
./gradlew.bat --no-daemon assembleDebug "-PnfsGameData=C:/games/Need for Speed Most Wanted" "-Dorg.gradle.jvmargs=-Xmx4g" > ../work/gradle_build.log 2>&1
```

- `assembleDebug` — как в nfsu2 (подписывается debug.keystore; гостевой код всё равно -O2).
- На момент записи прошли syncGameData, mergeDebugAssets, configureCMake;
  шёл `buildCMakeDebug[arm64-v8a]` (компиляция ~8,5 млн строк C + ThinLTO — долго).
- Проверить итог: `tail -40 work/gradle_build.log` (ищи `BUILD SUCCESSFUL` / `error:`).
  Если процесс gradle умер вместе с сессией — просто перезапустить ту же команду
  (инкрементально продолжит).

## 5. Что осталось сделать

1. Дождаться `BUILD SUCCESSFUL`; APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
2. Проверить содержимое: `unzip -l app-debug.apk | grep assets/nfsmw | head` и что
   внутри есть `assets/nfsmw/speed.exe`, `d3dx9_26.dll`, `server.dll`, `server.cfg`,
   папки CARS/FRONTEND/GLOBAL/LANGUAGES/MOVIES/NIS/SOUND/TRACKS; размер ≈ 3,2 ГБ.
3. Скопировать в `rel/` по аналогии с nfsu2 (`-arm64-v8a.apk` = с игрой):
   `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a.apk`. Локальные сборки не удалять.
4. Сообщить пользователю (по-русски): размер, что нужно ~4 ГБ свободно на телефоне,
   разрешение «доступ ко всем файлам», APK > 2 ГБ может не встать на части устройств,
   speed.exe не байт-в-байт эталон. Ничего не коммитить/не пушить без просьбы;
   игровые файлы никогда не публиковать.

## 6. Второй заход (2026-11-10): русификация, оптимизации nfsu2, читы, спидометр

Всё ниже тоже НЕ закоммичено.

**Русификация лаунчера** — `work/patch_lang_ru.py` (уже применён): язык `RU` в `Lang.java`
(отдельная таблица `ru(es, ru)`, нет перевода → английский), флаг 🇷🇺 в выборе языка, ru-локаль
телефона → русский по умолчанию. `work/lang_check.py --ru` (запускать из папки java-пакета)
проверяет, что у каждого ключа есть перевод.

**Оптимизации из nfsu2:**
- Флаги сборки (мои правки): `CMakeLists.txt` — `NFS_GUEST_CPU` (`-mcpu=<cpu>+nosve+nosve2+nosve2-bitperm`),
  `NFS_GUEST_OPT`, `NFS_GUEST_FAST` (гостевая память по фикс. адресу 0x100000000 + `-fomit-frame-pointer`),
  отдельный `NFS_GUEST_NO_YIELD` (в nfsu2 входит в FAST; в MW вынесен и по умолчанию ВЫКЛ — не проверено,
  что MW не крутит спин-ожиданий без Win32-ожиданий), `NFS_PGO` generate/use, `-fvisibility=hidden` у
  `nfsmw_android`. `arm64_guest.h`: RECOMP_ICALL/ITAIL через `nfs_icall_resolve`, без g_icall-трассы;
  ADDR с фикс. базой. `guest_memory.h`: MAP_FIXED_NOREPLACE. `main.cpp`: выгрузка профиля PGO по файлу
  `<external files>/pgo/dump`. gradle: `-PnfsCpu -PnfsFast -PnfsNoYield -PnfsOpt -PnfsPgo -PnfsPgoProfile`.
- Рантайм (портировал субагент; 6d85531, 99a2b14 частично, 4415fcf, 34974f7, ab1644b; 6994c52 и
  6b78f5e неприменимы — в MW нет dynamicThunk): кэш icall-целей, D3D9-токены первыми, 16-слотовый кэш
  D3D9-объектов, одна нативная ссылка на обёртку + дедуп шейдерных констант, событийный планировщик
  (blockGuestUntil/signalGuestSync), перерисовка тач-кнопок только при изменениях. Файлы: guest_runtime.cpp,
  guest_threads.inc, guest_kernel.inc, guest_window.inc, d3d9_bridge.cpp/.h, d3d9_*_methods.inc,
  RacingControlsView.java. На устройстве НЕ проверено — первый кандидат при зависаниях/крашах.
- PGO не собран: нужен телефон по adb (на ПК устройств не было). Процесс — как в
  `C:\dev\nfsu2-recompiled\docs\ANDROID_PGO.md`, профиль только под тот же набор флагов.

**Читы и спидометр** (через хуки проекта, без патча x86):
- `config/hooks.txt` +3: `0x0057A540` (Speedometer HUD update, `this`=ecx, скорость м/с в +0x48),
  `0x00692A96` и `0x00692AFA` (чтения запаса нитро `fld [esi+0xF8]`). После правки hooks.txt запускался
  `python scripts/relift_hooks.py` (23 хука проверены, перелифтнуты recomp_0027/0048/0067).
- `src/runtime/widescreen.c` (`recomp_hook`): нитро → `put_float(esi+0xF8, 1.0)` при включённом чите
  (идея из трейнера odell0111/nfs-mostwanted-trainer: он меняет оба fld на чтение константы 1.0 @0x8EC454);
  спидометр запоминает объект. API в `nfs_widescreen.h`.
- Деньги: `[[0x91CF90]+0x10]+0xB4` (int32). Найдено по функции списания 0x56D7A0
  (`sub eax,edx; mov [ecx+0xC],eax`, ecx = `[[0x91CF90]+0x10]+0xA8`) и её 8 вызовам. Начисление —
  `applyPendingCash()` в guest_runtime.cpp на Present (гостевой поток), с проверкой байт 0x56D7A0.
- Java: `GameActivity` — оверлей «N км/ч» сверху по центру (как nfsu2), диалог `showCheats()`
  (+1 000 000 $; переключатель бесконечного нитро, хранится в prefs `nfsmw_cheats`); пункт «Читы» в
  «Настройках управления» (`RacingControlsView`). Патч-скрипт: `work/patch_cheats_java.py` (применён).
- Трейнер скачан и декомпилирован только для чтения: `work/trainer_dl/` (не запускать), ilspycmd в `work/tools`.

**Скип заставок** (как в nfsu2 89d4479, патч `work/patch_skip_intro.py`, применён): опция лаунчера
«Заставки при запуске» (`GameOptions.INTRO`, по умолчанию skip) → `nativeSkipIntro` → `guestSkipIntro`.
В `guest_files.inc` `skippedIntroMovie()`: `MOVIES/ealogo*.vp6` и `MOVIES/psa*.vp6` отдают «файл не найден»
в CreateFile и GetFileAttributes (в MW нет blank.vp6, как в nfsu2; удаление этих файлов — известный PC-твик,
игра просто идёт дальше). На устройстве не проверено.

**Сборки** (скрипт `work/build_two.sh`, логи `work/gradle_portable.log`, `work/gradle_a720.log`):
- `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a.apk` — переносимая, с игрой;
- `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a-a720-fast.apk` — Xiaomi 15T: `-PnfsCpu=cortex-a720 -PnfsFast`
  (на старых ядрах не запустится).
- `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a--light-a720-fast.apk` — то же под 15T, но БЕЗ файлов игры
  (скрипт `work/build_light_a720.sh`, без `-PnfsGameData`; нативная часть переиспользуется). Схема имён как в
  nfsu2: `--light` = без игры. Публиковать (если попросят) можно только light.
- Итог 2026-11-10 11:06: все три APK собраны и лежат в rel/ (zip-целостность проверена; у a720-fast и
  light одинаковая libnfsmw_android.so). На телефоне НИЧЕГО не проверялось — следующий шаг: установка,
  проверка старта/скипа заставок/спидометра/читов/планировщика; при желании PGO по docs nfsu2.
- Issue codepdbh/nfsmwpc-recompiled#773 (Xiaomi 15T 25069PTEBG, MT6899): «Instruction unsupported at
  0x00834e2d: lock xadd». Наш pcrecomp (C:\dev\pcrecomp, детач. ветка с патчами) не умеет xadd/cmpxchg —
  добавлено в `scripts/mw_lifter.py` (_lift_interlocked; под машинным локом достаточно обычного RMW), перелифт
  `python scripts/relift_hooks.py --interlocked` (новый флаг; 29 функций, recomp_0077.c).
- Второй краш после этого: SIGSEGV в sub_007F0F48 (поиск в таблице строк [[0x91CB20]+0x10]) — игра грузила
  SPANISH.BIN, которого нет в русской копии: `setGuestLanguage` не знал "Russian" и падал в "Spanish".
  Исправлено: Russian в списке + fallback на существующий LANGUAGES/<lang>.bin (English, затем любой).
- Проверено на телефоне по adb (wifi) light-сборкой `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a--light.apk`:
  титул на русском, заставки пропущены (лог «Intro skipped»), главное меню, «Быстрый старт» → гонка 53-60 FPS,
  оверлей «N км/ч» совпадает с HUD. Меню читов на устройстве НЕ проверено. Скрипт: `work/light_install_run.sh`.
  adb: C:/Android/Sdk/platform-tools/adb.exe; в меню нажатия только `input swipe x y x y 150` (короткий tap
  игнорируется); правый верхний угол экрана у Xiaomi ловит системный жест «Подключить» — не тапать туда.
- Миникарта ближе к левому краю (просьба пользователя): `widescreen.c` — при «мини-карта слева вверху»
  (`nfs_widescreen_set_minimap_left`, из `nativeMinimapTop`) карта, 5 детей (+0x1C), local X (+0xB8), рамка
  (move_frame_object теперь X+Y) и пивот иконок радара 0x91CF04 сдвигаются на 320-hud_center (весь
  широкоэкранный запас). Проверено на 15T: центр карты ~9.5% ширины экрана, рамка и иконки на месте.
- 11:37: `rel/...--light-a720-fast.apk` пересобран со всеми исправлениями и УСТАНОВЛЕН на 15T (текущая версия
  на телефоне). Fast-режим (фикс. база 0x100000000) стартует; гонка 53-58 FPS при 10-17k D3D9-вызовов/кадр,
  падений нет. Переносимый light тоже актуален.
- 11:47: `rel/NFSMWPC-AndroidEvolved-0.1.13-arm64-v8a.apk` (переносимая, с игрой) пересобрана со всеми
  исправлениями (та же libnfsmw_android.so, что в переносимом light). В gradle исключены `**/*.lnk`
  (в папке игры появился ярлык NIS/«Галерея — ярлык.lnk»; сам файл не трогал).
- Устарел только `rel/...-a720-fast.apk` с игрой (собран до исправлений).
- ГРАБЛИ: APK > 2 ГБ нельзя обновлять инкрементально — `packageDebug` падает с «integer overflow»
  (IncrementalSplitterRunnable). Перед каждой сборкой удалять `app/build/intermediates/incremental/packageDebug`
  и `app/build/outputs/apk/debug` (функция `fresh` в скриптах).

## Важные проверки рантайма (чтобы не удивляться)

- `game_data.h`: на телефоне `nfsmw/speed.exe` должен быть ровно 6 029 312 байт, MZ;
  непустые CARS, FRONTEND, GLOBAL, LANGUAGES, MOVIES, NIS, SOUND, TRACKS.
- `guest_runtime.cpp:580`: ImageBase 0x400000, SizeOfImage 0x678e4e, EP RVA 0x3c4040.
- `abk_module.py` тоже проверяет SizeOfImage/EP speed.exe.
- DXVK берётся из `C:\dev\nfsu2-recompiled\build\dxvk-source` (готов), pcrecomp — `C:\dev\pcrecomp`.
