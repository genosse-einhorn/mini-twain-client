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

#include "dpihelper.h"

#include <windows.h>

static DpiHelper_AwarenessLevel (WINAPI *g_funcSetThreadDpiAwarenessContext)(DpiHelper_AwarenessLevel) = NULL;

DpiHelper_AwarenessLevel
DpiHelper_SetThreadAwareness(DpiHelper_AwarenessLevel level)
{
    if (g_funcSetThreadDpiAwarenessContext) {
        return g_funcSetThreadDpiAwarenessContext(level);
    } else {
        return (DpiHelper_AwarenessLevel)0;
    }
}

void
DpiHelper_Initialize(void)
{
    HMODULE user32 = GetModuleHandle(L"user32.dll");
    g_funcSetThreadDpiAwarenessContext = (DpiHelper_AwarenessLevel (WINAPI *)(DpiHelper_AwarenessLevel))(void*)GetProcAddress(user32, "SetThreadDpiAwarenessContext");
}
