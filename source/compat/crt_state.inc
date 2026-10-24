typedef struct CrtState {
    DWORD page, guard, selector_flag, info_pointer;
    BYTE table[TENKA_CRT_TABLE_SIZE];
    BOOL available, stable;
} CrtState;

static BOOL MutableCrtRanges(ULONG_PTR base, const CrtState *state)
{
    /* Known selector flag, global multibyte fields/table/case map, and the
       existing multibyte-info object that the setter may reuse. */
    return WritableRange(base + TENKA_CRT_SELECTOR_RVA, 4, base) &&
           WritableRange(base + TENKA_CRT_FIELDS_RVA, TENKA_CRT_FIELDS_SIZE, base) &&
           (!state->info_pointer || WritableRange(state->info_pointer, TENKA_CRT_INFO_SIZE, base));
}
static BOOL CaptureCrtState(ULONG_PTR base, CrtState *state)
{
    DWORD i, page_after, guard_after, flag_after, pointer_after;
    ZeroBytes(state, sizeof(*state));
    if (!ImageRange(base, TENKA_CRT_PAGE_RVA, 4, FALSE) ||
        !ImageRange(base, TENKA_CRT_GUARD_RVA, 4, FALSE) ||
        !ImageRange(base, TENKA_CRT_SELECTOR_RVA, 4, FALSE) ||
        !ImageRange(base, TENKA_CRT_INFO_POINTER_RVA, 4, FALSE) ||
        !ImageRange(base, TENKA_CRT_TABLE_RVA, TENKA_CRT_TABLE_SIZE, FALSE)) return FALSE;
    state->page = *(volatile DWORD *)(base + TENKA_CRT_PAGE_RVA);
    state->guard = *(volatile DWORD *)(base + TENKA_CRT_GUARD_RVA);
    state->selector_flag = *(volatile DWORD *)(base + TENKA_CRT_SELECTOR_RVA);
    state->info_pointer = *(volatile DWORD *)(base + TENKA_CRT_INFO_POINTER_RVA);
    for (i = 0; i < TENKA_CRT_TABLE_SIZE; ++i) state->table[i] = *(volatile BYTE *)(base + TENKA_CRT_TABLE_RVA + i);
    page_after = *(volatile DWORD *)(base + TENKA_CRT_PAGE_RVA);
    guard_after = *(volatile DWORD *)(base + TENKA_CRT_GUARD_RVA);
    flag_after = *(volatile DWORD *)(base + TENKA_CRT_SELECTOR_RVA);
    pointer_after = *(volatile DWORD *)(base + TENKA_CRT_INFO_POINTER_RVA);
    state->available = TRUE;
    state->stable = page_after == state->page && guard_after == state->guard &&
                    flag_after == state->selector_flag && pointer_after == state->info_pointer;
    return state->stable;
}
static BOOL IsJapaneseCrtState(const CrtState *s)
{
    return s->available && s->stable && s->guard == TENKA_CRT_READY && s->page == TENKA_JAPANESE_CODEPAGE &&
           EqualBytes(s->table, kJapanTable, sizeof(kJapanTable));
}
