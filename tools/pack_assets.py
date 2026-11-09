#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сборка архива ресурсов assets/game.pak (индекс + данные, футер 18 байт)."""
import os, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIRS = [
    (os.path.join(ROOT, "assets", "raw"), ""),
    (os.path.join(ROOT, "assets", "shaders"), ""),
    (os.path.join(ROOT, "fonts"), ""),
]
OUT = os.path.join(ROOT, "assets", "game.pak")
MAGIC = b"TZP1"

def collect():
    items = []
    for d, _ in SRC_DIRS:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            p = os.path.join(d, name)
            if not os.path.isfile(p):
                continue
            if name.endswith((".py", ".md", ".pyc")):
                continue
            items.append((name, p))
    return items

def main():
    items = collect()
    if not items:
        print("Нет ресурсов для упаковки"); return 1
    payload = bytearray()
    index = bytearray()
    for name, path in items:
        data = open(path, "rb").read()
        off = len(payload)
        payload += data
        nb = name.encode("utf-8")
        index += struct.pack("<H", len(nb)) + nb + struct.pack("<II", off, len(data))
    idx_off = len(payload)
    body = payload + index
    footer = MAGIC + struct.pack("<III", idx_off, len(index), len(items))
    with open(OUT, "wb") as f:
        f.write(body)
        f.write(footer)
    print(f"Упаковано {len(items)} файлов, {len(body)/1048576:.2f} МБ -> {OUT}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
