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

#include "twainhelper.h"

#include "dpihelper.h"

#include <windows.h>

static TW_IDENTITY           g_twainApp;
static TW_IDENTITY           g_twainSource;
static enum TwainHelperState g_twainState = TH_STATE_DSM_LOADED;

static TW_UINT16
TwainHelper_CallDSM(pTW_IDENTITY pDest,
                    TW_UINT32    DG,
                    TW_UINT16    DAT,
                    TW_UINT16    MSG,
                    TW_MEMREF    pData)
{
    // XXX: activate the original, empty activation context
    // so TWAIN won’t get unsolicited commctlv6
    ULONG_PTR actCookie = 0;
    ActivateActCtx(NULL, &actCookie);

    // XXX: disable high-dpi since lots of TWAIN sources can’t deal with it
    DpiHelper_AwarenessLevel oldDpiLevel = DpiHelper_SetThreadAwareness(DPIHELPER_LEVEL_UNAWARE);

    TW_UINT16 r = DSM_Entry(&g_twainApp, pDest, DG, DAT, MSG, pData);

    DpiHelper_SetThreadAwareness(oldDpiLevel);

    DeactivateActCtx(0, actCookie);

    return r;
}

enum TwainHelperState
TwainHelper_CurrentState(void)
{
    return g_twainState;
}

BOOL
TwainHelper_IsTwainMessage(MSG *pMsg, TW_UINT16 *pTWMessage)
{
    if (g_twainState < TH_STATE_SOURCE_ENABLED)
        return FALSE;

    TW_EVENT ev;
    ZeroMemory(&ev, sizeof(ev));
    ev.pEvent = pMsg;
    ev.TWMessage = MSG_NULL;

    if (TwainHelper_CallDSM(&g_twainSource,
                            DG_CONTROL,
                            DAT_EVENT,
                            MSG_PROCESSEVENT,
                            &ev) == TWRC_DSEVENT) {
        *pTWMessage = ev.TWMessage;

        if (ev.TWMessage == MSG_XFERREADY && g_twainState < TH_STATE_TRANSFER_READY) {
            g_twainState = TH_STATE_TRANSFER_READY;
        }

        return TRUE;
    } else {
        *pTWMessage = MSG_NULL;
        return FALSE;
    }
}

BOOL
TwainHelper_Initialize(HWND hwndDlg)
{
    g_twainApp.Id = 0;
    g_twainApp.Version.MajorNum = 1;
    g_twainApp.Version.MinorNum = 0;
    g_twainApp.Version.Language = TWLG_USA;
    g_twainApp.Version.Country = TWCY_USA;
    lstrcpyA(g_twainApp.Version.Info, "1.0");
    g_twainApp.ProtocolMajor = TWON_PROTOCOLMAJOR;
    g_twainApp.ProtocolMinor = TWON_PROTOCOLMINOR;
    g_twainApp.SupportedGroups = DG_IMAGE | DG_CONTROL;
    lstrcpyA(g_twainApp.Manufacturer, "Genosse Einhorn");
    lstrcpyA(g_twainApp.ProductFamily, "Example");
    lstrcpyA(g_twainApp.ProductName, "TWAIN Example Application");

    // We link against twain_32.dll because we won’t work without it anyway.
    // If TWAIN is not necessary for your app, you should LoadLibrary() it.
    g_twainState = TH_STATE_DSM_LOADED;

    // open the DSM
    if (TwainHelper_CallDSM(NULL, DG_CONTROL, DAT_PARENT, MSG_OPENDSM, &hwndDlg) == TWRC_SUCCESS) {
        // FIXME: handle error
        g_twainState = TH_STATE_DSM_OPEN;
        return TRUE;
    }

    return FALSE;
}

void
TwainHelper_Teardown(HWND hwndDlg)
{
    // disable and close the source
    TwainHelper_CloseSource();

    // close DSM
    if (g_twainState >= TH_STATE_DSM_OPEN) {
        TwainHelper_CallDSM(NULL, DG_CONTROL, DAT_PARENT, MSG_CLOSEDSM, &hwndDlg);
    }

    g_twainState = TH_STATE_DSM_LOADED;
}

void
TwainHelper_CloseSource(void)
{
    TwainHelper_DisableSource();

    if (g_twainState >= TH_STATE_SOURCE_OPEN) {
        TwainHelper_CallDSM(NULL, DG_CONTROL, DAT_IDENTITY, MSG_CLOSEDS, &g_twainSource);
        g_twainState = TH_STATE_DSM_OPEN;
    }
}

void
TwainHelper_DisableSource(void)
{
    TwainHelper_AbortPendingTransfers();

    if (g_twainState >= TH_STATE_SOURCE_ENABLED) {
        TW_USERINTERFACE twUI;
        ZeroMemory(&twUI, sizeof(twUI));
        TwainHelper_CallDSM(&g_twainSource, DG_CONTROL, DAT_USERINTERFACE, MSG_DISABLEDS, &twUI);
        g_twainState = TH_STATE_SOURCE_OPEN;
    }
}

BOOL
TwainHelper_UserSelectSource(TW_IDENTITY *pSource)
{
    if (g_twainState < TH_STATE_DSM_OPEN || g_twainState >= TH_STATE_SOURCE_OPEN)
        return FALSE;

    return TwainHelper_CallDSM(NULL,
                               DG_CONTROL,
                               DAT_IDENTITY,
                               MSG_USERSELECT,
                               pSource) == TWRC_SUCCESS;
}

BOOL
TwainHelper_OpenSource(const TW_IDENTITY *pSource)
{
    if (g_twainState < TH_STATE_DSM_OPEN || g_twainState >= TH_STATE_SOURCE_OPEN)
        return FALSE;

    g_twainSource = *pSource;

    if (TwainHelper_CallDSM(NULL,
                            DG_CONTROL,
                            DAT_IDENTITY,
                            MSG_OPENDS,
                            &g_twainSource) == TWRC_SUCCESS) {
        g_twainState = TH_STATE_SOURCE_OPEN;
        return TRUE;
    } else {
        return FALSE;
    }
}

BOOL
TwainHelper_SetNumImages(TW_UINT32 numImages)
{
    if (g_twainState < TH_STATE_SOURCE_OPEN)
        return FALSE;

    TW_CAPABILITY   twCapability;
    pTW_ONEVALUE    pval;

    twCapability.Cap = CAP_XFERCOUNT;
    twCapability.ConType = TWON_ONEVALUE;
    twCapability.hContainer = GlobalAlloc(GHND, sizeof(TW_ONEVALUE));
    pval = (pTW_ONEVALUE) GlobalLock(twCapability.hContainer);
    pval->ItemType = TWTY_INT16;
    pval->Item = numImages;
    GlobalUnlock(twCapability.hContainer);

    TW_UINT16 s = TwainHelper_CallDSM(&g_twainSource,
                                      DG_CONTROL,
                                      DAT_CAPABILITY,
                                      MSG_SET,
                                      &twCapability);

    GlobalFree((HANDLE)twCapability.hContainer);

    return s == TWRC_SUCCESS;
}

BOOL
TwainHelper_EnableSource(HWND hwndDlg)
{
    if (g_twainState < TH_STATE_SOURCE_OPEN)
        return FALSE;

    if (g_twainState >= TH_STATE_SOURCE_ENABLED)
        return TRUE;

    TW_USERINTERFACE twUI;
    ZeroMemory(&twUI, sizeof(twUI));
    twUI.ShowUI = TRUE;
    twUI.hParent = hwndDlg;

    if (TwainHelper_CallDSM(&g_twainSource,
                            DG_CONTROL,
                            DAT_USERINTERFACE,
                            MSG_ENABLEDS,
                            &twUI) == TWRC_SUCCESS) {
        g_twainState = TH_STATE_SOURCE_ENABLED;
        return TRUE;
    } else {
        // FIXME! handle errors
        return FALSE;
    }
}

HGLOBAL
TwainHelper_BeginTransferImage(void)
{
    if (g_twainState >= TH_STATE_TRANSFERRING)
        TwainHelper_EndTransferImage();

    if (g_twainState < TH_STATE_TRANSFER_READY)
        return NULL;

    HGLOBAL hBitmap = NULL;
    USHORT rc = TwainHelper_CallDSM(&g_twainSource,
                                    DG_IMAGE,
                                    DAT_IMAGENATIVEXFER,
                                    MSG_GET, &hBitmap);
    if (rc == TWRC_XFERDONE) {
        g_twainState = TH_STATE_TRANSFERRING;
        return hBitmap;
    } else {
        GlobalFree(hBitmap);
        return NULL;
    }
}

TW_UINT16
TwainHelper_EndTransferImage(void)
{
    if (g_twainState < TH_STATE_TRANSFERRING)
        return (TW_UINT16)-1;

    TW_PENDINGXFERS twPendingXfers;
    ZeroMemory(&twPendingXfers, sizeof(twPendingXfers));
    TwainHelper_CallDSM(&g_twainSource,
                        DG_CONTROL,
                        DAT_PENDINGXFERS,
                        MSG_ENDXFER,
                        &twPendingXfers);

    g_twainState = twPendingXfers.Count > 0 ? TH_STATE_TRANSFER_READY : TH_STATE_SOURCE_ENABLED;

    return twPendingXfers.Count;
}

BOOL
TwainHelper_AbortPendingTransfers(void)
{
    if (g_twainState < TH_STATE_SOURCE_ENABLED)
        return FALSE;

    if (g_twainState >= TH_STATE_TRANSFERRING)
        TwainHelper_EndTransferImage();

    TW_PENDINGXFERS twPendingXfers;
    ZeroMemory(&twPendingXfers, sizeof(twPendingXfers));
    TW_UINT16 s = TwainHelper_CallDSM(&g_twainSource,
                                      DG_CONTROL,
                                      DAT_PENDINGXFERS,
                                      MSG_RESET,
                                      &twPendingXfers);

    g_twainState = TH_STATE_SOURCE_ENABLED;
    return s == TWRC_SUCCESS;
}

