# Генерация icon.ico (гексагон РЕЗОНАНСА) без внешних библиотек
import struct, math

def draw(size):
    S = 4  # суперсэмплинг
    W = size * S
    px = [[(0, 0, 0, 0)] * W for _ in range(W)]
    cx = cy = W / 2.0
    R = W * 0.46
    def hexp(r, rot=-math.pi/2):
        return [(cx + math.cos(rot + i*math.pi/3)*r, cy + math.sin(rot + i*math.pi/3)*r) for i in range(6)]
    outer = hexp(R)
    inner = hexp(R*0.60)
    core = hexp(R*0.26)
    def pip(px_, py_, poly):
        inside = False
        j = len(poly) - 1
        for i in range(len(poly)):
            xi, yi = poly[i]; xj, yj = poly[j]
            if ((yi > py_) != (yj > py_)) and (px_ < (xj - xi) * (py_ - yi) / (yj - yi + 1e-12) + xi):
                inside = not inside
            j = i
        return inside
    for y in range(W):
        for x in range(W):
            r, g, b, a = 0, 0, 0, 0
            if pip(x, y, outer):
                r, g, b, a = 9, 14, 26, 255
            if pip(x, y, inner):
                r, g, b, a = 14, 26, 46, 255
            if pip(x, y, core):
                r, g, b, a = 79, 216, 255, 255
            px[y][x] = (r, g, b, a)
    # сглаживание: усредняем SxS
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            rr = gg = bb = aa = 0
            for dy in range(S):
                for dx in range(S):
                    c = px[y*S+dy][x*S+dx]
                    rr += c[0]; gg += c[1]; bb += c[2]; aa += c[3]
            n = S*S
            row.append((rr//n, gg//n, bb//n, aa//n))
        out.append(row)
    return out

def ico_bytes(sizes):
    images = []
    for s in sizes:
        img = draw(s)
        # XOR bitmap BGRA снизу вверх
        xor = bytearray()
        for y in range(s-1, -1, -1):
            for x in range(s):
                r, g, b, a = img[y][x]
                xor += bytes((b, g, r, a))
        # AND маска (1 бит на пиксель, строки выровнены по 4 байта)
        stride = ((s + 31) // 32) * 4
        andm = bytearray()
        for y in range(s-1, -1, -1):
            bits = 0
            row = bytearray()
            for x in range(s):
                a = img[y][x][3]
                bits = (bits << 1) | (0 if a > 127 else 1)
                if (x % 8) == 7:
                    row.append(bits); bits = 0
            if s % 8:
                bits <<= (8 - s % 8)
                row.append(bits)
            while len(row) < stride:
                row.append(0)
            andm += row
        hdr = struct.pack('<IiiHHIIiiII', 40, s, s*2, 1, 32, 0, len(xor)+len(andm), 0, 0, 0, 0)
        images.append((s, hdr + bytes(xor) + bytes(andm)))
    out = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16*len(images)
    data = b''
    for s, blob in images:
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        data += blob
    return out + data

open('native/icon.ico', 'wb').write(ico_bytes([16, 32, 48, 64, 128]))
print('icon.ico', len(open('native/icon.ico','rb').read()), 'байт')
