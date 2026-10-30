/*
 * BINDER (RTBind) — заявки в работе / выполнены + бинды Ctrl+F1..F12
 * Разработчик: kondrashov-m  (kondrashov-m.ru)
 *
 * Чистый WinAPI, без CRT и без зависимостей: Windows 7 / 8 / 10 / 11, x86 и x64.
 * Данные хранятся рядом с exe: rtbind_data.txt, rtbind_history.txt, папка binds\.
 */
#include "w32.h"

/* ---------------- без CRT ---------------- */
void *memset(void *d, int c, unsigned n) { volatile BYTE *p = d; while (n--) *p++ = (BYTE)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { volatile BYTE *p = d; const BYTE *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, unsigned n) {
  volatile BYTE *p = d; const BYTE *q = s;
  if (p < q) while (n--) *p++ = *q++; else { p += n; q += n; while (n--) *--p = *--q; }
  return d;
}
int _fltused = 0;

static int wlen(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
static void wcopy(WCHAR *d, const WCHAR *s, int max) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static int weq(const WCHAR *a, const WCHAR *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void wcat(WCHAR *d, const WCHAR *s, int max) { int n = wlen(d); wcopy(d + n, s, max - n); }
static void wtrim(WCHAR *s) {
  int n = wlen(s), a = 0;
  while (a < n && (s[a] == ' ' || s[a] == '\t')) a++;
  while (n > a && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
  int i; for (i = 0; i < n - a; i++) s[i] = s[a + i]; s[i] = 0;
}
static void wclean(WCHAR *s) { for (; *s; s++) if (*s == '|' || *s == '\r' || *s == '\n' || *s == '\t') *s = ' '; }

/* 64/32 деление через инструкцию div (без _aulldiv) */
static DWORD div6432(DWORD hi, DWORD lo, DWORD d) {
  DWORD q, r;
  __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "rm"(d));
  return q;
}
static DWORD now_unix(void) {
  FILETIME ft; GetSystemTimeAsFileTime(&ft);
  DWORD d = 10000000, qh = ft.dwHighDateTime / d, r = ft.dwHighDateTime % d;
  DWORD ql = div6432(r, ft.dwLowDateTime, d);
  ULONGLONG s = ((ULONGLONG)qh << 32) | ql;
  return (DWORD)(s - 11644473600ULL);
}
static void unix_local(DWORD u, SYSTEMTIME *st) {
  ULONGLONG s = (ULONGLONG)u + 11644473600ULL;
  DWORD sh = (DWORD)(s >> 32), sl = (DWORD)s;
  ULONGLONG v = (ULONGLONG)sl * 10000000UL + (((ULONGLONG)sh * 10000000UL) << 32);
  FILETIME ft, lft; ft.dwLowDateTime = (DWORD)v; ft.dwHighDateTime = (DWORD)(v >> 32);
  FileTimeToLocalFileTime(&ft, &lft); FileTimeToSystemTime(&lft, st);
}

/* ---------------- данные ---------------- */
#define MAXT 400
typedef struct {
  int status;               /* 0 — в работе, 1 — выполнена, 2 — на уточнении (runStart = с какого момента) */
  DWORD order, elapsed, runStart, created, doneAt;
  WCHAR num[32];
  WCHAR note[120];
} Ticket;

static Ticket T[MAXT];
static int NT;
static DWORD SEQ;

static WCHAR g_dir[520], g_data[560], g_tmp[560], g_hist[560], g_binds[560], g_set[560];

static DWORD tk_elapsed(const Ticket *t) {
  if (t->status == 0) { DWORD n = now_unix(); return t->elapsed + (n > t->runStart ? n - t->runStart : 0); }
  return t->elapsed;
}

static WCHAR SB[MAXT * 220 + 256];
static char UB[(MAXT * 220 + 256) * 3];

static BOOL write_utf8(const WCHAR *path, const WCHAR *w, int append) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen(w), UB, sizeof(UB), NULL, NULL);
  HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  if (append) SetFilePointer(f, 0, NULL, FILE_END);
  DWORD wr = 0; WriteFile(f, UB, (DWORD)n, &wr, NULL); CloseHandle(f);
  return wr == (DWORD)n;
}

static void save_data(void) {
  WCHAR line[400];
  SB[0] = 0;
  wsprintfW(line, L"RTBIND1\r\n%lu|%lu\r\n", SEQ, now_unix()); wcat(SB, line, sizeof(SB) / 2);
  for (int i = 0; i < NT; i++) {
    Ticket *t = &T[i];
    wsprintfW(line, L"%d|%lu|%lu|%lu|%lu|%lu|%s|%s\r\n", t->status, t->order, t->elapsed, t->runStart, t->created, t->doneAt, t->num, t->note);
    wcat(SB, line, sizeof(SB) / 2);
  }
  if (write_utf8(g_tmp, SB, 0)) MoveFileExW(g_tmp, g_data, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static DWORD parse_u(const WCHAR **p) {
  DWORD v = 0; while (**p >= '0' && **p <= '9') { v = v * 10 + (**p - '0'); (*p)++; }
  if (**p == '|') (*p)++;
  return v;
}
static void parse_s(const WCHAR **p, WCHAR *out, int max) {
  int i = 0; while (**p && **p != '|' && **p != '\r' && **p != '\n') { if (i < max - 1) out[i++] = **p; (*p)++; }
  out[i] = 0; if (**p == '|') (*p)++;
}

static char RB[1 << 20];
static WCHAR RW[1 << 20];

static int read_text(const WCHAR *path, WCHAR *out, int maxw) {
  out[0] = 0;
  HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (f == INVALID_HANDLE_VALUE) return 0;
  DWORD rd = 0; ReadFile(f, RB, sizeof(RB) - 2, &rd, NULL); CloseHandle(f);
  RB[rd] = 0; RB[rd + 1] = 0;
  int n;
  if (rd >= 2 && (BYTE)RB[0] == 0xFF && (BYTE)RB[1] == 0xFE) {
    n = (int)(rd - 2) / 2; if (n > maxw - 1) n = maxw - 1;
    memcpy(out, RB + 2, (unsigned)n * 2);
  } else {
    const char *s = RB; int len = (int)rd;
    if (rd >= 3 && (BYTE)RB[0] == 0xEF && (BYTE)RB[1] == 0xBB && (BYTE)RB[2] == 0xBF) { s += 3; len -= 3; }
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, out, maxw - 1);
    if (n <= 0 && len > 0) n = MultiByteToWideChar(1251, 0, s, len, out, maxw - 1);
    if (n < 0) n = 0;
  }
  out[n] = 0;
  return n;
}

static void load_data(void) {
  NT = 0; SEQ = 0;
  if (read_text(g_data, RW, sizeof(RW) / 2) <= 0) return;
  const WCHAR *p = RW;
  if (!(p[0] == 'R' && p[1] == 'T' && p[2] == 'B')) return;
  while (*p && *p != '\n') p++; if (*p) p++;
  SEQ = parse_u(&p);
  parse_u(&p); /* время последнего сохранения (не используется) */
  while (*p && *p != '\n') p++; if (*p) p++;
  DWORD now = now_unix();
  while (*p && NT < MAXT) {
    Ticket *t = &T[NT];
    memset(t, 0, sizeof(*t));
    if (!(*p >= '0' && *p <= '9')) { while (*p && *p != '\n') p++; if (*p) p++; continue; }
    t->status = (int)parse_u(&p); t->order = parse_u(&p); t->elapsed = parse_u(&p);
    t->runStart = parse_u(&p); t->created = parse_u(&p); t->doneAt = parse_u(&p);
    parse_s(&p, t->num, 32); parse_s(&p, t->note, 120);
    while (*p && *p != '\n') p++; if (*p) p++;
    if (!t->num[0]) continue;
    if (t->status != 1 && t->status != 2) t->status = 0;
    /* время в работе = накопленное + (текущее системное время - момент запуска таймера);
       программа может быть закрыта — таймер всё равно идёт */
    if (t->status == 0 && (t->runStart == 0 || t->runStart > now)) t->runStart = now;
    if (t->order > SEQ) SEQ = t->order;
    NT++;
  }
}

static int find_num(const WCHAR *num) { for (int i = 0; i < NT; i++) if (weq(T[i].num, num)) return i; return -1; }

/* ---------------- UI ---------------- */
static HINSTANCE g_inst;
static HWND g_wnd, g_eNum, g_eNote, g_bw, g_bList, g_bEdit;
static WNDPROC g_oldEdit;
static int g_dpi = 96;
#define S(x) MulDiv((x), g_dpi, 96)

static HFONT fTitle, fBadge, fH, fNum, fText, fSmall, fMono, fBtn, fEdit;
static HBRUSH brEdit, brBg;
static HCURSOR curArrow, curHand;

#define C_BG RGB(15, 14, 16)
#define C_PANEL RGB(21, 20, 24)
#define C_PANEL_BR RGB(40, 39, 45)
#define C_CARD RGB(30, 29, 34)
#define C_CARD_H RGB(37, 36, 42)
#define C_CARD_BR RGB(50, 49, 56)
#define C_TEXT RGB(255, 255, 255)
#define C_MUTED RGB(146, 146, 146)
#define C_DIM RGB(95, 95, 102)
#define C_ACC RGB(0, 163, 255)
#define C_ACC2 RGB(120, 90, 240)
#define C_SEC RGB(79, 179, 255)
#define C_GREEN RGB(100, 245, 210)
#define C_RED RGB(255, 92, 110)

static int g_scroll[3];
#define C_AMB RGB(255, 181, 71)
static int col_of(int st) { return st == 0 ? 0 : st == 2 ? 1 : 2; }
static const int COL_ST[3] = {0, 2, 1};
static int g_focusNum, g_focusNote;
static int g_hkOk;
static WCHAR g_toast[160];
static DWORD g_toastUntil;

/* hover / press */
enum { H_NONE, H_ADD, H_BINDS, H_CLEAR, H_CARD, H_CARD_B1, H_CARD_B2, H_CARD_B3, H_SITE, H_REMIND };
typedef struct { int kind, idx; } Hit;
static Hit g_hover, g_press;
static int g_dragging, g_dragIdx, g_dragDX, g_dragDY;
static POINT g_pressPt, g_mouse;
static int g_tracking;

static void open_edit(int i);
static void open_settings(void);
static void set_status(int i, int st);
static void remind_tick(void);
static int g_remOn = 1, g_remH = 19, g_remM = 30;
static HWND g_ew;
static void edit_close(void);
static void toast(const WCHAR *s) { wcopy(g_toast, s, 160); g_toastUntil = GetTickCount() + 2600; InvalidateRect(g_wnd, NULL, 0); }

static COLORREF mix(COLORREF a, COLORREF b, int t, int tmax) {
  int r = GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t / tmax;
  int g = GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t / tmax;
  int bl = GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t / tmax;
  return RGB(r, g, bl);
}
static int pulse(int period) { int p = (int)(GetTickCount() % (DWORD)period), h = period / 2; return p < h ? p : period - p; } /* 0..period/2 */

static void rrect(HDC dc, int l, int t, int r, int b, int rad, COLORREF fill, int border, COLORREF bc) {
  SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, fill);
  if (border) { SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, bc); }
  else SelectObject(dc, GetStockObject(NULL_PEN));
  RoundRect(dc, l, t, r + (border ? 0 : 1), b + (border ? 0 : 1), rad, rad);
}
static void grad_h(HDC dc, int l, int t, int r, int b, COLORREF c1, COLORREF c2) {
  TRIVERTEX v[2] = {{l, t, (WORD)(GetRValue(c1) << 8), (WORD)(GetGValue(c1) << 8), (WORD)(GetBValue(c1) << 8), 0},
                    {r, b, (WORD)(GetRValue(c2) << 8), (WORD)(GetGValue(c2) << 8), (WORD)(GetBValue(c2) << 8), 0}};
  GRADIENT_RECT gr = {0, 1};
  GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
}
static void grad_rr(HDC dc, int l, int t, int r, int b, int rad, COLORREF c1, COLORREF c2) {
  HRGN rg = CreateRoundRectRgn(l, t, r + 1, b + 1, rad, rad);
  SelectClipRgn(dc, rg); grad_h(dc, l, t, r, b, c1, c2); SelectClipRgn(dc, NULL); DeleteObject(rg);
}
static void glow(HDC dc, int cx, int cy, int rx, int ry, COLORREF c, COLORREF bg) {
  /* мягкое свечение: 4 треугольника от центра к краям */
  TRIVERTEX v[5];
  int px[5] = {cx, cx - rx, cx + rx, cx + rx, cx - rx}, py[5] = {cy, cy - ry, cy - ry, cy + ry, cy + ry};
  for (int i = 0; i < 5; i++) {
    COLORREF k = i == 0 ? c : bg;
    v[i].x = px[i]; v[i].y = py[i]; v[i].Red = (WORD)(GetRValue(k) << 8); v[i].Green = (WORD)(GetGValue(k) << 8); v[i].Blue = (WORD)(GetBValue(k) << 8); v[i].Alpha = 0;
  }
  GRADIENT_TRIANGLE tr[4] = {{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1}};
  GradientFill(dc, v, 5, tr, 4, GRADIENT_FILL_TRIANGLE);
}
static void text(HDC dc, const WCHAR *s, HFONT f, COLORREF c, int l, int t, int r, int b, UINT fl) {
  RECT rc = {l, t, r, b}; SelectObject(dc, f); SetTextColor(dc, c);
  DrawTextW(dc, s, -1, &rc, fl | DT_NOPREFIX);
}
static int text_w(HDC dc, const WCHAR *s, HFONT f) { SIZE sz; SelectObject(dc, f); GetTextExtentPoint32W(dc, s, wlen(s), &sz); return sz.cx; }
static void line(HDC dc, int x1, int y1, int x2, int y2, int w, COLORREF c) {
  HPEN p = CreatePen(PS_SOLID, w, c); HGDIOBJ o = SelectObject(dc, p);
  MoveToEx(dc, x1, y1, NULL); LineTo(dc, x2, y2); SelectObject(dc, o); DeleteObject(p);
}
static void circle(HDC dc, int cx, int cy, int r, COLORREF c) {
  SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, c); SelectObject(dc, GetStockObject(NULL_PEN));
  Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1);
}
static void fmt_dur(DWORD s, WCHAR *out) {
  DWORD h = s / 3600, m = (s / 60) % 60, sec = s % 60;
  if (h) wsprintfW(out, L"%lu:%02lu:%02lu", h, m, sec); else wsprintfW(out, L"%02lu:%02lu", m, sec);
}
static void fmt_clock(DWORD u, WCHAR *out) { SYSTEMTIME st; unix_local(u, &st); wsprintfW(out, L"%02d:%02d", st.wHour, st.wMinute); }

/* ---------- раскладка ---------- */
typedef struct {
  int W, H, M;
  RECT header, numBox, noteBox, addBtn, bindsBtn, remindBtn, site;
  RECT col[3], area[3], clearBtn;
  int cardH, gap;
  int idx[3][MAXT], cnt[3];
} Layout;
static Layout L;

static void build_lists(void) {
  L.cnt[0] = L.cnt[1] = L.cnt[2] = 0;
  for (int i = 0; i < NT; i++) {
    int c = col_of(T[i].status), k = L.cnt[c]++;
    while (k > 0 && T[L.idx[c][k - 1]].order < T[i].order) { L.idx[c][k] = L.idx[c][k - 1]; k--; }
    L.idx[c][k] = i;
  }
}

static void do_layout(void) {
  RECT rc; GetClientRect(g_wnd, &rc);
  L.W = rc.right; L.H = rc.bottom; L.M = S(22);
  int M = L.M, W = L.W, H = L.H;
  L.header = (RECT){M, S(16), W - M, S(64)};
  L.bindsBtn = (RECT){W - M - S(178), S(22), W - M, S(58)};
  L.remindBtn = (RECT){L.bindsBtn.left - S(10) - S(190), S(22), L.bindsBtn.left - S(10), S(58)};
  int y = S(80), h = S(46);
  L.addBtn = (RECT){W - M - S(170), y, W - M, y + h};
  L.numBox = (RECT){M, y, M + S(210), y + h};
  L.noteBox = (RECT){M + S(222), y, L.addBtn.left - S(12), y + h};
  int top = y + h + S(20), bottom = H - S(44);
  int gapc = S(16), cw = (W - 2 * M - 2 * gapc) / 3;
  for (int c = 0; c < 3; c++) L.col[c] = (RECT){M + c * (cw + gapc), top, M + c * (cw + gapc) + cw, bottom};
  L.col[2].right = W - M;
  for (int c = 0; c < 3; c++)
    L.area[c] = (RECT){L.col[c].left + S(14), L.col[c].top + S(62), L.col[c].right - S(14), L.col[c].bottom - S(12)};
  L.clearBtn = (RECT){L.col[2].right - S(104), L.col[2].top + S(15), L.col[2].right - S(14), L.col[2].top + S(43)};
  L.site = (RECT){M, H - S(34), M + S(360), H - S(10)};
  L.cardH = S(98); L.gap = S(10);
  build_lists();
  for (int c = 0; c < 3; c++) {
    int content = L.cnt[c] * (L.cardH + L.gap) - L.gap, vis = L.area[c].bottom - L.area[c].top;
    int mx = content > vis ? content - vis : 0;
    if (g_scroll[c] > mx) g_scroll[c] = mx;
    if (g_scroll[c] < 0) g_scroll[c] = 0;
  }
}

static RECT card_rect(int c, int k) {
  RECT r = L.area[c];
  r.top = L.area[c].top + k * (L.cardH + L.gap) - g_scroll[c];
  r.bottom = r.top + L.cardH;
  return r;
}
static RECT card_b1(RECT r) { int s = S(30); return (RECT){r.right - S(14) - s, r.top + S(14), r.right - S(14), r.top + S(14) + s}; }
static RECT card_b2(RECT r) { RECT b = card_b1(r); int s = S(30) + S(8); b.left -= s; b.right -= s; return b; }
static RECT card_b3(RECT r) { RECT b = card_b2(r); int s = S(30) + S(8); b.left -= s; b.right -= s; return b; }
static int in_rect(RECT r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

static Hit hit_test(int x, int y) {
  Hit h = {H_NONE, -1};
  if (in_rect(L.addBtn, x, y)) { h.kind = H_ADD; return h; }
  if (in_rect(L.bindsBtn, x, y)) { h.kind = H_BINDS; return h; }
  if (in_rect(L.remindBtn, x, y)) { h.kind = H_REMIND; return h; }
  if (L.cnt[2] && in_rect(L.clearBtn, x, y)) { h.kind = H_CLEAR; return h; }
  if (in_rect(L.site, x, y)) { h.kind = H_SITE; return h; }
  for (int c = 0; c < 3; c++) {
    if (!in_rect(L.area[c], x, y)) continue;
    for (int k = 0; k < L.cnt[c]; k++) {
      RECT r = card_rect(c, k);
      if (!in_rect(r, x, y)) continue;
      h.idx = L.idx[c][k];
      if (in_rect(card_b1(r), x, y)) h.kind = H_CARD_B1;
      else if (in_rect(card_b2(r), x, y)) h.kind = H_CARD_B2;
      else if (T[h.idx].status != 1 && in_rect(card_b3(r), x, y)) h.kind = H_CARD_B3;
      else h.kind = H_CARD;
      return h;
    }
  }
  return h;
}

/* ---------- отрисовка ---------- */
static void draw_check(HDC dc, RECT b, COLORREF c) {
  int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2, w = S(2);
  line(dc, cx - S(6), cy, cx - S(2), cy + S(4), w, c); line(dc, cx - S(2), cy + S(4), cx + S(6), cy - S(5), w, c);
}
static void draw_x(HDC dc, RECT b, COLORREF c) {
  int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2, d = S(5), w = S(2);
  line(dc, cx - d, cy - d, cx + d + 1, cy + d + 1, w, c); line(dc, cx - d, cy + d, cx + d + 1, cy - d - 1, w, c);
}
static void draw_back(HDC dc, RECT b, COLORREF c) {
  int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2, w = S(2);
  line(dc, cx + S(6), cy, cx - S(6), cy, w, c); line(dc, cx - S(6), cy, cx - S(2), cy - S(4), w, c); line(dc, cx - S(6), cy, cx - S(2), cy + S(4), w, c);
}

static void draw_q(HDC dc, RECT b, COLORREF c) { text(dc, L"?", fH, c, b.left, b.top, b.right, b.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER); }
static void fmt_wait(DWORD sec, WCHAR *out) {
  DWORD m = sec / 60, h = m / 60;
  if (h) wsprintfW(out, L"%lu ч %lu мин", h, m % 60); else wsprintfW(out, L"%lu мин", m);
}

static void draw_card(HDC dc, RECT r, int i, int hover, int hb, int ghost) {
  Ticket *t = &T[i];
  int work = t->status == 0, wait = t->status == 2;
  COLORREF fill = ghost ? C_CARD_H : (hover ? C_CARD_H : C_CARD);
  COLORREF br = ghost ? C_ACC : (hover ? RGB(70, 69, 78) : C_CARD_BR);
  rrect(dc, r.left, r.top, r.right, r.bottom, S(14), fill, 1, br);
  /* полоска-индикатор слева */
  int pv = pulse(1400);
  COLORREF bar = work ? mix(C_ACC, C_ACC2, pv, 700) : wait ? C_AMB : C_GREEN;
  rrect(dc, r.left + S(7), r.top + S(14), r.left + S(11), r.bottom - S(14), S(4), bar, 0, 0);

  int x = r.left + S(24);
  RECT b1 = card_b1(r), b2 = card_b2(r), b3 = card_b3(r);
  int numRight = (t->status == 1 ? b2.left : b3.left) - S(8);
  text(dc, t->num, fNum, C_TEXT, x, r.top + S(10), numRight, r.top + S(46), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
  WCHAR sub[200], tm[32], buf[80];
  if (t->note[0]) wcopy(sub, t->note, 200); else wcopy(sub, t->status == 1 ? L"" : L"без описания", 200);
  /* таймер-пилюля справа внизу */
  fmt_dur(tk_elapsed(t), tm);
  int tw = text_w(dc, tm, fMono) + S(t->status == 1 ? 22 : 34);
  RECT pill = {r.right - S(14) - tw, r.bottom - S(14) - S(28), r.right - S(14), r.bottom - S(14)};
  int cy = (pill.top + pill.bottom) / 2;
  if (work) {
    COLORREF pb = mix(RGB(0, 90, 145), C_ACC, pv, 700);
    rrect(dc, pill.left, pill.top, pill.right, pill.bottom, S(26), RGB(14, 32, 46), 1, pb);
    int cx = pill.left + S(14);
    circle(dc, cx, cy, S(3) + pv * S(3) / 700, mix(RGB(14, 32, 46), RGB(0, 110, 170), 700 - pv, 700));
    circle(dc, cx, cy, S(3), C_ACC);
    text(dc, tm, fMono, C_TEXT, pill.left + S(24), pill.top, pill.right - S(8), pill.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  } else if (wait) {
    rrect(dc, pill.left, pill.top, pill.right, pill.bottom, S(26), RGB(42, 31, 14), 1, RGB(140, 100, 40));
    int cx = pill.left + S(14);
    rrect(dc, cx - S(4), cy - S(5), cx - S(2), cy + S(5), 0, C_AMB, 0, 0);
    rrect(dc, cx + S(1), cy - S(5), cx + S(3), cy + S(5), 0, C_AMB, 0, 0);
    text(dc, tm, fMono, RGB(235, 225, 210), pill.left + S(24), pill.top, pill.right - S(8), pill.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  } else {
    rrect(dc, pill.left, pill.top, pill.right, pill.bottom, S(26), RGB(36, 35, 40), 1, RGB(58, 57, 64));
    text(dc, tm, fMono, C_MUTED, pill.left, pill.top, pill.right, pill.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
  /* нижняя строка: описание + время */
  WCHAR clk[16];
  COLORREF bc;
  if (work) { fmt_clock(t->created, clk); wsprintfW(buf, L"взята в %s", clk); bc = C_DIM; }
  else if (wait) { DWORD n = now_unix(); WCHAR wt[32]; fmt_wait(n > t->runStart ? n - t->runStart : 0, wt); wsprintfW(buf, L"ждёт %s", wt); bc = RGB(200, 150, 70); }
  else { fmt_clock(t->doneAt, clk); wsprintfW(buf, L"выполнена в %s", clk); bc = RGB(80, 190, 165); }
  int bw = text_w(dc, buf, fSmall);
  int bx = pill.left - S(12) - bw;
  if (bx < x + S(40)) bx = x + S(40);
  text(dc, buf, fSmall, bc, bx, pill.top, pill.left - S(10), pill.bottom, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
  if (sub[0]) text(dc, sub, fText, t->note[0] ? C_MUTED : C_DIM, x, pill.top, bx - S(10), pill.bottom, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

  /* кнопки: b1 — удалить; b2 — выполнено (или «вернуть» у выполненных); b3 — на уточнение / в работу */
  if (!ghost) {
    rrect(dc, b1.left, b1.top, b1.right, b1.bottom, S(10), hb == 1 ? RGB(70, 30, 40) : RGB(40, 39, 45), 0, 0);
    draw_x(dc, b1, hb == 1 ? C_RED : C_MUTED);
    if (t->status == 1) {
      rrect(dc, b2.left, b2.top, b2.right, b2.bottom, S(10), hb == 2 ? RGB(52, 51, 60) : RGB(40, 39, 45), 0, 0);
      draw_back(dc, b2, hb == 2 ? C_TEXT : C_MUTED);
    } else {
      if (hb == 2) grad_rr(dc, b2.left, b2.top, b2.right, b2.bottom, S(10), C_ACC, C_ACC2);
      else rrect(dc, b2.left, b2.top, b2.right, b2.bottom, S(10), RGB(20, 52, 72), 0, 0);
      draw_check(dc, b2, hb == 2 ? C_TEXT : C_SEC);
      if (work) {
        rrect(dc, b3.left, b3.top, b3.right, b3.bottom, S(10), hb == 3 ? RGB(90, 62, 20) : RGB(52, 40, 22), 0, 0);
        draw_q(dc, b3, hb == 3 ? C_TEXT : C_AMB);
      } else {
        rrect(dc, b3.left, b3.top, b3.right, b3.bottom, S(10), hb == 3 ? RGB(52, 51, 60) : RGB(40, 39, 45), 0, 0);
        draw_back(dc, b3, hb == 3 ? C_TEXT : C_MUTED);
      }
    }
  }
}

static void draw_box(HDC dc, RECT r, int focus) {
  rrect(dc, r.left, r.top, r.right, r.bottom, S(12), C_CARD, 1, focus ? C_ACC : C_CARD_BR);
}

/* кнопка в шапке: иконка + надпись + плашка справа */
static void icon_keys(HDC dc, int cx, int cy, COLORREF c) {
  int w = S(18), h = S(12), l = cx - w / 2, t = cy - h / 2;
  SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, c);
  SelectObject(dc, GetStockObject(NULL_BRUSH_ID));
  RoundRect(dc, l, t, l + w, t + h, S(4), S(4));
  for (int i = 0; i < 3; i++) circle(dc, l + S(4) + i * S(5), t + S(4), S(1), c);
  line(dc, l + S(5), t + S(8), l + w - S(5), t + S(8), S(1) < 1 ? 1 : S(1), c);
}
static void icon_gear(HDC dc, int cx, int cy, COLORREF c, COLORREF bg) {
  static const int dx[8] = {10, 7, 0, -7, -10, -7, 0, 7}, dy[8] = {0, 7, 10, 7, 0, -7, -10, -7};
  for (int i = 0; i < 8; i++) line(dc, cx, cy, cx + S(dx[i] * 9 / 10), cy + S(dy[i] * 9 / 10), S(3), c);
  circle(dc, cx, cy, S(6), c);
  circle(dc, cx, cy, S(2), bg);
}
static void hdr_btn(HDC dc, RECT r, int hover, int icon, const WCHAR *label, const WCHAR *chip, COLORREF chipFg, COLORREF chipBg) {
  COLORREF bg = hover ? RGB(38, 37, 44) : RGB(24, 23, 28);
  rrect(dc, r.left, r.top, r.right, r.bottom, r.bottom - r.top, bg, 1, hover ? RGB(100, 100, 112) : RGB(58, 57, 64));
  int cy = (r.top + r.bottom) / 2;
  int ix = r.left + S(22);
  if (icon) icon_gear(dc, ix, cy, hover ? C_TEXT : C_MUTED, bg); else icon_keys(dc, ix, cy, hover ? C_TEXT : C_MUTED);
  int cw = text_w(dc, chip, fSmall) + S(16);
  RECT c = {r.right - S(7) - cw, r.top + S(7), r.right - S(7), r.bottom - S(7)};
  rrect(dc, c.left, c.top, c.right, c.bottom, c.bottom - c.top, chipBg, 0, 0);
  text(dc, chip, fSmall, chipFg, c.left, c.top, c.right, c.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  text(dc, label, fBtn, C_TEXT, ix + S(16), r.top, c.left - S(6), r.bottom, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void paint(HWND h) {
  PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps);
  do_layout();
  int W = L.W, H = L.H;
  HDC dc = CreateCompatibleDC(wdc);
  HBITMAP bm = CreateCompatibleBitmap(wdc, W, H);
  HGDIOBJ ob = SelectObject(dc, bm);
  SetBkMode(dc, TRANSPARENT);
  RECT all = {0, 0, W, H}; FillRect(dc, &all, brBg);
  glow(dc, W - S(120), S(10), S(520), S(220), RGB(8, 44, 66), C_BG);
  glow(dc, S(60), H, S(420), S(200), RGB(26, 18, 52), C_BG);

  /* шапка */
  int M = L.M;
  text(dc, L"BINDER", fTitle, C_TEXT, M, S(14), M + S(300), S(64), DT_SINGLELINE | DT_VCENTER);
  int tw = text_w(dc, L"BINDER", fTitle);
  WCHAR *dev = L"kondrashov-m";
  int bw = text_w(dc, dev, fBadge) + S(26);
  RECT bd = {M + tw + S(14), S(26), M + tw + S(14) + bw, S(54)};
  grad_rr(dc, bd.left, bd.top, bd.right, bd.bottom, S(28), C_ACC, C_ACC2);
  rrect(dc, bd.left + 1, bd.top + 1, bd.right - 1, bd.bottom - 1, S(26), RGB(18, 17, 24), 0, 0);
  text(dc, dev, fBadge, C_SEC, bd.left, bd.top, bd.right, bd.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

  {
    WCHAR chip[32];
    wsprintfW(chip, L"F1–F12");
    hdr_btn(dc, L.bindsBtn, g_hover.kind == H_BINDS, 0, L"БИНДЫ", chip, C_SEC, RGB(14, 40, 58));
    if (g_remOn) wsprintfW(chip, L"%02d:%02d", g_remH, g_remM); else wsprintfW(chip, L"выкл");
    hdr_btn(dc, L.remindBtn, g_hover.kind == H_REMIND, 1, L"НАСТРОЙКИ", chip, g_remOn ? C_GREEN : C_MUTED, g_remOn ? RGB(18, 48, 44) : RGB(40, 39, 45));
  }

  /* ввод */
  draw_box(dc, L.numBox, g_focusNum);
  draw_box(dc, L.noteBox, g_focusNote);
  RECT ab = L.addBtn;
  if (g_hover.kind == H_ADD) grad_rr(dc, ab.left, ab.top, ab.right, ab.bottom, S(46), C_SEC, C_ACC2);
  else grad_rr(dc, ab.left, ab.top, ab.right, ab.bottom, S(46), C_ACC, C_ACC2);
  text(dc, L"В РАБОТУ  →", fBtn, C_TEXT, ab.left, ab.top, ab.right, ab.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

  /* колонки */
  const WCHAR *titles[3] = {L"ЗАЯВКИ В РАБОТЕ", L"НА УТОЧНЕНИИ", L"ВЫПОЛНЕНЫ"};
  int pv = pulse(1400);
  for (int c = 0; c < 3; c++) {
    RECT col = L.col[c];
    int dropHere = g_dragging && col_of(T[g_dragIdx].status) != c && in_rect(col, g_mouse.x, g_mouse.y);
    rrect(dc, col.left, col.top, col.right, col.bottom, S(18), C_PANEL, 1, dropHere ? C_ACC : C_PANEL_BR);
    int hx = col.left + S(20), hy = col.top + S(29);
    if (c == 0) { circle(dc, hx + S(4), hy, S(4) + (L.cnt[0] ? pv * S(3) / 700 : 0), mix(C_PANEL, RGB(0, 90, 140), L.cnt[0] ? 700 - pv : 0, 700)); circle(dc, hx + S(4), hy, S(4), L.cnt[0] ? C_ACC : C_DIM); }
    else circle(dc, hx + S(4), hy, S(4), c == 1 ? C_AMB : C_GREEN);
    text(dc, titles[c], fH, C_TEXT, hx + S(18), col.top + S(12), col.right - S(14), col.top + S(46), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    int ttw = text_w(dc, titles[c], fH);
    WCHAR cnt[16]; wsprintfW(cnt, L"%d", L.cnt[c]);
    int cwid = text_w(dc, cnt, fSmall) + S(18);
    RECT cp = {hx + S(18) + ttw + S(10), col.top + S(18), hx + S(18) + ttw + S(10) + cwid, col.top + S(40)};
    rrect(dc, cp.left, cp.top, cp.right, cp.bottom, S(22), c == 0 ? RGB(14, 40, 58) : c == 1 ? RGB(56, 40, 16) : RGB(18, 48, 44), 0, 0);
    text(dc, cnt, fSmall, c == 0 ? C_SEC : c == 1 ? C_AMB : C_GREEN, cp.left, cp.top, cp.right, cp.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    if (c == 2 && L.cnt[2]) {
      RECT cb = L.clearBtn;
      rrect(dc, cb.left, cb.top, cb.right, cb.bottom, S(28), g_hover.kind == H_CLEAR ? RGB(48, 30, 36) : RGB(30, 29, 34), 1, g_hover.kind == H_CLEAR ? RGB(120, 60, 70) : RGB(55, 54, 60));
      text(dc, L"очистить", fSmall, g_hover.kind == H_CLEAR ? C_RED : C_MUTED, cb.left, cb.top, cb.right, cb.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    line(dc, col.left + S(14), col.top + S(52), col.right - S(14), col.top + S(52), 1, C_PANEL_BR);

    RECT a = L.area[c];
    IntersectClipRect(dc, a.left - 2, a.top, a.right + 2, a.bottom);
    if (!L.cnt[c]) {
      const WCHAR *e = c == 0 ? L"Нет заявок в работе.\nВпиши номер сверху и нажми Enter.\nДвойной клик по карточке — изменить."
                     : c == 1 ? L"Заявки, по которым ждёшь ответа.\nКнопка «?» на карточке — сюда.\nТаймер работы здесь на паузе."
                              : L"Сюда попадут выполненные заявки.\nНажми галочку на карточке\nили перетащи её сюда.";
      text(dc, e, fText, C_DIM, a.left + S(10), a.top + S(40), a.right - S(10), a.top + S(120), DT_CENTER | DT_WORDBREAK);
    }
    for (int k = 0; k < L.cnt[c]; k++) {
      RECT r = card_rect(c, k);
      if (r.bottom < a.top || r.top > a.bottom) continue;
      int i = L.idx[c][k];
      if (g_dragging && i == g_dragIdx) { rrect(dc, r.left, r.top, r.right, r.bottom, S(14), C_PANEL, 1, RGB(55, 54, 62)); continue; }
      int hv = (g_hover.idx == i && g_hover.kind >= H_CARD && g_hover.kind <= H_CARD_B3);
      int hb = !hv ? 0 : g_hover.kind == H_CARD_B1 ? 1 : g_hover.kind == H_CARD_B2 ? 2 : g_hover.kind == H_CARD_B3 ? 3 : 0;
      draw_card(dc, r, i, hv, hb, 0);
    }
    /* полоса прокрутки */
    int content = L.cnt[c] * (L.cardH + L.gap) - L.gap, vis = a.bottom - a.top;
    if (content > vis) {
      int th = vis * vis / content; if (th < S(30)) th = S(30);
      int ty = a.top + (vis - th) * g_scroll[c] / (content - vis);
      rrect(dc, a.right + S(4), ty, a.right + S(8), ty + th, S(4), RGB(60, 59, 66), 0, 0);
    }
    SelectClipRgn(dc, NULL);
  }
  if (g_dragging) {
    RECT r = {g_mouse.x - g_dragDX, g_mouse.y - g_dragDY, 0, 0};
    r.right = r.left + (L.area[0].right - L.area[0].left); r.bottom = r.top + L.cardH;
    draw_card(dc, r, g_dragIdx, 1, 0, 1);
  }

  /* подвал */
  RECT st = L.site;
  text(dc, L"Разработчик: kondrashov-m", fSmall, g_hover.kind == H_SITE ? C_SEC : C_DIM, st.left, st.top, st.right, st.bottom, DT_SINGLELINE | DT_VCENTER);
  WCHAR foot[200];
  if (g_toast[0] && GetTickCount() < g_toastUntil) {
    text(dc, g_toast, fSmall, C_SEC, W / 2 - S(200), st.top, W - M, st.bottom, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
  } else {
    DWORD total = 0; for (int k = 0; k < L.cnt[2]; k++) total += T[L.idx[2][k]].elapsed;
    WCHAR d[32]; fmt_dur(total, d);
    wsprintfW(foot, L"в работе: %d   ·   на уточнении: %d   ·   выполнено: %d   ·   время по выполненным: %s", L.cnt[0], L.cnt[1], L.cnt[2], d);
    text(dc, foot, fSmall, C_DIM, W / 2 - S(200), st.top, W - M, st.bottom, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
  }

  BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY);
  SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
  EndPaint(h, &ps);
}

static void place_edits(void) {
  do_layout();
  int eh = S(22);
  RECT a = L.numBox, b = L.noteBox;
  MoveWindow(g_eNum, a.left + S(14), (a.top + a.bottom - eh) / 2, a.right - a.left - S(28), eh, 1);
  MoveWindow(g_eNote, b.left + S(14), (b.top + b.bottom - eh) / 2, b.right - b.left - S(28), eh, 1);
}

/* ---------- действия ---------- */
static void act_add(void) {
  WCHAR num[64], note[200], msg[160];
  GetWindowTextW(g_eNum, num, 64); GetWindowTextW(g_eNote, note, 200);
  wtrim(num); wtrim(note); wclean(num); wclean(note);
  if (!num[0]) { toast(L"Впиши номер заявки"); SetFocus(g_eNum); return; }
  num[31] = 0; note[119] = 0;
  int i = find_num(num);
  DWORD now = now_unix();
  if (i >= 0) {
    if (T[i].status == 0) { wsprintfW(msg, L"Заявка %s уже в работе", num); toast(msg); }
    else {
      if (note[0]) wcopy(T[i].note, note, 120);
      set_status(i, 0);
    }
  } else {
    if (NT >= MAXT) { toast(L"Слишком много заявок — очисти выполненные"); return; }
    Ticket *t = &T[NT++]; memset(t, 0, sizeof(*t));
    wcopy(t->num, num, 32); wcopy(t->note, note, 120);
    t->created = t->runStart = now; t->order = ++SEQ;
    wsprintfW(msg, L"Заявка %s взята в работу", num); toast(msg);
  }
  g_scroll[0] = 0;
  SetWindowTextW(g_eNum, L""); SetWindowTextW(g_eNote, L""); SetFocus(g_eNum);
  save_data(); InvalidateRect(g_wnd, NULL, 0);
}
static void set_status(int i, int st) {
  Ticket *t = &T[i];
  if (t->status == st) return;
  DWORD now = now_unix();
  if (t->status == 0) t->elapsed = tk_elapsed(t);   /* остановили таймер работы */
  t->status = st; t->order = ++SEQ;
  if (st == 0 || st == 2) t->runStart = now;        /* старт работы / начало ожидания */
  else t->doneAt = now;
  g_scroll[col_of(st)] = 0;
  WCHAR m[140], d[32];
  if (st == 1) { fmt_dur(t->elapsed, d); wsprintfW(m, L"Заявка %s выполнена за %s", t->num, d); }
  else if (st == 2) wsprintfW(m, L"Заявка %s на уточнении — таймер на паузе", t->num);
  else wsprintfW(m, L"Заявка %s в работе — таймер продолжен", t->num);
  toast(m);
  save_data(); InvalidateRect(g_wnd, NULL, 0);
}
static void act_done(int i) { set_status(i, 1); }
static void act_return(int i) { set_status(i, 0); }
static void act_wait(int i) { set_status(i, 2); }
static void remove_at(int i) {
  if (g_ew) edit_close(); for (int k = i; k < NT - 1; k++) T[k] = T[k + 1]; NT--; }
static void act_delete(int i) {
  WCHAR m[200]; wsprintfW(m, L"Удалить заявку %s из списка?", T[i].num);
  if (MessageBoxW(g_wnd, m, L"BINDER", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  remove_at(i); g_hover.kind = H_NONE; save_data(); InvalidateRect(g_wnd, NULL, 0);
}
static void act_clear(void) {
  build_lists();
  if (!L.cnt[2]) return;
  WCHAR m[200]; wsprintfW(m, L"Убрать %d выполненных заявок из списка?\nОни сохранятся в файле rtbind_history.txt.", L.cnt[2]);
  if (MessageBoxW(g_wnd, m, L"BINDER", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  WCHAR ln[400], d[32], c1[16], c2[16]; SYSTEMTIME st;
  SB[0] = 0;
  for (int i = 0; i < NT; i++) {
    if (T[i].status != 1) continue;
    unix_local(T[i].doneAt, &st); fmt_dur(T[i].elapsed, d); fmt_clock(T[i].created, c1); fmt_clock(T[i].doneAt, c2);
    wsprintfW(ln, L"%02d.%02d.%04d | %s | взята %s | выполнена %s | время %s | %s\r\n", st.wDay, st.wMonth, st.wYear, T[i].num, c1, c2, d, T[i].note);
    wcat(SB, ln, sizeof(SB) / 2);
  }
  write_utf8(g_hist, SB, 1);
  for (int i = NT - 1; i >= 0; i--) if (T[i].status == 1) remove_at(i);
  g_scroll[2] = 0; save_data(); toast(L"Выполненные заявки перенесены в историю"); InvalidateRect(g_wnd, NULL, 0);
}
static void copy_text(const WCHAR *s) {
  int n = wlen(s);
  if (!OpenClipboard(g_wnd)) return;
  EmptyClipboard();
  HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (ULONG_PTR)(n + 1) * 2);
  if (g) { WCHAR *p = GlobalLock(g); memcpy(p, s, (unsigned)(n + 1) * 2); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
  CloseClipboard();
}

/* ---------- бинды ---------- */
static WCHAR BT[20000];
static void bind_path(int n, WCHAR *out) { wsprintfW(out, L"%s\\F%d.txt", g_binds, n); }
static void bind_read(int n, WCHAR *out, int max) {
  WCHAR p[600]; bind_path(n, p);
  read_text(p, RW, sizeof(RW) / 2);
  wcopy(out, RW, max);
}
static void bind_write(int n, const WCHAR *s) { WCHAR p[600]; bind_path(n, p); CreateDirectoryW(g_binds, NULL); write_utf8(p, s, 0); }

static int key_down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static INPUT IN[512];
static void type_text(const WCHAR *s) {
  DWORD end = GetTickCount() + 2000;
  while (GetTickCount() < end && (key_down(VK_CONTROL) || key_down(VK_SHIFT) || key_down(VK_MENU))) Sleep(10);
  int n = 0;
  for (; *s; s++) {
    WCHAR c = *s;
    if (c == '\r') { if (s[1] == '\n') continue; c = '\n'; }
    if (n > 500) { SendInput((UINT)n, IN, sizeof(INPUT)); n = 0; Sleep(8); }
    memset(&IN[n], 0, sizeof(INPUT) * 2);
    IN[n].type = IN[n + 1].type = INPUT_KEYBOARD;
    if (c == '\n' || c == '\t') { IN[n].ki.wVk = IN[n + 1].ki.wVk = c == '\n' ? VK_RETURN : VK_TAB; IN[n + 1].ki.dwFlags = KEYEVENTF_KEYUP; }
    else { IN[n].ki.wScan = IN[n + 1].ki.wScan = c; IN[n].ki.dwFlags = KEYEVENTF_UNICODE; IN[n + 1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP; }
    n += 2;
  }
  if (n) SendInput((UINT)n, IN, sizeof(INPUT));
}
static void on_hotkey(int n) {
  bind_read(n, BT, 20000);
  int l = wlen(BT); while (l > 0 && (BT[l - 1] == '\n' || BT[l - 1] == '\r')) BT[--l] = 0;
  WCHAR m[120];
  if (!l) { wsprintfW(m, L"Бинд Ctrl+F%d пустой — открой «Бинды»", n); toast(m); return; }
  type_text(BT);
  wsprintfW(m, L"Ctrl+F%d — текст напечатан", n); toast(m);
}

/* ---------- окно редактора биндов ---------- */
static int g_bCur = -1, g_bHover;
static RECT g_bSave, g_bFolder;
static void binds_list_item(int n, WCHAR *out) {
  WCHAR prev[60]; bind_read(n, BT, 20000);
  int k = 0; for (int i = 0; BT[i] && k < 40; i++) { WCHAR c = BT[i]; if (c == '\r' || c == '\n' || c == '\t') c = ' '; prev[k++] = c; }
  prev[k] = 0;
  wsprintfW(out, L"Ctrl+F%-2d   %s", n, k ? prev : L"— пусто —");
}
static void binds_save_cur(void) {
  if (g_bCur < 0 || !g_bEdit) return;
  GetWindowTextW(g_bEdit, BT, 20000);
  bind_write(g_bCur + 1, BT);
  WCHAR it[120]; binds_list_item(g_bCur + 1, it);
  SendMessageW(g_bList, LB_DELETESTRING, (WPARAM)g_bCur, 0);
  SendMessageW(g_bList, LB_INSERTSTRING, (WPARAM)g_bCur, (LPARAM)it);
  SendMessageW(g_bList, LB_SETCURSEL, (WPARAM)g_bCur, 0);
}
static void binds_load(int k) { g_bCur = k; bind_read(k + 1, BT, 20000); SetWindowTextW(g_bEdit, BT); }

static void binds_layout(HWND h) {
  RECT rc; GetClientRect(h, &rc);
  int M = S(20), top = S(78), bot = rc.bottom - S(70);
  MoveWindow(g_bList, M, top, S(250), bot - top, 1);
  MoveWindow(g_bEdit, M + S(266), top, rc.right - M - (M + S(266)), bot - top, 1);
  g_bSave = (RECT){rc.right - M - S(170), rc.bottom - S(54), rc.right - M, rc.bottom - S(16)};
  g_bFolder = (RECT){rc.right - M - S(170) - S(12) - S(170), rc.bottom - S(54), rc.right - M - S(182), rc.bottom - S(16)};
}

static LRESULT WINAPI BindsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE: {
    g_bList = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, h, (HMENU)201, g_inst, NULL);
    g_bEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, h, (HMENU)202, g_inst, NULL);
    SendMessageW(g_bList, WM_SETFONT, (WPARAM)fEdit, 1); SendMessageW(g_bEdit, WM_SETFONT, (WPARAM)fEdit, 1);
    SendMessageW(g_bList, LB_SETITEMHEIGHT, 0, S(30));
    SendMessageW(g_bEdit, EM_SETLIMITTEXT, 19000, 0);
    WCHAR it[120];
    for (int n = 1; n <= 12; n++) { binds_list_item(n, it); SendMessageW(g_bList, LB_ADDSTRING, 0, (LPARAM)it); }
    SendMessageW(g_bList, LB_SETCURSEL, 0, 0); binds_load(0);
    binds_layout(h);
    return 0;
  }
  case WM_SIZE: binds_layout(h); InvalidateRect(h, NULL, 0); return 0;
  case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = S(640); ((MINMAXINFO *)l)->ptMinTrackSize.y = S(420); return 0;
  case WM_COMMAND:
    if (LOWORD(w) == 201 && HIWORD(w) == LBN_SELCHANGE) {
      int k = (int)SendMessageW(g_bList, LB_GETCURSEL, 0, 0);
      if (k >= 0 && k != g_bCur) { binds_save_cur(); SendMessageW(g_bList, LB_SETCURSEL, (WPARAM)k, 0); binds_load(k); }
    }
    return 0;
  case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
    SetTextColor((HDC)w, C_TEXT); SetBkColor((HDC)w, C_CARD); return (LRESULT)brEdit;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT); FillRect(dc, &rc, brBg);
    glow(dc, rc.right - S(60), 0, S(360), S(160), RGB(8, 44, 66), C_BG);
    int M = S(20);
    text(dc, L"БИНДЫ", fH, C_TEXT, M, S(14), rc.right, S(40), DT_SINGLELINE | DT_VCENTER);
    text(dc, L"Ctrl+F1…F12 печатают этот текст в активное окно. Слева выбери клавишу, справа впиши фразу.", fSmall, C_MUTED, M, S(40), rc.right - M, S(64), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (g_bHover == 2) grad_rr(dc, g_bSave.left, g_bSave.top, g_bSave.right, g_bSave.bottom, S(40), C_SEC, C_ACC2);
    else grad_rr(dc, g_bSave.left, g_bSave.top, g_bSave.right, g_bSave.bottom, S(40), C_ACC, C_ACC2);
    text(dc, L"СОХРАНИТЬ", fBtn, C_TEXT, g_bSave.left, g_bSave.top, g_bSave.right, g_bSave.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    rrect(dc, g_bFolder.left, g_bFolder.top, g_bFolder.right, g_bFolder.bottom, S(40), g_bHover == 1 ? RGB(40, 39, 46) : RGB(26, 25, 30), 1, RGB(62, 61, 68));
    text(dc, L"ОТКРЫТЬ ПАПКУ", fBtn, C_TEXT, g_bFolder.left, g_bFolder.top, g_bFolder.right, g_bFolder.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    text(dc, L"Сохраняется и при переключении клавиши", fSmall, C_DIM, M, g_bSave.top, g_bFolder.left - S(10), g_bSave.bottom, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
    return 0;
  }
  case WM_MOUSEMOVE: {
    int x = GET_X(l), y = GET_Y(l), hv = in_rect(g_bSave, x, y) ? 2 : in_rect(g_bFolder, x, y) ? 1 : 0;
    if (hv != g_bHover) { g_bHover = hv; InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_SETCURSOR: {
    POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
    if (LOWORD(l) == 1 && (HWND)w == h && (in_rect(g_bSave, p.x, p.y) || in_rect(g_bFolder, p.x, p.y))) { SetCursor(curHand); return 1; }
    break;
  }
  case WM_LBUTTONUP: {
    int x = GET_X(l), y = GET_Y(l);
    if (in_rect(g_bSave, x, y)) { binds_save_cur(); toast(L"Бинды сохранены"); SetFocus(g_bEdit); }
    else if (in_rect(g_bFolder, x, y)) { binds_save_cur(); CreateDirectoryW(g_binds, NULL); ShellExecuteW(h, L"open", g_binds, NULL, NULL, SW_SHOWNORMAL); }
    return 0;
  }
  case WM_CLOSE: binds_save_cur(); DestroyWindow(h); return 0;
  case WM_DESTROY: g_bw = NULL; g_bList = g_bEdit = NULL; g_bCur = -1; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static void open_binds(void) {
  if (g_bw) { ShowWindow(g_bw, SW_RESTORE); SetForegroundWindow(g_bw); return; }
  RECT wr; GetWindowRect(g_wnd, &wr);
  int w = S(760), hh = S(500);
  g_bw = CreateWindowExW(0, L"RTBindBinds", L"Бинды — BINDER by kondrashov-m", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                         wr.left + (wr.right - wr.left - w) / 2, wr.top + S(60), w, hh, g_wnd, NULL, g_inst, NULL);
  ShowWindow(g_bw, SW_SHOW);
}

/* ---------- поля ввода ---------- */
static LRESULT WINAPI EditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CHAR && w == '\r') { PostMessageW(g_wnd, WM_APP + 1, 0, 0); return 0; }
  if (m == WM_CHAR && w == '\t') { SetFocus(h == g_eNum ? g_eNote : g_eNum); return 0; }
  if (m == WM_CHAR && w == 27) { SetWindowTextW(h, L""); return 0; }
  return CallWindowProcW(g_oldEdit, h, m, w, l);
}

static HFONT mkfont(const WCHAR *face, int px, int weight) {
  return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, 1, 0, 0, 5, 0, face);
}

/* ---------- окно «Изменить заявку» ---------- */
static HWND g_ew, g_ewNum, g_ewNote;
static int g_ewIdx = -1, g_ewHover, g_ewFocus;
static WNDPROC g_oldEd2;
static RECT g_ewSave, g_ewCancel, g_ewBoxNum, g_ewBoxNote;

static void edit_close(void) {
  if (!g_ew) return;
  HWND w = g_ew; g_ew = NULL;
  EnableWindow(g_wnd, 1);
  DestroyWindow(w);
  SetForegroundWindow(g_wnd);
  InvalidateRect(g_wnd, NULL, 0);
}
static void edit_save(void) {
  int i = g_ewIdx;
  if (i < 0 || i >= NT) { edit_close(); return; }
  WCHAR num[64], note[200];
  GetWindowTextW(g_ewNum, num, 64); GetWindowTextW(g_ewNote, note, 200);
  wtrim(num); wtrim(note); wclean(num); wclean(note); num[31] = 0; note[119] = 0;
  if (!num[0]) { MessageBoxW(g_ew, L"Номер заявки не может быть пустым.", L"BINDER", MB_OK | MB_ICONWARNING); SetFocus(g_ewNum); return; }
  int j = find_num(num);
  if (j >= 0 && j != i) {
    WCHAR m[160]; wsprintfW(m, L"Заявка %s уже есть в списке.", num);
    MessageBoxW(g_ew, m, L"BINDER", MB_OK | MB_ICONWARNING); SetFocus(g_ewNum); return;
  }
  wcopy(T[i].num, num, 32); wcopy(T[i].note, note, 120);
  save_data();
  WCHAR m[120]; wsprintfW(m, L"Заявка %s изменена", num); toast(m);
  edit_close();
}
static LRESULT WINAPI Ed2Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CHAR && w == '\r') { PostMessageW(g_ew, WM_APP + 2, 0, 0); return 0; }
  if (m == WM_CHAR && w == 27) { PostMessageW(g_ew, WM_APP + 3, 0, 0); return 0; }
  if (m == WM_CHAR && w == '\t') { SetFocus(h == g_ewNum ? g_ewNote : g_ewNum); return 0; }
  return CallWindowProcW(g_oldEd2, h, m, w, l);
}
static void ew_layout(HWND h) {
  RECT rc; GetClientRect(h, &rc);
  int M = S(22), bh = S(46);
  g_ewBoxNum = (RECT){M, S(86), rc.right - M, S(86) + bh};
  g_ewBoxNote = (RECT){M, S(166), rc.right - M, S(166) + bh};
  g_ewSave = (RECT){rc.right - M - S(160), rc.bottom - S(62), rc.right - M, rc.bottom - S(20)};
  g_ewCancel = (RECT){g_ewSave.left - S(12) - S(130), g_ewSave.top, g_ewSave.left - S(12), g_ewSave.bottom};
  int eh = S(22);
  MoveWindow(g_ewNum, g_ewBoxNum.left + S(14), (g_ewBoxNum.top + g_ewBoxNum.bottom - eh) / 2, g_ewBoxNum.right - g_ewBoxNum.left - S(28), eh, 1);
  MoveWindow(g_ewNote, g_ewBoxNote.left + S(14), (g_ewBoxNote.top + g_ewBoxNote.bottom - eh) / 2, g_ewBoxNote.right - g_ewBoxNote.left - S(28), eh, 1);
}
static LRESULT WINAPI EditWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE:
    g_ewNum = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)301, g_inst, NULL);
    g_ewNote = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)302, g_inst, NULL);
    SendMessageW(g_ewNum, WM_SETFONT, (WPARAM)fEdit, 1); SendMessageW(g_ewNote, WM_SETFONT, (WPARAM)fEdit, 1);
    SendMessageW(g_ewNum, EM_SETLIMITTEXT, 30, 0); SendMessageW(g_ewNote, EM_SETLIMITTEXT, 118, 0);
    SendMessageW(g_ewNote, EM_SETCUEBANNER, 1, (LPARAM)L"Что делаю (необязательно)");
    g_oldEd2 = (WNDPROC)SetWindowLongW(g_ewNum, GWL_WNDPROC, (LONG)Ed2Proc);
    SetWindowLongW(g_ewNote, GWL_WNDPROC, (LONG)Ed2Proc);
    ew_layout(h);
    return 0;
  case WM_SIZE: ew_layout(h); InvalidateRect(h, NULL, 0); return 0;
  case WM_CTLCOLOREDIT: SetTextColor((HDC)w, C_TEXT); SetBkColor((HDC)w, C_CARD); return (LRESULT)brEdit;
  case WM_COMMAND:
    if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) { g_ewFocus = GetFocus() == g_ewNum ? 1 : GetFocus() == g_ewNote ? 2 : 0; InvalidateRect(h, NULL, 0); }
    return 0;
  case WM_APP + 2: edit_save(); return 0;
  case WM_APP + 3: edit_close(); return 0;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT); FillRect(dc, &rc, brBg);
    glow(dc, rc.right - S(40), 0, S(320), S(140), RGB(8, 44, 66), C_BG);
    int M = S(22);
    text(dc, L"ИЗМЕНИТЬ ЗАЯВКУ", fH, C_TEXT, M, S(14), rc.right - M, S(44), DT_SINGLELINE | DT_VCENTER);
    text(dc, L"Номер заявки", fSmall, C_MUTED, M, S(60), rc.right, S(82), DT_SINGLELINE | DT_VCENTER);
    text(dc, L"Что делаю", fSmall, C_MUTED, M, S(140), rc.right, S(162), DT_SINGLELINE | DT_VCENTER);
    rrect(dc, g_ewBoxNum.left, g_ewBoxNum.top, g_ewBoxNum.right, g_ewBoxNum.bottom, S(12), C_CARD, 1, g_ewFocus == 1 ? C_ACC : C_CARD_BR);
    rrect(dc, g_ewBoxNote.left, g_ewBoxNote.top, g_ewBoxNote.right, g_ewBoxNote.bottom, S(12), C_CARD, 1, g_ewFocus == 2 ? C_ACC : C_CARD_BR);
    grad_rr(dc, g_ewSave.left, g_ewSave.top, g_ewSave.right, g_ewSave.bottom, S(42), g_ewHover == 2 ? C_SEC : C_ACC, C_ACC2);
    text(dc, L"СОХРАНИТЬ", fBtn, C_TEXT, g_ewSave.left, g_ewSave.top, g_ewSave.right, g_ewSave.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    rrect(dc, g_ewCancel.left, g_ewCancel.top, g_ewCancel.right, g_ewCancel.bottom, S(42), g_ewHover == 1 ? RGB(40, 39, 46) : RGB(26, 25, 30), 1, RGB(62, 61, 68));
    text(dc, L"ОТМЕНА", fBtn, C_TEXT, g_ewCancel.left, g_ewCancel.top, g_ewCancel.right, g_ewCancel.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    text(dc, L"Enter — сохранить, Esc — отмена", fSmall, C_DIM, M, g_ewSave.top, g_ewCancel.left - S(10), g_ewSave.bottom, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
    return 0;
  }
  case WM_MOUSEMOVE: {
    int x = GET_X(l), y = GET_Y(l), hv = in_rect(g_ewSave, x, y) ? 2 : in_rect(g_ewCancel, x, y) ? 1 : 0;
    if (hv != g_ewHover) { g_ewHover = hv; InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_SETCURSOR: {
    POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
    if (LOWORD(l) == 1 && (HWND)w == h && (in_rect(g_ewSave, p.x, p.y) || in_rect(g_ewCancel, p.x, p.y))) { SetCursor(curHand); return 1; }
    break;
  }
  case WM_LBUTTONUP: {
    int x = GET_X(l), y = GET_Y(l);
    if (in_rect(g_ewSave, x, y)) edit_save();
    else if (in_rect(g_ewCancel, x, y)) edit_close();
    return 0;
  }
  case WM_CLOSE: edit_close(); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
static void open_edit(int i) {
  if (g_ew || i < 0 || i >= NT) return;
  g_ewIdx = i; g_ewHover = 0; g_ewFocus = 0;
  RECT wr; GetWindowRect(g_wnd, &wr);
  int w = S(560), hh = S(330);
  g_ew = CreateWindowExW(0, L"RTBindEdit", L"Изменить заявку — BINDER", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                         wr.left + (wr.right - wr.left - w) / 2, wr.top + (wr.bottom - wr.top - hh) / 2, w, hh, g_wnd, NULL, g_inst, NULL);
  SetWindowTextW(g_ewNum, T[i].num); SetWindowTextW(g_ewNote, T[i].note);
  EnableWindow(g_wnd, 0);
  ShowWindow(g_ew, SW_SHOW);
  SetFocus(T[i].note[0] ? g_ewNum : g_ewNote);
}

/* ---------- напоминание ---------- */
static DWORD g_remLastDay;       /* ггггммдд последнего показа */
static DWORD g_snoozeUntil;      /* unix-время отложенного показа */
static HWND g_rw, g_sw, g_swH, g_swM;
static int g_rwTest, g_rwHover, g_swOn, g_swHover, g_swFocus;
static RECT g_rwOpen, g_rwSnooze, g_rwClose, g_swToggle, g_swSave, g_swTest, g_swCancel, g_swBoxH, g_swBoxM;
static WNDPROC g_oldEd3;

static DWORD today_key(void) { SYSTEMTIME st; GetLocalTime(&st); return (DWORD)st.wYear * 10000 + st.wMonth * 100 + st.wDay; }

static void save_settings(void) {
  WCHAR b[200];
  wsprintfW(b, L"remind=%d\r\ntime=%02d:%02d\r\nlast=%lu\r\n", g_remOn, g_remH, g_remM, g_remLastDay);
  write_utf8(g_set, b, 0);
}
static int find_key(const WCHAR *src, const WCHAR *key, const WCHAR **val) {
  int kl = wlen(key);
  for (const WCHAR *p = src; *p; p++) {
    if (p != src && p[-1] != '\n') continue;
    int k = 0; while (k < kl && p[k] == key[k]) k++;
    if (k == kl) { *val = p + kl; return 1; }
  }
  return 0;
}
static void load_settings(void) {
  if (read_text(g_set, RW, sizeof(RW) / 2) <= 0) { save_settings(); return; }
  const WCHAR *v;
  if (find_key(RW, L"remind=", &v)) g_remOn = (*v == '1');
  if (find_key(RW, L"time=", &v)) {
    DWORD hh = parse_u(&v); if (*v == ':') v++; DWORD mm = parse_u(&v);
    if (hh < 24 && mm < 60) { g_remH = (int)hh; g_remM = (int)mm; }
  }
  if (find_key(RW, L"last=", &v)) g_remLastDay = parse_u(&v);
}

static void rem_close(void) { if (g_rw) { HWND w = g_rw; g_rw = NULL; DestroyWindow(w); } }

static void show_reminder(int test) {
  rem_close();
  build_lists();
  g_rwTest = test; g_rwHover = 0;
  int open = L.cnt[0] + L.cnt[1];
  int rows = open > 6 ? 7 : open;
  if (!rows) rows = 1;
  int w = S(440), hh = S(112) + rows * S(30) + S(84);
  RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  g_rw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"RTBindRemind", L"BINDER — напоминание", WS_POPUP,
                         wa.right - w - S(20), wa.bottom - hh - S(20), w, hh, NULL, NULL, g_inst, NULL);
  SetWindowPos(g_rw, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ShowWindow(g_rw, SW_SHOWNOACTIVATE);
  MessageBeep(MB_ICONINFORMATION);
}

static void remind_tick(void) {
  if (g_snoozeUntil && now_unix() >= g_snoozeUntil) {
    g_snoozeUntil = 0; build_lists();
    if (L.cnt[0] + L.cnt[1]) show_reminder(0);
    return;
  }
  if (!g_remOn) return;
  SYSTEMTIME st; GetLocalTime(&st);
  DWORD day = today_key();
  if (g_remLastDay == day) return;
  if (st.wHour > g_remH || (st.wHour == g_remH && st.wMinute >= g_remM)) {
    /* показываем только в ту же минуту или в течение 2 часов после, чтобы не всплывало утром за вчера */
    int diff = (st.wHour * 60 + st.wMinute) - (g_remH * 60 + g_remM);
    g_remLastDay = day; save_settings();
    build_lists();
    if (diff <= 120 && L.cnt[0] + L.cnt[1]) show_reminder(0);
  }
}

static LRESULT WINAPI RemProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT); FillRect(dc, &rc, brBg);
    glow(dc, rc.right - S(40), 0, S(360), S(160), RGB(8, 44, 66), C_BG);
    grad_h(dc, 0, 0, rc.right, S(4), C_ACC, C_ACC2);
    SelectObject(dc, GetStockObject(NULL_PEN));
    int M = S(22);
    build_lists();
    g_rwClose = (RECT){rc.right - S(44), S(16), rc.right - S(14), S(46)};
    rrect(dc, g_rwClose.left, g_rwClose.top, g_rwClose.right, g_rwClose.bottom, S(10), g_rwHover == 3 ? RGB(60, 59, 66) : RGB(34, 33, 38), 0, 0);
    draw_x(dc, g_rwClose, C_MUTED);
    int tx = M;
    if (g_rwTest) {
      RECT tb = {M, S(20), M + S(62), S(42)};
      rrect(dc, tb.left, tb.top, tb.right, tb.bottom, S(22), RGB(60, 40, 20), 0, 0);
      text(dc, L"ТЕСТ", fSmall, RGB(255, 190, 90), tb.left, tb.top, tb.right, tb.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
      tx = tb.right + S(10);
    }
    text(dc, L"НАПОМИНАНИЕ", fH, C_TEXT, tx, S(16), rc.right - S(50), S(46), DT_SINGLELINE | DT_VCENTER);
    WCHAR b[160];
    if (L.cnt[0] && L.cnt[1]) wsprintfW(b, L"Скоро конец смены. В работе: %d, на уточнении: %d", L.cnt[0], L.cnt[1]);
    else if (L.cnt[0]) wsprintfW(b, L"Скоро конец смены. В работе осталось заявок: %d", L.cnt[0]);
    else if (L.cnt[1]) wsprintfW(b, L"Скоро конец смены. На уточнении заявок: %d", L.cnt[1]);
    else wsprintfW(b, L"Незакрытых заявок нет — всё сделано. Так держать!");
    text(dc, b, fText, C_MUTED, M, S(54), rc.right - M, S(80), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    int y = S(92);
    int open = L.cnt[0] + L.cnt[1];
    for (int k = 0; k < open && k < 6; k++) {
      int isWait = k >= L.cnt[0];
      Ticket *t = &T[isWait ? L.idx[1][k - L.cnt[0]] : L.idx[0][k]];
      WCHAR d[32]; fmt_dur(tk_elapsed(t), d);
      rrect(dc, M, y, rc.right - M, y + S(26), S(10), C_CARD, 0, 0);
      circle(dc, M + S(12), y + S(13), S(3), isWait ? C_AMB : C_ACC);
      text(dc, t->num, fBtn, C_TEXT, M + S(24), y, M + S(150), y + S(26), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
      text(dc, t->note, fSmall, C_MUTED, M + S(150), y, rc.right - M - S(90), y + S(26), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
      text(dc, d, fMono, isWait ? C_AMB : C_SEC, rc.right - M - S(90), y, rc.right - M - S(10), y + S(26), DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
      y += S(30);
    }
    if (open > 6) { wsprintfW(b, L"и ещё %d…", open - 6); text(dc, b, fSmall, C_DIM, M, y, rc.right - M, y + S(26), DT_SINGLELINE | DT_VCENTER); }
    g_rwOpen = (RECT){rc.right - M - S(150), rc.bottom - S(60), rc.right - M, rc.bottom - S(20)};
    g_rwSnooze = (RECT){g_rwOpen.left - S(12) - S(140), g_rwOpen.top, g_rwOpen.left - S(12), g_rwOpen.bottom};
    grad_rr(dc, g_rwOpen.left, g_rwOpen.top, g_rwOpen.right, g_rwOpen.bottom, S(40), g_rwHover == 2 ? C_SEC : C_ACC, C_ACC2);
    text(dc, L"ПОДРОБНЕЕ", fBtn, C_TEXT, g_rwOpen.left, g_rwOpen.top, g_rwOpen.right, g_rwOpen.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    rrect(dc, g_rwSnooze.left, g_rwSnooze.top, g_rwSnooze.right, g_rwSnooze.bottom, S(40), g_rwHover == 1 ? RGB(40, 39, 46) : RGB(26, 25, 30), 1, RGB(62, 61, 68));
    text(dc, L"ХОРОШО", fBtn, C_TEXT, g_rwSnooze.left, g_rwSnooze.top, g_rwSnooze.right, g_rwSnooze.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    /* рамка */
    SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, RGB(55, 54, 62)); SelectObject(dc, GetStockObject(DC_BRUSH));
    line(dc, 0, 0, 0, rc.bottom, 1, RGB(55, 54, 62)); line(dc, rc.right - 1, 0, rc.right - 1, rc.bottom, 1, RGB(55, 54, 62)); line(dc, 0, rc.bottom - 1, rc.right, rc.bottom - 1, 1, RGB(55, 54, 62));
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
    return 0;
  }
  case WM_MOUSEMOVE: {
    int x = GET_X(l), y = GET_Y(l);
    int hv = in_rect(g_rwOpen, x, y) ? 2 : in_rect(g_rwSnooze, x, y) ? 1 : in_rect(g_rwClose, x, y) ? 3 : 0;
    if (hv != g_rwHover) { g_rwHover = hv; InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_SETCURSOR: {
    POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
    if (in_rect(g_rwOpen, p.x, p.y) || in_rect(g_rwSnooze, p.x, p.y) || in_rect(g_rwClose, p.x, p.y)) { SetCursor(curHand); return 1; }
    SetCursor(curArrow); return 1;
  }
  case WM_LBUTTONUP: {
    int x = GET_X(l), y = GET_Y(l);
    if (in_rect(g_rwOpen, x, y)) { rem_close(); ShowWindow(g_wnd, SW_RESTORE); SetForegroundWindow(g_wnd); }
    else if (in_rect(g_rwSnooze, x, y)) rem_close();
    else if (in_rect(g_rwClose, x, y)) rem_close();
    return 0;
  }
  case WM_DESTROY: if (g_rw == h) g_rw = NULL; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

/* окно настроек напоминания */
static void set_close(void) {
  if (!g_sw) return;
  HWND w = g_sw; g_sw = NULL;
  EnableWindow(g_wnd, 1); DestroyWindow(w); SetForegroundWindow(g_wnd); InvalidateRect(g_wnd, NULL, 0);
}
static int set_read(int *hh, int *mm) {
  WCHAR a[8], b[8]; GetWindowTextW(g_swH, a, 8); GetWindowTextW(g_swM, b, 8);
  if (!a[0] || !b[0]) return 0;
  const WCHAR *p = a; DWORD h1 = parse_u(&p); p = b; DWORD m1 = parse_u(&p);
  if (h1 > 23 || m1 > 59) return 0;
  *hh = (int)h1; *mm = (int)m1; return 1;
}
static void set_save(void) {
  int hh, mm;
  if (!set_read(&hh, &mm)) { MessageBoxW(g_sw, L"Укажи время: часы 0–23, минуты 0–59.", L"BINDER", MB_OK | MB_ICONWARNING); SetFocus(g_swH); return; }
  int changed = hh != g_remH || mm != g_remM || g_swOn != g_remOn;
  g_remOn = g_swOn; g_remH = hh; g_remM = mm;
  if (changed) {
    /* если новое время ещё не наступило сегодня — сегодня тоже напомним */
    SYSTEMTIME st; GetLocalTime(&st);
    if (st.wHour * 60 + st.wMinute < hh * 60 + mm) g_remLastDay = 0;
  }
  save_settings();
  WCHAR msg[100];
  if (g_remOn) wsprintfW(msg, L"Напоминание включено на %02d:%02d", g_remH, g_remM); else wsprintfW(msg, L"Напоминание выключено");
  toast(msg);
  set_close();
}
static LRESULT WINAPI Ed3Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_CHAR && w == '\r') { PostMessageW(g_sw, WM_APP + 2, 0, 0); return 0; }
  if (m == WM_CHAR && w == 27) { PostMessageW(g_sw, WM_APP + 3, 0, 0); return 0; }
  if (m == WM_CHAR && w == '\t') { SetFocus(h == g_swH ? g_swM : g_swH); return 0; }
  return CallWindowProcW(g_oldEd3, h, m, w, l);
}
static void set_layout(HWND h) {
  RECT rc; GetClientRect(h, &rc);
  int M = S(22);
  g_swToggle = (RECT){M, S(104), M + S(56), S(104) + S(30)};
  g_swBoxH = (RECT){M + S(90), S(160), M + S(90) + S(64), S(160) + S(44)};
  g_swBoxM = (RECT){g_swBoxH.right + S(24), g_swBoxH.top, g_swBoxH.right + S(24) + S(64), g_swBoxH.bottom};
  g_swSave = (RECT){rc.right - M - S(150), rc.bottom - S(62), rc.right - M, rc.bottom - S(20)};
  g_swCancel = (RECT){g_swSave.left - S(12) - S(120), g_swSave.top, g_swSave.left - S(12), g_swSave.bottom};
  g_swTest = (RECT){M, g_swSave.top, M + S(110), g_swSave.bottom};
  int eh = S(22);
  MoveWindow(g_swH, g_swBoxH.left + S(8), (g_swBoxH.top + g_swBoxH.bottom - eh) / 2, g_swBoxH.right - g_swBoxH.left - S(16), eh, 1);
  MoveWindow(g_swM, g_swBoxM.left + S(8), (g_swBoxM.top + g_swBoxM.bottom - eh) / 2, g_swBoxM.right - g_swBoxM.left - S(16), eh, 1);
}
static LRESULT WINAPI SetProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE: {
    g_swH = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, 0, 0, 10, 10, h, (HMENU)401, g_inst, NULL);
    g_swM = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_NUMBER, 0, 0, 10, 10, h, (HMENU)402, g_inst, NULL);
    SendMessageW(g_swH, WM_SETFONT, (WPARAM)fEdit, 1); SendMessageW(g_swM, WM_SETFONT, (WPARAM)fEdit, 1);
    SendMessageW(g_swH, EM_SETLIMITTEXT, 2, 0); SendMessageW(g_swM, EM_SETLIMITTEXT, 2, 0);
    g_oldEd3 = (WNDPROC)SetWindowLongW(g_swH, GWL_WNDPROC, (LONG)Ed3Proc);
    SetWindowLongW(g_swM, GWL_WNDPROC, (LONG)Ed3Proc);
    WCHAR b[8];
    wsprintfW(b, L"%02d", g_remH); SetWindowTextW(g_swH, b);
    wsprintfW(b, L"%02d", g_remM); SetWindowTextW(g_swM, b);
    set_layout(h);
    return 0;
  }
  case WM_SIZE: set_layout(h); InvalidateRect(h, NULL, 0); return 0;
  case WM_CTLCOLOREDIT: SetTextColor((HDC)w, C_TEXT); SetBkColor((HDC)w, C_CARD); return (LRESULT)brEdit;
  case WM_COMMAND:
    if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) { g_swFocus = GetFocus() == g_swH ? 1 : GetFocus() == g_swM ? 2 : 0; InvalidateRect(h, NULL, 0); }
    return 0;
  case WM_APP + 2: set_save(); return 0;
  case WM_APP + 3: set_close(); return 0;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(dc, bm);
    SetBkMode(dc, TRANSPARENT); FillRect(dc, &rc, brBg);
    glow(dc, rc.right - S(40), 0, S(320), S(140), RGB(8, 44, 66), C_BG);
    int M = S(22);
    text(dc, L"НАСТРОЙКИ  ·  НАПОМИНАНИЕ", fH, C_TEXT, M, S(14), rc.right - M, S(44), DT_SINGLELINE | DT_VCENTER);
    text(dc, L"Каждый день в это время BINDER покажет, какие заявки ещё в работе.", fSmall, C_MUTED, M, S(46), rc.right - M, S(86), DT_WORDBREAK);
    /* переключатель */
    RECT t = g_swToggle;
    if (g_swOn) grad_rr(dc, t.left, t.top, t.right, t.bottom, S(30), C_ACC, C_ACC2);
    else rrect(dc, t.left, t.top, t.right, t.bottom, S(30), RGB(45, 44, 50), 0, 0);
    int knob = S(11), ky = (t.top + t.bottom) / 2;
    circle(dc, g_swOn ? t.right - S(15) : t.left + S(15), ky, knob, C_TEXT);
    text(dc, g_swOn ? L"Включено" : L"Выключено", fText, g_swOn ? C_TEXT : C_MUTED, t.right + S(14), t.top, rc.right - M, t.bottom, DT_SINGLELINE | DT_VCENTER);
    /* время */
    text(dc, L"Время", fText, g_swOn ? C_MUTED : C_DIM, M, g_swBoxH.top, g_swBoxH.left - S(8), g_swBoxH.bottom, DT_SINGLELINE | DT_VCENTER);
    rrect(dc, g_swBoxH.left, g_swBoxH.top, g_swBoxH.right, g_swBoxH.bottom, S(12), C_CARD, 1, g_swFocus == 1 ? C_ACC : C_CARD_BR);
    rrect(dc, g_swBoxM.left, g_swBoxM.top, g_swBoxM.right, g_swBoxM.bottom, S(12), C_CARD, 1, g_swFocus == 2 ? C_ACC : C_CARD_BR);
    text(dc, L":", fH, C_MUTED, g_swBoxH.right, g_swBoxH.top, g_swBoxM.left, g_swBoxH.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    text(dc, L"часы : минуты", fSmall, C_DIM, g_swBoxM.right + S(14), g_swBoxH.top, rc.right - M, g_swBoxH.bottom, DT_SINGLELINE | DT_VCENTER);
    /* кнопки */
    rrect(dc, g_swTest.left, g_swTest.top, g_swTest.right, g_swTest.bottom, S(40), g_swHover == 3 ? RGB(60, 44, 24) : RGB(36, 30, 24), 1, RGB(120, 90, 50));
    text(dc, L"ТЕСТ", fBtn, RGB(255, 190, 90), g_swTest.left, g_swTest.top, g_swTest.right, g_swTest.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    rrect(dc, g_swCancel.left, g_swCancel.top, g_swCancel.right, g_swCancel.bottom, S(40), g_swHover == 1 ? RGB(40, 39, 46) : RGB(26, 25, 30), 1, RGB(62, 61, 68));
    text(dc, L"ОТМЕНА", fBtn, C_TEXT, g_swCancel.left, g_swCancel.top, g_swCancel.right, g_swCancel.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    grad_rr(dc, g_swSave.left, g_swSave.top, g_swSave.right, g_swSave.bottom, S(40), g_swHover == 2 ? C_SEC : C_ACC, C_ACC2);
    text(dc, L"СОХРАНИТЬ", fBtn, C_TEXT, g_swSave.left, g_swSave.top, g_swSave.right, g_swSave.bottom, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
    return 0;
  }
  case WM_MOUSEMOVE: {
    int x = GET_X(l), y = GET_Y(l);
    int hv = in_rect(g_swSave, x, y) ? 2 : in_rect(g_swCancel, x, y) ? 1 : in_rect(g_swTest, x, y) ? 3 : in_rect(g_swToggle, x, y) ? 4 : 0;
    if (hv != g_swHover) { g_swHover = hv; InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_SETCURSOR: {
    POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
    if (LOWORD(l) == 1 && (HWND)w == h && (in_rect(g_swSave, p.x, p.y) || in_rect(g_swCancel, p.x, p.y) || in_rect(g_swTest, p.x, p.y) || in_rect(g_swToggle, p.x, p.y))) { SetCursor(curHand); return 1; }
    break;
  }
  case WM_LBUTTONUP: {
    int x = GET_X(l), y = GET_Y(l);
    if (in_rect(g_swToggle, x, y)) { g_swOn = !g_swOn; InvalidateRect(h, NULL, 0); }
    else if (in_rect(g_swSave, x, y)) set_save();
    else if (in_rect(g_swCancel, x, y)) set_close();
    else if (in_rect(g_swTest, x, y)) show_reminder(1);
    return 0;
  }
  case WM_CLOSE: set_close(); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
static void open_settings(void) {
  if (g_sw) return;
  g_swOn = g_remOn; g_swHover = 0; g_swFocus = 0;
  RECT wr; GetWindowRect(g_wnd, &wr);
  int w = S(500), hh = S(330);
  g_sw = CreateWindowExW(0, L"RTBindSet", L"Настройки — BINDER", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                         wr.left + (wr.right - wr.left - w) / 2, wr.top + (wr.bottom - wr.top - hh) / 2, w, hh, g_wnd, NULL, g_inst, NULL);
  EnableWindow(g_wnd, 0);
  ShowWindow(g_sw, SW_SHOW);
  SetFocus(g_swH);
}

static void context_menu(int i, int sx, int sy) {
  HMENU m = CreatePopupMenu();
  if (T[i].status != 0) AppendMenuW(m, MF_STRING, 2, L"Вернуть в работу");
  if (T[i].status != 2) AppendMenuW(m, MF_STRING, 7, L"На уточнение");
  if (T[i].status != 1) AppendMenuW(m, MF_STRING, 1, L"Выполнено");
  AppendMenuW(m, MF_STRING, 6, L"Изменить…");
  AppendMenuW(m, MF_STRING, 3, L"Копировать номер");
  AppendMenuW(m, MF_STRING, 4, L"Взять номер и описание в поля ввода");
  AppendMenuW(m, MF_SEPARATOR, 0, NULL);
  AppendMenuW(m, MF_STRING, 5, L"Удалить");
  int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, sx, sy, 0, g_wnd, NULL);
  DestroyMenu(m);
  WCHAR msg[100];
  switch (r) {
  case 1: act_done(i); break;
  case 2: act_return(i); break;
  case 3: copy_text(T[i].num); wsprintfW(msg, L"Номер %s скопирован", T[i].num); toast(msg); break;
  case 4: SetWindowTextW(g_eNum, T[i].num); SetWindowTextW(g_eNote, T[i].note); SetFocus(g_eNote); break;
  case 5: act_delete(i); break;
  case 6: open_edit(i); break;
  case 7: act_wait(i); break;
  }
}

static LRESULT WINAPI WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
  case WM_CREATE: {
    g_wnd = h;
    g_eNum = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)101, g_inst, NULL);
    g_eNote = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, h, (HMENU)102, g_inst, NULL);
    SendMessageW(g_eNum, WM_SETFONT, (WPARAM)fEdit, 1); SendMessageW(g_eNote, WM_SETFONT, (WPARAM)fEdit, 1);
    SendMessageW(g_eNum, EM_SETLIMITTEXT, 30, 0); SendMessageW(g_eNote, EM_SETLIMITTEXT, 118, 0);
    SendMessageW(g_eNum, EM_SETCUEBANNER, 1, (LPARAM)L"Номер заявки");
    SendMessageW(g_eNote, EM_SETCUEBANNER, 1, (LPARAM)L"Что делаю (необязательно)");
    g_oldEdit = (WNDPROC)SetWindowLongW(g_eNum, GWL_WNDPROC, (LONG)EditProc);
    SetWindowLongW(g_eNote, GWL_WNDPROC, (LONG)EditProc);
    g_hkOk = 0;
    for (int n = 1; n <= 12; n++) if (RegisterHotKey(h, 100 + n, MOD_CONTROL | MOD_NOREPEAT, VK_F1 + n - 1)) g_hkOk++;
    SetTimer(h, 1, 100, NULL);
    SetTimer(h, 2, 30000, NULL);
    SetTimer(h, 3, 1000, NULL);
    place_edits();
    if (g_hkOk < 12) { WCHAR mm[100]; wsprintfW(mm, L"Часть Ctrl+F занята другой программой: работает %d из 12", g_hkOk); toast(mm); }
    return 0;
  }
  case WM_SIZE: place_edits(); InvalidateRect(h, NULL, 0); return 0;
  case WM_SETFOCUS: SetFocus(g_eNum); return 0;
  case WM_GETMINMAXINFO: ((MINMAXINFO *)l)->ptMinTrackSize.x = S(1000); ((MINMAXINFO *)l)->ptMinTrackSize.y = S(620); return 0;
  case WM_ERASEBKGND: return 1;
  case WM_PAINT: paint(h); return 0;
  case WM_TIMER:
    if (w == 1) {
      /* перерисовываем только область колонок и подвала (поля ввода не мерцают) */
      RECT r = {0, L.col[0].top - S(4), L.W, L.H}; InvalidateRect(h, &r, 0);
    } else if (w == 3) remind_tick();
    else save_data();
    return 0;
  case WM_CTLCOLOREDIT: SetTextColor((HDC)w, C_TEXT); SetBkColor((HDC)w, C_CARD); return (LRESULT)brEdit;
  case WM_COMMAND:
    if ((LOWORD(w) == 101 || LOWORD(w) == 102) && (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS)) {
      g_focusNum = GetFocus() == g_eNum; g_focusNote = GetFocus() == g_eNote; InvalidateRect(h, NULL, 0);
    }
    return 0;
  case WM_APP + 1: act_add(); return 0;
  case WM_HOTKEY: if (w > 100 && w <= 112) on_hotkey((int)w - 100); return 0;
  case WM_SETCURSOR:
    if (LOWORD(l) == 1 && (HWND)w == h) {
      POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
      Hit ht = hit_test(p.x, p.y);
      if (ht.kind != H_NONE && ht.kind != H_CARD) { SetCursor(curHand); return 1; }
      SetCursor(curArrow); return 1;
    }
    break;
  case WM_MOUSEMOVE: {
    int x = GET_X(l), y = GET_Y(l);
    g_mouse.x = x; g_mouse.y = y;
    if (!g_tracking) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_tracking = 1; }
    if (g_press.kind == H_CARD && !g_dragging) {
      int dx = x - g_pressPt.x, dy = y - g_pressPt.y;
      if (dx * dx + dy * dy > S(6) * S(6)) {
        g_dragging = 1; g_dragIdx = g_press.idx;
        int c = col_of(T[g_dragIdx].status);
        for (int k = 0; k < L.cnt[c]; k++) if (L.idx[c][k] == g_dragIdx) { RECT r = card_rect(c, k); g_dragDX = g_pressPt.x - r.left; g_dragDY = g_pressPt.y - r.top; }
      }
    }
    Hit ht = g_dragging ? (Hit){H_NONE, -1} : hit_test(x, y);
    if (g_dragging || ht.kind != g_hover.kind || ht.idx != g_hover.idx) { g_hover = ht; InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_MOUSELEAVE: g_tracking = 0; if (!g_dragging) { g_hover.kind = H_NONE; g_hover.idx = -1; InvalidateRect(h, NULL, 0); } return 0;
  case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
    g_pressPt.x = GET_X(l); g_pressPt.y = GET_Y(l);
    g_press = hit_test(g_pressPt.x, g_pressPt.y);
    if (g_press.kind != H_NONE) SetCapture(h);
    if (m == WM_LBUTTONDBLCLK && g_press.kind == H_CARD) {
      int i = g_press.idx; g_press.kind = H_NONE; ReleaseCapture();
      open_edit(i);
    }
    return 0;
  }
  case WM_LBUTTONUP: {
    int x = GET_X(l), y = GET_Y(l);
    ReleaseCapture();
    Hit p = g_press; g_press.kind = H_NONE;
    if (g_dragging) {
      g_dragging = 0;
      int i = g_dragIdx;
      for (int c = 0; c < 3; c++)
        if (in_rect(L.col[c], x, y) && COL_ST[c] != T[i].status) { set_status(i, COL_ST[c]); break; }
      InvalidateRect(h, NULL, 0);
      return 0;
    }
    Hit ht = hit_test(x, y);
    if (ht.kind != p.kind || ht.idx != p.idx) return 0;
    switch (ht.kind) {
    case H_ADD: act_add(); break;
    case H_BINDS: open_binds(); break;
    case H_REMIND: open_settings(); break;
    case H_CLEAR: act_clear(); break;
    case H_SITE: ShellExecuteW(h, L"open", L"https://kondrashov-m.ru", NULL, NULL, SW_SHOWNORMAL); break;
    case H_CARD_B1: act_delete(ht.idx); break;
    case H_CARD_B2: if (T[ht.idx].status == 1) act_return(ht.idx); else act_done(ht.idx); break;
    case H_CARD_B3: if (T[ht.idx].status == 0) act_wait(ht.idx); else act_return(ht.idx); break;
    }
    return 0;
  }
  case WM_RBUTTONUP: {
    Hit ht = hit_test(GET_X(l), GET_Y(l));
    if (ht.idx >= 0) { POINT p; GetCursorPos(&p); context_menu(ht.idx, p.x, p.y); }
    return 0;
  }
  case WM_MOUSEWHEEL: {
    POINT p = {GET_X(l), GET_Y(l)}; ScreenToClient(h, &p);
    int d = (short)HIWORD(w);
    for (int c = 0; c < 3; c++) if (in_rect(L.col[c], p.x, p.y)) { g_scroll[c] -= d * S(70) / 120; do_layout(); InvalidateRect(h, NULL, 0); }
    return 0;
  }
  case WM_CLOSE:
    save_data();
    if (g_bw) SendMessageW(g_bw, WM_CLOSE, 0, 0);
    DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    for (int n = 1; n <= 12; n++) UnregisterHotKey(h, 100 + n);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void WINAPI Entry(void) {
  g_inst = GetModuleHandleW(NULL);
  HANDLE mx = CreateMutexW(NULL, 0, L"Local\\kondrashov-m-rtbind");
  (void)mx;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND o = FindWindowW(L"RTBindMain", NULL);
    if (o) { ShowWindow(o, SW_RESTORE); SetForegroundWindow(o); }
    ExitProcess(0);
  }
  InitCommonControls();

  GetModuleFileNameW(NULL, g_dir, 520);
  int n = wlen(g_dir); while (n > 0 && g_dir[n - 1] != '\\') n--; g_dir[n ? n - 1 : 0] = 0;
  wsprintfW(g_data, L"%s\\rtbind_data.txt", g_dir);
  wsprintfW(g_tmp, L"%s\\rtbind_data.tmp", g_dir);
  wsprintfW(g_hist, L"%s\\rtbind_history.txt", g_dir);
  wsprintfW(g_binds, L"%s\\binds", g_dir);
  wsprintfW(g_set, L"%s\\rtbind_settings.txt", g_dir);
  CreateDirectoryW(g_binds, NULL);
  load_data();
  load_settings();

  HDC sdc = GetDC(NULL); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(NULL, sdc);
  if (g_dpi < 96) g_dpi = 96;
  fTitle = mkfont(L"Segoe UI", 34, 800);
  fBadge = mkfont(L"Segoe UI", 14, 600);
  fH = mkfont(L"Segoe UI", 16, 700);
  fNum = mkfont(L"Segoe UI", 23, 600);
  fText = mkfont(L"Segoe UI", 13, 400);
  fSmall = mkfont(L"Segoe UI", 12, 400);
  fMono = mkfont(L"Consolas", 14, 700);
  fBtn = mkfont(L"Segoe UI", 13, 700);
  fEdit = mkfont(L"Segoe UI", 15, 400);
  brEdit = CreateSolidBrush(C_CARD);
  brBg = CreateSolidBrush(C_BG);
  curArrow = LoadCursorW(NULL, IDC_ARROW);
  curHand = LoadCursorW(NULL, IDC_HAND);

  HICON ib = LoadImageW(g_inst, (const WCHAR *)1, IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CXICON), LR_DEFAULTCOLOR);
  HICON is = LoadImageW(g_inst, (const WCHAR *)1, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXSMICON), LR_DEFAULTCOLOR);
  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc); wc.style = CS_DBLCLKS; wc.lpfnWndProc = WndProc; wc.hInstance = g_inst;
  wc.hIcon = ib; wc.hIconSm = is; wc.hCursor = curArrow; wc.lpszClassName = L"RTBindMain";
  RegisterClassExW(&wc);
  wc.lpfnWndProc = BindsProc; wc.lpszClassName = L"RTBindBinds"; wc.style = 0;
  RegisterClassExW(&wc);
  wc.lpfnWndProc = EditWndProc; wc.lpszClassName = L"RTBindEdit";
  RegisterClassExW(&wc);
  wc.lpfnWndProc = SetProc; wc.lpszClassName = L"RTBindSet";
  RegisterClassExW(&wc);
  wc.lpfnWndProc = RemProc; wc.lpszClassName = L"RTBindRemind";
  RegisterClassExW(&wc);

  int W = S(1280), H = S(800);
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
  if (W > sw - 40) W = sw - 40;
  if (H > sh - 80) H = sh - 80;
  HWND h = CreateWindowExW(0, L"RTBindMain", L"BINDER — kondrashov-m", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           (sw - W) / 2, (sh - H) / 2, W, H, NULL, NULL, g_inst, NULL);
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
  SetFocus(g_eNum);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  ExitProcess(0);
}
