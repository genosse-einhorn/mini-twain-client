// Copyright © 2021 Jonas Kümmerlin <jonas@kuemmerlin.eu>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <gdiplus.h>
#include "resource.h"
#include "twainhelper.h"
#include "folderbrowsehelper.h"
#include "dpihelper.h"

static Gdiplus::ImageCodecInfo *g_gdiplusEncoders;
static UINT                     g_gdiplusEncoderCount;

static void
TC_CopyFileExtension(WCHAR *buf, SIZE_T bufsize, const WCHAR *s)
{
    while (*s == '*' || *s == '.')
        ++s;

    while (*s && *s != ';' && bufsize > 1) {
        *buf++ = (WCHAR)(DWORD_PTR)CharLowerW((WCHAR *)(DWORD_PTR)*s++);
        bufsize--;
    }

    *buf = 0;
}

static void
TC_SetFileNumber(HWND hwndDlg, UINT number)
{
    WCHAR buf[5];
    wsprintf(buf, L"%04u", number % 10000);

    SetDlgItemText(hwndDlg, IDC_FILENUMBEREDIT, buf);
}

static void
TC_FixFileNumber(HWND hwndDlg)
{
    UINT n = GetDlgItemInt(hwndDlg, IDC_FILENUMBEREDIT, NULL, FALSE);
    TC_SetFileNumber(hwndDlg, n);
}

static void
TC_UpdateScanBtnState(HWND hwndDlg)
{
    BOOL enabled = TwainHelper_CurrentState() >= TH_STATE_DSM_OPEN && TwainHelper_CurrentState() < TH_STATE_SOURCE_ENABLED;
    EnableWindow(GetDlgItem(hwndDlg, IDC_SCANBTN), enabled);
}

static void
TC_ErrorDialog(HWND hwndDlg, const WCHAR *text)
{
    // HACK! if the dialog is disabled (because of another modal window,
    // or whatever), show the error dialog as topmost without a parent
    if (IsWindowEnabled(hwndDlg)) {
        MessageBox(hwndDlg, text, NULL, MB_OK|MB_ICONHAND);
    } else {
        MessageBox(NULL, text, NULL, MB_OK|MB_ICONHAND|MB_SYSTEMMODAL);
    }
}

static void
TC_BeginScan(HWND hwndDlg)
{
    TwainHelper_CloseSource();

    TW_IDENTITY source;
    ZeroMemory(&source, sizeof(source));

    if (!TwainHelper_UserSelectSource(&source)) {
        TC_ErrorDialog(hwndDlg, L"Failed to select TWAIN source");
        goto out;
    }

    if (!TwainHelper_OpenSource(&source)) {
        TC_ErrorDialog(hwndDlg, L"Failed to open TWAIN source");
        goto out;
    }

    TwainHelper_SetNumImages((TW_UINT32)-1);
    // ignore errors - worst case we only transfer one image

    if (!TwainHelper_EnableSource(hwndDlg)) {
        TC_ErrorDialog(hwndDlg, L"Failed to enable TWAIN source");
        TwainHelper_CloseSource();
    }

out:
    TC_UpdateScanBtnState(hwndDlg);
}

static void
TC_SaveImage(HWND hwndDlg, HGLOBAL hDibGlobal)
{
    int formatIndex = SendDlgItemMessage(hwndDlg,
                                         IDC_FILEFORMATCOMBO,
                                         CB_GETCURSEL,
                                         0, 0);
    if (formatIndex < 0 || (UINT)formatIndex >= g_gdiplusEncoderCount) {
        TC_ErrorDialog(hwndDlg, L"Invalid image format selected");
        return;
    }

    CLSID formatClsid = g_gdiplusEncoders[formatIndex].Clsid;

    WCHAR ext[32] = L"";

    TC_CopyFileExtension(ext, sizeof(ext)/sizeof(ext[0]), g_gdiplusEncoders[formatIndex].FilenameExtension);

    WCHAR basepath[MAX_PATH] = L"";
    GetDlgItemText(hwndDlg, IDC_FOLDEREDIT, basepath, sizeof(basepath)/sizeof(basepath[0]));

    WCHAR filename[MAX_PATH] = L"";
    GetDlgItemText(hwndDlg, IDC_FILENAMEEDIT, filename, sizeof(filename)/sizeof(filename[0]));

    UINT counter = GetDlgItemInt(hwndDlg, IDC_FILENUMBEREDIT, NULL, FALSE) % 10000;

    WCHAR path[1024] = L"";

    DWORD error = 0;
    do {
        wsprintf(path, L"%s\\%s%04u.%s", basepath, filename, counter, ext);

        counter = (counter + 1) % 10000;

        // create once with CREATE_NEW and then close, GDI+ will open it again
        HANDLE hFile = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!hFile || hFile == INVALID_HANDLE_VALUE) {
            error = GetLastError();
        } else {
            CloseHandle(hFile);
            error = 0;
        }
    } while (error == ERROR_FILE_EXISTS);

    if (error) {
        TC_ErrorDialog(hwndDlg, L"Failed to open file");
        return;
    }

    TC_SetFileNumber(hwndDlg, counter);

    BITMAPINFOHEADER *dibBuf = (BITMAPINFOHEADER *)GlobalLock(hDibGlobal);

    int paletteSize = 0;
    if (dibBuf->biBitCount == 1)
        paletteSize = 2;
    if (dibBuf->biBitCount == 4)
        paletteSize = 16;
    if (dibBuf->biBitCount == 8)
        paletteSize = 256;

    Gdiplus::Bitmap bitmap((BITMAPINFO*)dibBuf,
                           (char*)dibBuf + dibBuf->biSize + paletteSize * sizeof(RGBQUAD));
    if (bitmap.GetLastStatus() != Gdiplus::Ok)
        TC_ErrorDialog(hwndDlg, L"failed to create GDI+ bitmap");

    if (bitmap.Save(path, &formatClsid, NULL) != Gdiplus::Ok)
        TC_ErrorDialog(hwndDlg, L"failed to save file");

    GlobalUnlock(hDibGlobal);
}

static void
TC_SetDefaultFolder(HWND hwndDlg)
{
    WCHAR path[MAX_PATH] = L"C:";

    SHGetFolderPath(hwndDlg, CSIDL_MYPICTURES|CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, path);

    SetDlgItemText(hwndDlg, IDC_FOLDEREDIT, path);
}

static void
TC_BrowseForFolder(HWND hwndDlg)
{
    WCHAR old[MAX_PATH] = L"C:";
    WCHAR *selected = NULL;

    GetDlgItemText(hwndDlg, IDC_FOLDEREDIT, old, sizeof(old)/sizeof(old[0]));

    if (SUCCEEDED(FolderBrowseHelper_BrowseForFolder(hwndDlg, old, &selected))) {
        SetDlgItemText(hwndDlg, IDC_FOLDEREDIT, selected);
        CoTaskMemFree(selected);
    }
}

static INT_PTR CALLBACK
TC_MainDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            PostQuitMessage(0);
            return (INT_PTR) TRUE;
        } else if (LOWORD(wParam) == IDC_FILENUMBEREDIT && HIWORD(wParam) == EN_KILLFOCUS) {
            TC_FixFileNumber(hwndDlg);
        } else if (LOWORD(wParam) == IDC_FOLDERBROWSEBTN) {
            TC_BrowseForFolder(hwndDlg);
        } else if (LOWORD(wParam) == IDC_SCANBTN) {
            TC_BeginScan(hwndDlg);
        }
        break;
    case WM_NOTIFY:
        if (wParam == IDC_FILENUMBERUPDOWN && ((NMHDR *)lParam)->code == UDN_DELTAPOS) {
            NMUPDOWN *n = (NMUPDOWN *)lParam;
            if (n->iDelta < 0) {
                UINT n = GetDlgItemInt(hwndDlg, IDC_FILENUMBEREDIT, NULL, FALSE);
                TC_SetFileNumber(hwndDlg, n >= 9999 ? 0 : n + 1);
            }
            if (n->iDelta > 0) {
                UINT n = GetDlgItemInt(hwndDlg, IDC_FILENUMBEREDIT, NULL, FALSE);
                TC_SetFileNumber(hwndDlg, n > 0 ? n - 1 : 9999);
            }
        }
        break;
    case WM_INITDIALOG:
        TC_FixFileNumber(hwndDlg);
        TC_SetDefaultFolder(hwndDlg);
        SetDlgItemText(hwndDlg, IDC_FILENAMEEDIT, L"scan");

        for (UINT i = 0; i < g_gdiplusEncoderCount; ++i) {
            SendDlgItemMessage(hwndDlg,
                               IDC_FILEFORMATCOMBO,
                               CB_ADDSTRING,
                               (WPARAM)0,
                               (LPARAM)g_gdiplusEncoders[i].FormatDescription);
        }

        SendDlgItemMessage(hwndDlg,
                           IDC_FILEFORMATCOMBO,
                           CB_SETCURSEL,
                           (WPARAM)0,
                           (LPARAM)0);

        TC_UpdateScanBtnState(hwndDlg);

        return (INT_PTR) TRUE;
    }

    return (INT_PTR) FALSE;
}

static HANDLE
TC_CreateActivationContext(HMODULE self)
{
    ACTCTX c;
    ZeroMemory(&c, sizeof(c));

    WCHAR selfpath[MAX_PATH+1];
    ZeroMemory(selfpath, sizeof(selfpath));
    GetModuleFileName(self, selfpath, MAX_PATH);

    c.cbSize = sizeof(c);
    c.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
    c.hModule = self;
    c.lpSource = selfpath;
    c.lpResourceName = MAKEINTRESOURCE(42);

    return CreateActCtx(&c);
}

#ifdef TC_RAW_ENTRY_POINT
DWORD CALLBACK
wWinMainCRTStartup(void)
{
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandle(NULL);
    int nCmdShow = SW_SHOWDEFAULT;
#else
int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
#endif

    ULONG_PTR gdiplusToken;
    HWND hwndDlg;

    // activation context for commctl v6
    HANDLE hActCtx = TC_CreateActivationContext((HMODULE)hInstance);
    ULONG_PTR actCookie = 0;
    ActivateActCtx(hActCtx, &actCookie);

    DpiHelper_Initialize();
    DpiHelper_SetThreadAwareness(DPIHELPER_LEVEL_PER_MONITOR_AWARE_V2);

    // Init COM and OLE
    CoInitialize(NULL);

    // Init GDI+
    Gdiplus::GdiplusStartupInput gdipSi;
    ZeroMemory(&gdipSi, sizeof(gdipSi));
    gdipSi.GdiplusVersion = 1;
    Gdiplus::GpStatus s = Gdiplus::GdiplusStartup(&gdiplusToken, &gdipSi, NULL);
    if (s) {
        TC_ErrorDialog(NULL, L"GDI+ initialization failed");
    }

    UINT bytesize = 0;
    Gdiplus::GetImageEncodersSize(&g_gdiplusEncoderCount, &bytesize);
    g_gdiplusEncoders = (Gdiplus::ImageCodecInfo *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytesize);
    Gdiplus::GetImageEncoders(g_gdiplusEncoderCount, bytesize, g_gdiplusEncoders);

    // Set up the dialog
    hwndDlg = CreateDialog(hInstance,
                           MAKEINTRESOURCE(IDD_MAINWINDOW),
                           NULL,
                           TC_MainDialogProc);

    // Setup TWAIN
    if (!TwainHelper_Initialize(hwndDlg)) {
        TC_ErrorDialog(hwndDlg, L"Failed to initialize TWAIN");
    }

    TC_UpdateScanBtnState(hwndDlg);

    ShowWindow(hwndDlg, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TW_UINT16 TWMessage = MSG_NULL;
        if (TwainHelper_IsTwainMessage(&msg, &TWMessage)) {
            switch (TWMessage) {
            case MSG_XFERREADY:
                for (;;) {
                    HGLOBAL hBitmap = TwainHelper_BeginTransferImage();
                    if (hBitmap) {
                        TC_SaveImage(hwndDlg, hBitmap);
                        GlobalFree(hBitmap);
                        TwainHelper_EndTransferImage();
                    } else if (TwainHelper_CurrentState() == TH_STATE_TRANSFER_READY) {
                        // something went wrong
                        TC_ErrorDialog(hwndDlg, L"Failed to transfer image");
                        TwainHelper_AbortPendingTransfers();
                        break;
                    } else {
                        // all images transferred
                        break;
                    }
                }
                break;
            case MSG_CLOSEDSREQ:
                TwainHelper_CloseSource();
                TC_UpdateScanBtnState(hwndDlg);
                break;
            }
        } else {
            if (!IsDialogMessage(hwndDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    TwainHelper_Teardown(hwndDlg);

    DestroyWindow(hwndDlg);

    HeapFree(GetProcessHeap(), 0, g_gdiplusEncoders);
    Gdiplus::GdiplusShutdown(gdiplusToken);

    CoUninitialize();

    DeactivateActCtx(0, actCookie);
    ReleaseActCtx(hActCtx);

#ifdef TC_RAW_ENTRY_POINT
    ExitProcess(msg.wParam);
#else
    return (int)msg.wParam;
#endif
}
