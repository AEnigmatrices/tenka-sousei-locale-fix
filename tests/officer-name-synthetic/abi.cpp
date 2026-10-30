#include "model.h"
using namespace synthetic;

extern "C" {
uint32_t g_eval_mod16 = 0, g_adapter_mod16 = 0, g_assign_mod16 = 0;
bool g_assignment_stack_ok = true;
static ProbeResult *probe_result;
static uint32_t probe_frame, probe_padding;

// Authored synthetic thunks only. No extracted game instructions or address calls.
__declspec(naked) void MockEvaluatorAbi() {
    __asm {
        mov eax, esp
        and eax, 15
        mov g_eval_mod16, eax
        mov eax, esp
        push dword ptr [eax + 12]
        push dword ptr [eax + 8]
        push dword ptr [eax + 4]
        push ecx
        call MockEvaluatorCore
        add esp, 16
        ret 12
    }
}
__declspec(naked) void AdapterAbi() {
    __asm {
        mov eax, esp
        and eax, 15
        mov g_adapter_mod16, eax
        mov eax, esp
        push dword ptr [eax + 12]
        push dword ptr [eax + 8]
        push dword ptr [eax + 4]
        push ecx
        call AdapterCore
        add esp, 16
        ret 12
    }
}
__declspec(naked) void MockAssignmentAbi() {
    __asm {
        mov eax, esp
        and eax, 15
        mov g_assign_mod16, eax
        mov eax, esp
        push dword ptr [eax + 8]
        push dword ptr [eax + 4]
        push ecx
        call AssignmentCore
        add esp, 12
        ret 8
    }
}
__declspec(naked) void BadRegistersControl() {
    __asm { xor ebx, ebx }
    __asm {
        xor esi, esi
    }
    __asm { xor edi, edi }
    __asm {
        xor ebp, ebp
    }
    __asm { xor eax, eax }
    __asm {ret 12}
}
__declspec(naked) void BadCleanupControl() {
    __asm { xor eax, eax }
    __asm {ret 8}
}
__declspec(naked) void BadDirectionControl() {
    __asm { std }
    __asm {
        xor eax, eax
    }
    __asm { ret 12 }
}
// Probe is single-threaded and may not be recursively called. The tested adapter
// can reenter through InvokeAdapter, which deliberately does not nest this probe.
__declspec(naked) void Probe(Entry, Manager *, NameString *, NameString *, uint32_t, ProbeResult *,
                             uint32_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 32
        mov eax, dword ptr [ebp + 28]
        mov probe_result, eax
        mov probe_frame, ebp
        mov dword ptr [eax + 4], esp
        mov dword ptr [ebp - 16], 0x6a88c431
        mov dword ptr [ebp - 20], 0x28dfac19
        mov edx, dword ptr [ebp + 32]
        mov probe_padding, edx
        sub esp, edx
        push dword ptr [ebp + 24]
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        mov ecx, dword ptr [ebp + 12]
        mov eax, dword ptr [ebp + 8]
        mov ebx, 0x11223344
        mov esi, 0x55667788
        mov edi, 0x99aabbcc
        mov ebp, 0x1234abcd
        cld
        call eax
        mov edx, probe_result
        mov dword ptr [edx + 0], eax
        mov dword ptr [edx + 12], ebx
        mov dword ptr [edx + 16], esi
        mov dword ptr [edx + 20], edi
        mov dword ptr [edx + 24], ebp
        pushfd
        pop eax
        mov dword ptr [edx + 28], eax
        cld // record the callee DF first; protect the test caller even on failure
        add esp, probe_padding
        mov dword ptr [edx + 8], esp
        mov ecx, probe_frame
        xor eax, eax
        cmp dword ptr [ecx - 16], 0x6a88c431
        jne canary_done
        cmp dword ptr [ecx - 20], 0x28dfac19
        jne canary_done
        mov eax, 1
    canary_done:
        mov dword ptr [edx + 32], eax
        lea esp, [ecx - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void BadAssignmentRegistersControl() {
    __asm { xor ebx, ebx }
    __asm {
        xor esi, esi
    }
    __asm { xor edi, edi }
    __asm {
        xor ebp, ebp
    }
    __asm { xor eax, eax }
    __asm {ret 8}
}
__declspec(naked) void BadAssignmentCleanupControl() {
    __asm { xor eax, eax }
    __asm {ret 4}
}
__declspec(naked) void BadAssignmentDirectionControl() {
    __asm { std }
    __asm {
        xor eax, eax
    }
    __asm { ret 8 }
}
__declspec(naked) void ProbeAssignment(Entry, NameString *, const char *, uint32_t, ProbeResult *,
                                       uint32_t) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 32
        mov eax, dword ptr [ebp + 24]
        mov probe_result, eax
        mov probe_frame, ebp
        mov dword ptr [eax + 4], esp
        mov dword ptr [ebp - 16], 0x6a88c431
        mov dword ptr [ebp - 20], 0x28dfac19
        mov edx, dword ptr [ebp + 28]
        mov probe_padding, edx
        sub esp, edx
        push dword ptr [ebp + 20]
        push dword ptr [ebp + 16]
        mov ecx, dword ptr [ebp + 12]
        mov eax, dword ptr [ebp + 8]
        mov ebx, 0x11223344
        mov esi, 0x55667788
        mov edi, 0x99aabbcc
        mov ebp, 0x1234abcd
        cld
        call eax
        mov edx, probe_result
        mov dword ptr [edx + 0], eax
        mov dword ptr [edx + 12], ebx
        mov dword ptr [edx + 16], esi
        mov dword ptr [edx + 20], edi
        mov dword ptr [edx + 24], ebp
        pushfd
        pop eax
        mov dword ptr [edx + 28], eax
        cld // record the callee DF first; protect the test caller even on failure
        add esp, probe_padding
        mov dword ptr [edx + 8], esp
        mov ecx, probe_frame
        xor eax, eax
        cmp dword ptr [ecx - 16], 0x6a88c431
        jne canary_done
        cmp dword ptr [ecx - 20], 0x28dfac19
        jne canary_done
        mov eax, 1
    canary_done:
        mov dword ptr [edx + 32], eax
        lea esp, [ecx - 12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
}
namespace synthetic {
__declspec(noinline) uint32_t InvokeEvaluator(Manager *m, NameString *expr, NameString *destination,
                                              uint32_t flag) {
    uint32_t result = 0;
    __asm {
        push flag
        push destination
        push expr
        mov ecx, m
        call MockEvaluatorAbi
        mov result, eax
    }
    return result;
}
__declspec(noinline) uint32_t InvokeAdapter(Manager *m, NameString *expr, NameString *destination,
                                            uint32_t flag) {
    uint32_t result = 0;
    __asm {
        push flag
        push destination
        push expr
        mov ecx, m
        call AdapterAbi
        mov result, eax
    }
    return result;
}
__declspec(noinline) NameString *InvokeAssignment(NameString *destination, const char *bytes,
                                                  uint32_t byte_count) {
    NameString *result = nullptr;
    uint32_t before = 0, after = 0;
    __asm {
        mov before, esp
        push byte_count
        push bytes
        mov ecx, destination
        call MockAssignmentAbi
        mov result, eax
        mov after, esp
    }
    g_assignment_stack_ok = g_assignment_stack_ok && before == after;
    return result;
}
} // namespace synthetic
