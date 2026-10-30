/* Минимальные объявления WinAPI (без Windows SDK). Сборка: i686, без CRT. */
#pragma once
typedef unsigned int UINT;
typedef unsigned long DWORD, ULONG;
typedef int BOOL;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef long LONG;
typedef unsigned short WCHAR;
typedef void *HANDLE, *HWND, *HDC, *HINSTANCE, *HMODULE, *HICON, *HCURSOR, *HBRUSH,
    *HFONT, *HGDIOBJ, *HBITMAP, *HRGN, *HMENU, *HPEN, *HGLOBAL;
typedef unsigned int UINT_PTR, WPARAM;
typedef long LONG_PTR, LPARAM, LRESULT;
typedef unsigned long ULONG_PTR;
typedef unsigned short ATOM;
typedef unsigned long long ULONGLONG;
typedef DWORD COLORREF;

#define WINAPI __stdcall
#define IMP __declspec(dllimport)
#define NULL ((void *)0)

typedef LRESULT(WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct {
  UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
  HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; const WCHAR *lpszMenuName, *lpszClassName; HICON hIconSm;
} WNDCLASSEXW;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { LONG x, y; WORD Red, Green, Blue, Alpha; } TRIVERTEX;
typedef struct { ULONG UpperLeft, LowerRight; } GRADIENT_RECT;
typedef struct { ULONG Vertex1, Vertex2, Vertex3; } GRADIENT_TRIANGLE;
typedef struct { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; } TRACKMOUSEEVENT;
typedef struct { WORD wVk, wScan; DWORD dwFlags, time; ULONG_PTR dwExtraInfo; } KEYBDINPUT;
typedef struct { DWORD type; KEYBDINPUT ki; DWORD pad[2]; } INPUT; /* 28 байт на x86 */

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#define LOWORD(l) ((WORD)((DWORD)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define GET_X(l) ((int)(short)LOWORD(l))
#define GET_Y(l) ((int)(short)HIWORD(l))

/* kernel32 */
IMP DWORD WINAPI GetModuleFileNameW(HMODULE, WCHAR *, DWORD);
IMP HMODULE WINAPI GetModuleHandleW(const WCHAR *);
IMP HANDLE WINAPI CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
IMP BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
IMP BOOL WINAPI CloseHandle(HANDLE);
IMP DWORD WINAPI GetFileSize(HANDLE, DWORD *);
IMP DWORD WINAPI SetFilePointer(HANDLE, LONG, LONG *, DWORD);
IMP BOOL WINAPI CreateDirectoryW(const WCHAR *, void *);
IMP BOOL WINAPI MoveFileExW(const WCHAR *, const WCHAR *, DWORD);
IMP void WINAPI GetSystemTimeAsFileTime(FILETIME *);
IMP BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *, FILETIME *);
IMP BOOL WINAPI FileTimeToSystemTime(const FILETIME *, SYSTEMTIME *);
IMP DWORD WINAPI GetTickCount(void);
IMP void WINAPI Sleep(DWORD);
IMP void WINAPI ExitProcess(UINT);
IMP HANDLE WINAPI CreateMutexW(void *, BOOL, const WCHAR *);
IMP DWORD WINAPI GetLastError(void);
IMP int WINAPI MultiByteToWideChar(UINT, DWORD, const char *, int, WCHAR *, int);
IMP int WINAPI WideCharToMultiByte(UINT, DWORD, const WCHAR *, int, char *, int, const char *, BOOL *);
IMP HGLOBAL WINAPI GlobalAlloc(UINT, ULONG_PTR);
IMP void *WINAPI GlobalLock(HGLOBAL);
IMP BOOL WINAPI GlobalUnlock(HGLOBAL);
IMP int WINAPI MulDiv(int, int, int);
IMP void WINAPI GetLocalTime(SYSTEMTIME *);

/* user32 */
IMP ATOM WINAPI RegisterClassExW(const WNDCLASSEXW *);
IMP HWND WINAPI CreateWindowExW(DWORD, const WCHAR *, const WCHAR *, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
IMP LRESULT WINAPI DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
IMP BOOL WINAPI GetMessageW(MSG *, HWND, UINT, UINT);
IMP BOOL WINAPI TranslateMessage(const MSG *);
IMP LRESULT WINAPI DispatchMessageW(const MSG *);
IMP void WINAPI PostQuitMessage(int);
IMP BOOL WINAPI ShowWindow(HWND, int);
IMP BOOL WINAPI UpdateWindow(HWND);
IMP HCURSOR WINAPI LoadCursorW(HINSTANCE, const WCHAR *);
IMP HANDLE WINAPI LoadImageW(HINSTANCE, const WCHAR *, UINT, int, int, UINT);
IMP HDC WINAPI BeginPaint(HWND, PAINTSTRUCT *);
IMP BOOL WINAPI EndPaint(HWND, const PAINTSTRUCT *);
IMP BOOL WINAPI GetClientRect(HWND, RECT *);
IMP BOOL WINAPI GetWindowRect(HWND, RECT *);
IMP BOOL WINAPI InvalidateRect(HWND, const RECT *, BOOL);
IMP UINT_PTR WINAPI SetTimer(HWND, UINT_PTR, UINT, void *);
IMP BOOL WINAPI KillTimer(HWND, UINT_PTR);
IMP BOOL WINAPI RegisterHotKey(HWND, int, UINT, UINT);
IMP BOOL WINAPI UnregisterHotKey(HWND, int);
IMP short WINAPI GetAsyncKeyState(int);
IMP UINT WINAPI SendInput(UINT, INPUT *, int);
IMP LONG WINAPI SetWindowLongW(HWND, int, LONG);
IMP LRESULT WINAPI CallWindowProcW(WNDPROC, HWND, UINT, WPARAM, LPARAM);
IMP LRESULT WINAPI SendMessageW(HWND, UINT, WPARAM, LPARAM);
IMP BOOL WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM);
IMP HWND WINAPI SetFocus(HWND);
IMP HWND WINAPI GetFocus(void);
IMP int WINAPI GetWindowTextW(HWND, WCHAR *, int);
IMP BOOL WINAPI SetWindowTextW(HWND, const WCHAR *);
IMP BOOL WINAPI MoveWindow(HWND, int, int, int, int, BOOL);
IMP HWND WINAPI SetCapture(HWND);
IMP BOOL WINAPI ReleaseCapture(void);
IMP BOOL WINAPI GetCursorPos(POINT *);
IMP BOOL WINAPI ScreenToClient(HWND, POINT *);
IMP HMENU WINAPI CreatePopupMenu(void);
IMP BOOL WINAPI AppendMenuW(HMENU, UINT, UINT_PTR, const WCHAR *);
IMP BOOL WINAPI TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT *);
IMP BOOL WINAPI DestroyMenu(HMENU);
IMP int WINAPI MessageBoxW(HWND, const WCHAR *, const WCHAR *, UINT);
IMP BOOL WINAPI OpenClipboard(HWND);
IMP BOOL WINAPI EmptyClipboard(void);
IMP HANDLE WINAPI SetClipboardData(UINT, HANDLE);
IMP BOOL WINAPI CloseClipboard(void);
IMP int __cdecl wsprintfW(WCHAR *, const WCHAR *, ...);
IMP HWND WINAPI FindWindowW(const WCHAR *, const WCHAR *);
IMP BOOL WINAPI SetForegroundWindow(HWND);
IMP HDC WINAPI GetDC(HWND);
IMP int WINAPI ReleaseDC(HWND, HDC);
IMP BOOL WINAPI DestroyWindow(HWND);
IMP int WINAPI FillRect(HDC, const RECT *, HBRUSH);
IMP int WINAPI DrawTextW(HDC, const WCHAR *, int, RECT *, UINT);
IMP int WINAPI GetSystemMetrics(int);
IMP BOOL WINAPI TrackMouseEvent(TRACKMOUSEEVENT *);
IMP HCURSOR WINAPI SetCursor(HCURSOR);
IMP BOOL WINAPI IsWindow(HWND);
IMP BOOL WINAPI EnableWindow(HWND, BOOL);
IMP BOOL WINAPI MessageBeep(UINT);
IMP BOOL WINAPI SystemParametersInfoW(UINT, UINT, void *, UINT);
IMP BOOL WINAPI SetWindowPos(HWND, HWND, int, int, int, int, UINT);

/* gdi32 */
IMP HFONT WINAPI CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const WCHAR *);
IMP HGDIOBJ WINAPI SelectObject(HDC, HGDIOBJ);
IMP BOOL WINAPI DeleteObject(HGDIOBJ);
IMP int WINAPI SetBkMode(HDC, int);
IMP COLORREF WINAPI SetTextColor(HDC, COLORREF);
IMP COLORREF WINAPI SetBkColor(HDC, COLORREF);
IMP HBRUSH WINAPI CreateSolidBrush(COLORREF);
IMP HGDIOBJ WINAPI GetStockObject(int);
IMP COLORREF WINAPI SetDCBrushColor(HDC, COLORREF);
IMP COLORREF WINAPI SetDCPenColor(HDC, COLORREF);
IMP BOOL WINAPI RoundRect(HDC, int, int, int, int, int, int);
IMP BOOL WINAPI Ellipse(HDC, int, int, int, int);
IMP HDC WINAPI CreateCompatibleDC(HDC);
IMP HBITMAP WINAPI CreateCompatibleBitmap(HDC, int, int);
IMP BOOL WINAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
IMP BOOL WINAPI DeleteDC(HDC);
IMP HRGN WINAPI CreateRoundRectRgn(int, int, int, int, int, int);
IMP int WINAPI SelectClipRgn(HDC, HRGN);
IMP int WINAPI IntersectClipRect(HDC, int, int, int, int);
IMP BOOL WINAPI MoveToEx(HDC, int, int, POINT *);
IMP BOOL WINAPI LineTo(HDC, int, int);
IMP HPEN WINAPI CreatePen(int, int, COLORREF);
IMP int WINAPI GetDeviceCaps(HDC, int);
IMP BOOL WINAPI GetTextExtentPoint32W(HDC, const WCHAR *, int, SIZE *);

/* msimg32 / shell32 / comctl32 */
IMP BOOL WINAPI GradientFill(HDC, TRIVERTEX *, ULONG, void *, ULONG, ULONG);
IMP HINSTANCE WINAPI ShellExecuteW(HWND, const WCHAR *, const WCHAR *, const WCHAR *, const WCHAR *, int);
IMP void WINAPI InitCommonControls(void);

/* константы */
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_SETFOCUS 0x0007
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_SETFONT 0x0030
#define WM_CONTEXTMENU 0x007B
#define WM_KEYDOWN 0x0100
#define WM_CHAR 0x0102
#define WM_COMMAND 0x0111
#define WM_TIMER 0x0113
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_CTLCOLORSTATIC 0x0138
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONUP 0x0205
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELEAVE 0x02A3
#define WM_HOTKEY 0x0312
#define WM_APP 0x8000

#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_OVERLAPPED 0x00000000
#define WS_CAPTION 0x00C00000
#define WS_SYSMENU 0x00080000
#define WS_MINIMIZEBOX 0x00020000
#define WS_THICKFRAME 0x00040000
#define WS_CHILD 0x40000000
#define WS_VISIBLE 0x10000000
#define WS_CLIPCHILDREN 0x02000000
#define WS_TABSTOP 0x00010000
#define WS_VSCROLL 0x00200000
#define ES_MULTILINE 0x0004
#define ES_AUTOVSCROLL 0x0040
#define ES_AUTOHSCROLL 0x0080
#define ES_WANTRETURN 0x1000
#define LBS_NOTIFY 0x0001
#define LBS_NOINTEGRALHEIGHT 0x0100
#define LBN_SELCHANGE 1
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_SETITEMHEIGHT 0x01A0
#define EM_SETLIMITTEXT 0x00C5
#define EM_SETCUEBANNER 0x1501
#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define CW_USEDEFAULT ((int)0x80000000)
#define GWL_WNDPROC (-4)
#define SW_SHOW 5
#define SW_RESTORE 9
#define SW_SHOWNORMAL 1
#define CS_DBLCLKS 0x0008
#define IDC_ARROW ((const WCHAR *)32512)
#define IDC_HAND ((const WCHAR *)32649)
#define IMAGE_ICON 1
#define LR_DEFAULTCOLOR 0
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXICON 11
#define SM_CXSMICON 49
#define TRANSPARENT 1
#define OPAQUE 2
#define NULL_PEN 8
#define DC_BRUSH 18
#define DC_PEN 19
#define PS_SOLID 0
#define SRCCOPY 0x00CC0020
#define LOGPIXELSX 88
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_RIGHT 0x0002
#define DT_VCENTER 0x0004
#define DT_WORDBREAK 0x0010
#define DT_SINGLELINE 0x0020
#define DT_NOPREFIX 0x0800
#define DT_END_ELLIPSIS 0x8000
#define GRADIENT_FILL_RECT_H 0
#define GRADIENT_FILL_RECT_V 1
#define GRADIENT_FILL_TRIANGLE 2
#define TME_LEAVE 2
#define MF_STRING 0
#define MF_SEPARATOR 0x800
#define TPM_RETURNCMD 0x100
#define TPM_RIGHTBUTTON 0x2
#define MB_YESNO 0x4
#define MB_OK 0
#define MB_ICONQUESTION 0x20
#define MB_ICONWARNING 0x30
#define MB_ICONINFORMATION 0x40
#define IDYES 6
#define CF_UNICODETEXT 13
#define GMEM_MOVEABLE 2
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_END 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define ERROR_ALREADY_EXISTS 183
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 8
#define MOD_CONTROL 0x0002
#define MOD_NOREPEAT 0x4000
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_F1 0x70
#define INPUT_KEYBOARD 1
#define KEYEVENTF_KEYUP 0x0002
#define KEYEVENTF_UNICODE 0x0004
#define SWP_NOZORDER 0x0004
#define SWP_NOSIZE 0x0001

#define WS_POPUP 0x80000000
#define WS_EX_TOPMOST 0x00000008
#define WS_EX_TOOLWINDOW 0x00000080
#define HWND_TOPMOST ((HWND)(LONG_PTR)-1)
#define SW_SHOWNOACTIVATE 4
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define ES_CENTER 0x0001
#define ES_NUMBER 0x2000
#define SPI_GETWORKAREA 0x0030
#define NULL_BRUSH_ID 5
