/* Tenka Sousei Direct3D 8 compatibility proxy.
 * Include order keeps the implementation in one x86 translation unit.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#if !defined(_M_IX86)
#error This proxy requires the native x86 compiler.
#endif
#pragma intrinsic(_ReturnAddress)

static HMODULE g_self;

#include "support.inc"
#include "compat/verified_inputs.h"
#include "platform/game_paths.inc"
#include "compat/integrity.inc"
#include "compat/crt_state.inc"
#include "compat/locale_fix.inc"
#include "graphics/direct3d8_forwarding.inc"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) g_self = instance;
    return TRUE;
}
