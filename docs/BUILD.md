# Сборка «Тихого Залива» с нуля

## 1. Инструменты

* `python3` (3.9+) и пакеты `numpy` — для генераторов текстур и звука
* `gcc` или `clang` — для нативных тестов логики
* `zig` 0.13+ — кросс-компилятор Windows x64 (`zig cc`), работает как drop-in `cc`

## 2. Ресурсы

```bash
python3 tools/gen_textures.py     # 23 PNG (бесшовные текстуры и карты нормалей)
python3 tools/gen_audio.py        # 31 WAV (16 бит, 44.1 кГц)
python3 tools/pack_assets.py      # assets/game.pak — 63 файла
python3 tools/make_ideas.py       # 100_3D_GAME_IDEAS.json/.md
```

## 3. Объектные файлы raylib

```bash
mkdir -p vendor_raylib_objects
for f in rcamera rcore rdraw rglfw rgestures rmodels rshapes rtext rtextures rutils raudio; do
  zig cc -target x86_64-windows-gnu -O2 -std=gnu11 \
    -Ivendor/raylib/src -Ivendor/raylib/src/external \
    -c vendor/raylib/src/$f.c -o vendor_raylib_objects/$f.o
done
```

## 4. Исполняемый файл

```bash
zig cc -target x86_64-windows-gnu -std=gnu11 -O2 \
  -Ivendor/raylib/src -Ivendor/raylib -Isrc \
  src/main.c src/render/models.c src/render/render.c src/render/ui.c \
  src/audio/audio.c src/platform/pak.c src/sim/*.c \
  vendor_raylib_objects/*.o -o build/game.exe \
  -lopengl32 -lgdi32 -lwinmm -lshell32 -luser32 -lole32 -lm
```

## 5. Упаковка ресурсов внутрь .exe

`pak.c` ищет архив в конце собственного исполняемого файла (футер `TZP1`, 16 байт:
magic, смещение индекса, размер индекса, число файлов). Достаточно дописать архив:

```bash
cat build/game.exe assets/game.pak > TikhiyZaliv_v1.0.0.exe
```

Порядок поиска при запуске: (1) архив в конце `.exe`, (2) `game.pak` рядом,
(3) `assets/game.pak`, (4) режим разработки — чтение напрямую из `assets/raw`.

## 6. Проверка

```bash
python3 - <<'EOF'
import struct
d = open('TikhiyZaliv_v1.0.0.exe','rb').read()
foot = d[-16:]
assert foot[:4] == b'TZP1'
off, size, count = struct.unpack('<III', foot[4:])
assert off + size == len(d) - 16
print('PE:', d[:2], 'файлов в архиве:', count)
EOF
```

## 7. Зависимости сборки

Исполняемый файл статически слинкован с `zig`-овским mingw/ucrt рантаймом.
Импортируются только системные библиотеки Windows: `KERNEL32`, `USER32`, `GDI32`,
`WINMM`, `SHELL32` и `api-ms-win-crt-*` (Universal C Runtime входит в Windows 10/11).
OpenGL загружается во время выполнения через `opengl32.dll`.
