import re, sys

def protos(path, macro="RLAPI"):
    src = open(path, encoding="utf-8", errors="ignore").read()
    out = []
    for m in re.finditer(macro + r'\s+([^;{]*?\([^;{]*?\))\s*;', src):
        decl = " ".join(m.group(1).split())
        out.append(decl)
    return out

MANUAL = {
 "int GetScreenWidth": "return 1400;",
 "int GetScreenHeight": "return 900;",
 "float GetFrameTime": "g_frames++; return 1.0f/60.0f;",
 "double GetTime": "return (double)g_frames/60.0;",
 "bool WindowShouldClose": "return g_frames > 900;",
 "bool IsWindowResized": "return false;",
 "Vector2 GetMousePosition": "return (Vector2){ 60.0f, 60.0f };",
 "Vector2 GetMouseDelta": "return (Vector2){ 0.0f, 0.0f };",
 "float GetMouseWheelMove": "return 0.0f;",
 "bool IsCursorHidden": "return false;",
 "Font GetFontDefault": "{ static Font f; static int init = 0; if (!init) { init = 1; f.baseSize = 12; f.glyphCount = 1200; f.glyphPadding = 0; f.glyphs = (GlyphInfo *)calloc(1200, sizeof(GlyphInfo)); f.recs = (Rectangle *)calloc(1200, sizeof(Rectangle)); for (int i = 0; i < 1200; i++) { f.glyphs[i].value = i; f.glyphs[i].advanceX = 8; f.glyphs[i].offsetX = 0; f.glyphs[i].offsetY = 0; } f.texture.id = 1; f.texture.width = 1; f.texture.height = 1; } return f; }",
 "Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing)":
   "return (Vector2){ (float)(text?strlen(text):0) * fontSize * 0.5f, fontSize };",
 "Shader LoadShader": "return (Shader){ 0 };",
 "Shader LoadShaderFromMemory": "return (Shader){ 0 };",
 "int GetShaderLocation": "(void)uniformName; return -1;",
 "int GetShaderLocationAttrib": "return -1;",
 "Texture2D LoadTextureFromImage": "return (Texture2D){ 0 };",
 "Texture2D LoadTexture": "return (Texture2D){ 0 };",
 "RenderTexture2D LoadRenderTexture": "return (RenderTexture2D){ 0 };",
 "Model LoadModelFromMesh": "return (Model){ 0 };",
 "Model LoadModel": "return (Model){ 0 };",
 "Mesh GenMeshCube": "return (Mesh){ 0 };",
 "Image GenImageColor": "return (Image){ 0 };",
 "Wave LoadWaveFromMemory": "return (Wave){ 0 };",
 "Sound LoadSoundFromWave": "return (Sound){ 0 };",
 "Music LoadMusicStreamFromMemory": "return (Music){ 0 };",
 "char *TextFormat": "static char b[1024]; va_list a; va_start(a, text); vsnprintf(b, sizeof b, text, a); va_end(a); return b;",
 "void TraceLog": "(void)logLevel; (void)text;",
 "Material LoadMaterialDefault":
   "{ Material m = { 0 }; m.maps = (MaterialMap *)calloc(12, sizeof(MaterialMap)); return m; }",
 "void UnloadMaterial": "{ if (material.maps) free(material.maps); }",
 "void UnloadModel": "((void)0);",
 "ModelAnimation *LoadModelAnimations": "return NULL;",
}

MANUAL.update({
 "bool IsWindowReady": "return true;",
 "bool IsAudioDeviceReady": "return true;",
 "const char *GetApplicationDirectory": "return \"./\";",
 "int GetMonitorWidth": "return 1920;",
 "int GetMonitorHeight": "return 1080;",
 "int GetCurrentMonitor": "return 0;",
 "int GetCodepointNext":
   "{ unsigned char c0 = (unsigned char)text[0]; int sz = 1; int cp = c0; if (c0 >= 0xF0) { sz = 4; cp = c0 & 0x07; } else if (c0 >= 0xE0) { sz = 3; cp = c0 & 0x0F; } else if (c0 >= 0xC0) { sz = 2; cp = c0 & 0x1F; } for (int i = 1; i < sz; i++) cp = (cp << 6) | ((unsigned char)text[i] & 0x3F); if (codepointSize) *codepointSize = sz; return cp; }",
 "int GetCodepointCount":
   "{ int n = 0; int i = 0; while (text && text[i]) { unsigned char c0 = (unsigned char)text[i]; i += (c0 >= 0xF0) ? 4 : (c0 >= 0xE0) ? 3 : (c0 >= 0xC0) ? 2 : 1; n++; } return n; }",
 "int TextLength": "return text ? (int)strlen(text) : 0;",
 "const char *TextSubtext":
   "{ static char b[4096]; int n = (int)strlen(text); if (position >= n) position = n; int len = (length < 0) ? (n - position) : length; if (len > 4095) len = 4095; memcpy(b, text + position, len); b[len] = 0; return b; }",
 "const char *CodepointToUTF8":
   "{ static char b[8]; int n = 0; unsigned int cp = (unsigned int)codepoint; if (cp < 0x80) b[n++] = (char)cp; else if (cp < 0x800) { b[n++] = (char)(0xC0 | (cp >> 6)); b[n++] = (char)(0x80 | (cp & 0x3F)); } else if (cp < 0x10000) { b[n++] = (char)(0xE0 | (cp >> 12)); b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[n++] = (char)(0x80 | (cp & 0x3F)); } else { b[n++] = (char)(0xF0 | (cp >> 18)); b[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[n++] = (char)(0x80 | (cp & 0x3F)); } if (utf8Size) *utf8Size = n; b[n] = 0; return b; }",
})

HEAD = '''// Автоматически сгенерированные заглушки raylib для безэкранных тестов.
#include "raylib.h"
#include "rlgl.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_frames = 0;
'''

def main():
    decls = protos("vendor/raylib/src/raylib.h") + protos("vendor/raylib/src/rlgl.h")
    lines = [HEAD]
    seen = set()
    for d in decls:
        # name
        nm = re.match(r'(.*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)$', d, re.S)
        if not nm: continue
        ret, name, args = nm.group(1).strip(), nm.group(2), nm.group(3).strip()
        if name in seen: continue
        seen.add(name)
        body = "((void)0);" if ret in ("void","") else "return (%s){ 0 };" % ret
        key = None
        for k, v in MANUAL.items():
            if name in k and k.startswith(ret) and v:
                body = v
                break
        if "..." in args:
            body = "((void)0);" if ret in ("void","") else "return (%s){ 0 };" % ret
        lines.append("%s %s(%s) { %s }\n" % (ret if ret else "void", name, args if args else "void", body))
    open("tests/stub/raylib_stub.c", "w").write("".join(lines))
    print("stubs:", len(seen))

main()
