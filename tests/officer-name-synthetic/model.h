#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>

#if !defined(_M_IX86)
#error Synthetic ABI tests require MSVC x86.
#endif

namespace synthetic {
enum class Kind {
    Bytes,
    String,
    Officer,
    Manager,
    Resource,
    Context,
    Slots,
    Commands,
    Environment,
    Heap
};
struct Block {
    unsigned char *pages;
    unsigned char *data;
    size_t size;
    Kind kind;
    bool live;
};
struct View {
    const void *data;
    size_t size;
    Kind kind;
    bool writable;
};
class Arena {
    std::vector<Block> blocks_;
    std::vector<View> views_;
    size_t page_ = 0;

  public:
    unsigned heap_allocations = 0, heap_frees = 0, invalid_frees = 0;
    bool fail_next_heap = false;
    Arena();
    ~Arena();
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;
    void *Allocate(size_t size, Kind kind);
    template <class T> T *Object(Kind kind) {
        return static_cast<T *>(Allocate(sizeof(T), kind));
    }
    size_t Register(const void *data, size_t size, Kind kind, bool writable = false);
    void Unregister(size_t token);
    bool Contains(const void *data, size_t size, Kind kind, bool write = false) const;
    bool Owns(const void *data, Kind kind) const;
    bool Release(void *data);
    bool Canaries() const;
    bool GuardPages() const;
    unsigned LiveHeaps() const;
};
class ScopedReadView {
    Arena &owner_;
    size_t token_;

  public:
    ScopedReadView(Arena &owner, const void *data, size_t size)
        : owner_(owner), token_(owner.Register(data, size, Kind::Bytes)) {}
    ~ScopedReadView() {
        owner_.Unregister(token_);
    }
    ScopedReadView(const ScopedReadView &) = delete;
    ScopedReadView &operator=(const ScopedReadView &) = delete;
};
struct NameString {
    uint32_t leading;
    union Storage {
        char bytes[16];
        char *heap;
    } storage;
    uint32_t length, capacity;
};
struct Officer {
    uint32_t type_token;
    uint16_t encoded_id;
    unsigned char pad[2], surname[9], surname_reading[17], given[9], given_reading[17];
    unsigned char tail[0xb4 - 0x3c];
};
struct EventContext {
    uint32_t vtable_token;
    unsigned char pad[0xa0 - 4];
    uint32_t request;
};
struct Resource {
    uint32_t vtable_token;
    NameString filename;
    unsigned char pad[0x88 - 4 - sizeof(NameString)];
    uint32_t message_base;
};
struct Manager {
    unsigned char pad0[0x48];
    Officer **bp_begin;
    unsigned char pad1[0x58 - 0x4c];
    Officer **spk_begin;
    unsigned char pad2[0x98 - 0x5c];
    Resource *file;
    unsigned char pad3[0x108 - 0x9c];
    uint32_t mode;
    unsigned char pad4[0x12c - 0x10c];
    uint32_t *command_begin;
    uint32_t *command_end;
    unsigned char pad5[0x140 - 0x134];
    EventContext *context;
};
struct BuildIdentity {
    unsigned char image_sha256[32], call_bytes[5];
    uint32_t machine, image_base, call_site, original_entry, return_site;
    bool externally_validated;
};
// These are supplied harness metadata, NOT newly inferred fields in the game.
struct Environment {
    BuildIdentity build;
    Officer *canonical_officer;
    uint32_t resource_type, property;
    bool custom, historical_provenance;
};
struct Snapshot {
    Environment environment;
    Manager *manager;
    Officer *officer;
    Officer **speakers;
    Resource *file;
    EventContext *context;
    uint32_t *begin;
    uint32_t *end;
    uint32_t next_statement;
    unsigned char officer_bytes[sizeof(Officer)];
};
struct MockOptions {
    uint32_t status = 1;
    const char *output = nullptr;
    uint32_t output_length = 0;
    bool mutate_source = false, mutate_context = false, reenter = false;
    NameString *nested_output = nullptr;
};
struct Counters {
    unsigned evaluations = 0, adapter_invocations = 0, substitutions = 0, reentry_refusals = 0,
             assignments = 0;
    void *manager = nullptr;
    NameString *expression = nullptr;
    NameString *output = nullptr;
    uint32_t flag = 0;
    uint32_t nested_status = 0;
};
struct ProbeResult {
    uint32_t eax, esp_before, esp_after, ebx, esi, edi, ebp, flags, canaries;
};
static_assert(sizeof(void *) == 4);
static_assert(sizeof(ProbeResult) == 36 && offsetof(ProbeResult, canaries) == 32);
static_assert(sizeof(NameString) == 0x1c && offsetof(NameString, storage) == 4);
static_assert(offsetof(NameString, length) == 0x14 && offsetof(NameString, capacity) == 0x18);
static_assert(sizeof(Officer) == 0xb4 && offsetof(Officer, surname) == 8 &&
              offsetof(Officer, given) == 0x22);
static_assert(offsetof(Manager, bp_begin) == 0x48 && offsetof(Manager, spk_begin) == 0x58);
static_assert(offsetof(Manager, file) == 0x98 && offsetof(Manager, mode) == 0x108);
static_assert(offsetof(Manager, command_begin) == 0x12c && offsetof(Manager, command_end) == 0x130);
static_assert(offsetof(Manager, context) == 0x140 && offsetof(EventContext, request) == 0xa0);
static_assert(offsetof(Resource, filename) == 4 && offsetof(Resource, message_base) == 0x88);
extern Arena *arena;
extern Environment *environment;
extern MockOptions options;
extern Counters counters;
extern const char japanese_surname[5], japanese_given[5];
void Initialize(NameString *s);
bool ValidString(const NameString *s);
const char *Data(const NameString *s);
void Put(NameString *s, const char *bytes);
void SetOfficer(Officer *o, uint16_t id, const char *surname, const char *given);
uint16_t DecodeId(uint16_t encoded);
bool DecodeField(const unsigned char (&encoded)[9], char (&decoded)[10]);
BuildIdentity ExpectedIdentity();
bool Capture(Manager *m, NameString *expression, NameString *output, uint32_t flag, Snapshot &s);
uint32_t InvokeAdapter(Manager *m, NameString *expr, NameString *out, uint32_t flag);
uint32_t InvokeEvaluator(Manager *m, NameString *expr, NameString *out, uint32_t flag);
NameString *InvokeAssignment(NameString *out, const char *bytes, uint32_t length);
struct Fixture {
    Arena memory;
    Environment *env;
    Manager *manager;
    Officer *officer;
    Officer *bp_decoy;
    Resource *resource;
    EventContext *context;
    Officer **speakers;
    Officer **bps;
    uint32_t *commands;
    NameString *expression;
    NameString *output;
    Fixture();
    ~Fixture();
};
using Entry = void (*)();
} // namespace synthetic
extern "C" {
extern uint32_t g_eval_mod16, g_adapter_mod16, g_assign_mod16;
extern bool g_assignment_stack_ok;
void MockEvaluatorAbi();
void AdapterAbi();
void MockAssignmentAbi();
void Probe(synthetic::Entry target, synthetic::Manager *m, synthetic::NameString *expr,
           synthetic::NameString *out, uint32_t flag, synthetic::ProbeResult *result,
           uint32_t padding);
uint32_t __cdecl MockEvaluatorCore(synthetic::Manager *, synthetic::NameString *,
                                   synthetic::NameString *, uint32_t);
uint32_t __cdecl AdapterCore(synthetic::Manager *, synthetic::NameString *, synthetic::NameString *,
                             uint32_t);
synthetic::NameString *__cdecl AssignmentCore(synthetic::NameString *, const char *, uint32_t);
void ProbeAssignment(synthetic::Entry, synthetic::NameString *, const char *, uint32_t,
                     synthetic::ProbeResult *, uint32_t);
void BadAssignmentRegistersControl();
void BadAssignmentCleanupControl();
void BadAssignmentDirectionControl();
void BadRegistersControl();
void BadCleanupControl();
void BadDirectionControl();
}
