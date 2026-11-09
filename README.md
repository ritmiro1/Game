# «Тихий Залив» — исходный код и материалы

Атмосферный 3D-симулятор рыбалки и исследования. Русский интерфейс, собственный движок
на C99 + raylib 5.5 (OpenGL 3.3), полностью процедурная графика и звук — ни одного
скачанного ассета.

* Игра: `TikhiyZaliv_v1.0.0.exe` (Windows 10/11 x64, один файл, ресурсы внутри)
* Документы: `GDD_TikhiyZaliv.md`, `100_3D_GAME_IDEAS.md`, `100_3D_GAME_IDEAS.json`

## Состав репозитория

```
src/main.c                точка входа, игровой цикл, маршрутизация экранов
src/sim/                  логика мира без графики (world, player_boat, fish, fishing, npc, progress, save)
src/render/               meshes (models.c), рендер-пайплайн (render.c), интерфейс (ui.c)
src/audio/                звук: 3D-панорама, эмбиент-слои, кроссфейд музыки
src/platform/pak.c        чтение упакованных ресурсов (в т.ч. дописанных в .exe)
assets/raw/               исходные PNG/WAV, сгенерированные скриптами
assets/shaders/           8 GLSL-шейдеров (lit, water, sky, blur, composite)
assets/game.pak           архив ресурсов (63 файла)
tools/                    генераторы текстур, звука, упаковщик, каталог идей
tests/                    headless-тесты логики и полный безэкранный прогон игры
docs/                     ГДД
vendor/raylib/            raylib 5.5 + raygui 4.0 (исходники)
```

## Сборка

Нужны `gcc`/`clang` и (для Windows) `zig`. Ресурсы пересобираются скриптами:

```bash
python3 tools/gen_textures.py      # 23 PNG в assets/raw
python3 tools/gen_audio.py         # 31 WAV в assets/raw
python3 tools/pack_assets.py       # assets/game.pak

# Windows x64 (кросс-компиляция, статическая линковка)
zig cc -target x86_64-windows-gnu -std=gnu11 -O2 \
  -Ivendor/raylib/src -Ivendor/raylib -Isrc \
  src/main.c src/render/*.c src/audio/*.c src/platform/*.c src/sim/*.c \
  vendor_raylib_objects/*.o -o build/game.exe \
  -lopengl32 -lgdi32 -lwinmm -lshell32 -luser32 -lole32 -lm

# ресурсы дописываются в конец исполняемого файла
cat build/game.exe assets/game.pak > build/TikhiyZaliv_v1.0.0.exe
```

Объектные файлы raylib собираются тем же `zig cc` из `vendor/raylib/src/*.c`
(см. список в `docs/BUILD.md`).

## Тесты

```bash
gcc -O2 -std=c99 -Wall -Wextra -Isrc/sim tests/sim_test.c src/sim/*.c -lm -o /tmp/simtest
/tmp/simtest                                        # 19 проверок логики

gcc -std=gnu11 -O1 -g -fsanitize=address,undefined -Ivendor/raylib/src -Ivendor/raylib -Isrc \
  tests/stub/*.c tests/ui_smoke.c src/render/*.c src/audio/audio.c src/platform/pak.c src/sim/*.c \
  -lm -o /tmp/uismoke && /tmp/uismoke                # полный прогон без GPU
```

## Управление

WASD — движение, мышь — обзор, Shift — бег, C — присесть, Пробел — прыжок,
ЛКМ — заброс/подсечка/подмотка, колесо — фрикцион, E — действие и диалог,
F — лодка, L — фонарь, N — сонар, J — журнал, Esc — пауза.

## Сохранения

`%APPDATA%\TikhiyZaliv\save.json`
