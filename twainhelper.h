#pragma once

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
#include "twain.h"

enum TwainHelperState {
    TH_STATE_DSM_LOADED = 2, // we link against twain_32.dll, so this is always given
    TH_STATE_DSM_OPEN = 3,
    TH_STATE_SOURCE_OPEN = 4,
    TH_STATE_SOURCE_ENABLED = 5,
    TH_STATE_TRANSFER_READY = 6,
    TH_STATE_TRANSFERRING = 7
};

enum TwainHelperState
TwainHelper_CurrentState(void);

BOOL
TwainHelper_Initialize(HWND hwndParentWindow);

void
TwainHelper_Teardown(HWND hwndParentWindow);

BOOL
TwainHelper_OpenSource(const TW_IDENTITY *pSource);

void
TwainHelper_CloseSource(void);

void
TwainHelper_DisableSource(void);

BOOL
TwainHelper_SetNumImages(TW_UINT32 numImages);

BOOL
TwainHelper_EnableSource(HWND hwndParentWindow);

BOOL
TwainHelper_UserSelectSource(TW_IDENTITY *pSource);

// TwainHelper_IsTwainMessage() needs to be called at the top of the message loop.
// If it returns TRUE, you need to check *pTWMessage for what to do:
//      MSG_XFERREADY  -> Call TwainHelper_BeginTransferImage
//      MSG_CLOSEDSREQ -> Call TwainHelper_CloseSource
//      otherwise: Do nothing
// If it returns FALSE, do your normal message processing (IsDialogMessage, TranslateMessage, DispatchMessage, etc.)
BOOL
TwainHelper_IsTwainMessage(MSG *pMsg, TW_UINT16 *pTWMessage);

HGLOBAL
TwainHelper_BeginTransferImage(void);

// returns the number of transfers left
TW_UINT16
TwainHelper_EndTransferImage(void);

BOOL
TwainHelper_AbortPendingTransfers(void);

