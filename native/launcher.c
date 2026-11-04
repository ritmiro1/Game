/* РЕЗОНАНС — нативный лаунчер для Windows.
   Поднимает локальный HTTP-сервер на 127.0.0.1 со встроенной игрой
   и открывает её в отдельном окне (Edge/Chrome в режиме --app).
   Сборка: см. tools/build-exe.js (zig cc, кросс-компиляция с Linux). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "game_html.h"

static int file_ok(const char *p) {
  DWORD a = GetFileAttributesA(p);
  return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void send_all(SOCKET s, const char *data, int len) {
  int sent = 0;
  while (sent < len) {
    int r = send(s, data + sent, len - sent, 0);
    if (r <= 0) return;
    sent += r;
  }
}

static void serve_client(SOCKET s) {
  char buf[4096];
  int n = (int)recv(s, buf, sizeof(buf) - 1, 0);
  if (n <= 0) return;
  buf[n] = 0;
  if (strncmp(buf, "GET /favicon", 12) == 0) {
    const char *r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    send_all(s, r, (int)strlen(r));
    return;
  }
  char head[320];
  int hl = snprintf(head, sizeof(head),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Connection: close\r\n"
    "Content-Length: %u\r\n"
    "\r\n", GAME_HTML_LEN);
  send_all(s, head, hl);
  unsigned int off = 0;
  while (off < GAME_HTML_LEN) {
    unsigned int chunk = GAME_HTML_LEN - off;
    if (chunk > 16384) chunk = 16384;
    send_all(s, (const char *)GAME_HTML + off, (int)chunk);
    off += chunk;
  }
}

static void fatal(const char *msg) {
  MessageBoxA(NULL, msg, "РЕЗОНАНС", MB_ICONERROR | MB_OK);
  ExitProcess(1);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
  (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) fatal("Не удалось запустить сетевой слой (WSAStartup).");
  SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ls == INVALID_SOCKET) fatal("Не удалось создать слушающий сокет.");

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  addr.sin_port = 0;
  if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0) fatal("Не удалось занять локальный порт.");
  if (listen(ls, 16) != 0) fatal("Не удалось начать прослушивание.");

  int alen = sizeof(addr);
  if (getsockname(ls, (struct sockaddr *)&addr, &alen) != 0) fatal("Не удалось получить порт.");
  int port = (int)ntohs(addr.sin_port);
  char url[160];
  snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);

  /* ищем браузер с поддержкой режима приложения */
  const char *cands[8];
  char p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH], p4[MAX_PATH], p5[MAX_PATH], p6[MAX_PATH];
  const char *pf = getenv("ProgramFiles");
  const char *pf86 = getenv("ProgramFiles(x86)");
  const char *lad = getenv("LOCALAPPDATA");
  int ci = 0;
  if (pf86) { snprintf(p1, MAX_PATH, "%s\\Microsoft\\Edge\\Application\\msedge.exe", pf86); cands[ci++] = p1; }
  if (pf)   { snprintf(p2, MAX_PATH, "%s\\Microsoft\\Edge\\Application\\msedge.exe", pf);   cands[ci++] = p2; }
  if (lad)  { snprintf(p3, MAX_PATH, "%s\\Microsoft\\Edge\\Application\\msedge.exe", lad);  cands[ci++] = p3; }
  if (pf86) { snprintf(p4, MAX_PATH, "%s\\Google\\Chrome\\Application\\chrome.exe", pf86);  cands[ci++] = p4; }
  if (pf)   { snprintf(p5, MAX_PATH, "%s\\Google\\Chrome\\Application\\chrome.exe", pf);    cands[ci++] = p5; }
  if (lad)  { snprintf(p6, MAX_PATH, "%s\\Google\\Chrome\\Application\\chrome.exe", lad);   cands[ci++] = p6; }
  cands[ci] = NULL;

  char exe[MAX_PATH];
  exe[0] = 0;
  for (int i = 0; i < ci; i++) {
    if (file_ok(cands[i])) { strncpy(exe, cands[i], MAX_PATH - 1); exe[MAX_PATH - 1] = 0; break; }
  }

  /* отдельный профиль: гарантирует собственный процесс браузера,
     который живёт ровно столько, сколько открыто окно игры */
  char prof[MAX_PATH];
  prof[0] = 0;
  if (lad) {
    snprintf(prof, MAX_PATH, "%s\\Rezonans", lad);
    CreateDirectoryA(prof, NULL);
    strncat(prof, "\\profile", MAX_PATH - strlen(prof) - 1);
    CreateDirectoryA(prof, NULL);
  }

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  int haveProc = 0;
  if (exe[0]) {
    char args[1024];
    if (prof[0]) {
      snprintf(args, sizeof(args),
        "\"--app=%s\" \"--user-data-dir=%s\" --no-first-run --no-default-browser-check "
        "--disable-features=Translate --window-size=1460,940 --window-position=50,30", url, prof);
    } else {
      snprintf(args, sizeof(args),
        "\"--app=%s\" --no-first-run --no-default-browser-check --disable-features=Translate "
        "--window-size=1460,940 --window-position=50,30", url);
    }
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    if (CreateProcessA(exe, args, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
      haveProc = 1;
      CloseHandle(pi.hThread);
    }
  }
  if (!haveProc) {
    /* запасной путь: системный браузер по умолчанию */
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
  }

  DWORD start = GetTickCount();
  unsigned long served = 0;
  for (;;) {
    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(ls, &rs);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 250000;
    int r = select(0, &rs, NULL, NULL, &tv);
    if (r > 0 && FD_ISSET(ls, &rs)) {
      SOCKET c = accept(ls, NULL, NULL);
      if (c != INVALID_SOCKET) {
        serve_client(c);
        closesocket(c);
        served++;
      }
    }
    if (haveProc) {
      if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
        /* браузер передал запуск уже работающему процессу — открываем системным */
        if (served == 0 && GetTickCount() - start < 4000) {
          ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
          haveProc = 0;
          start = GetTickCount();
          continue;
        }
        break;
      }
    } else {
      if (GetTickCount() - start > (DWORD)8 * 3600 * 1000) break;
    }
  }

  if (haveProc) CloseHandle(pi.hProcess);
  closesocket(ls);
  WSACleanup();
  return 0;
}
