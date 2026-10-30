#include "model.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace synthetic {
Arena *arena = nullptr;
Environment *environment = nullptr;
MockOptions options;
Counters counters;
const char japanese_surname[5] = "\x90\x44\x93\x63";
const char japanese_given[5] = "\x90\x4d\x92\xb7";
static const char english_given[] = "Nobunaga";
static thread_local unsigned depth = 0, reentry_epoch = 0;

Arena::Arena() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    page_ = info.dwPageSize;
}
Arena::~Arena() {
    for (auto &b : blocks_)
        if (b.live)
            VirtualFree(b.pages, 0, MEM_RELEASE);
}
void *Arena::Allocate(size_t size, Kind kind) {
    if (!size || size > page_ - 128)
        return nullptr;
    if (kind == Kind::Heap && fail_next_heap) {
        fail_next_heap = false;
        return nullptr;
    }
    auto *pages =
        static_cast<unsigned char *>(VirtualAlloc(nullptr, page_ * 3, MEM_RESERVE, PAGE_NOACCESS));
    if (!pages)
        return nullptr;
    if (!VirtualAlloc(pages + page_, page_, MEM_COMMIT, PAGE_READWRITE)) {
        VirtualFree(pages, 0, MEM_RELEASE);
        return nullptr;
    }
    std::memset(pages + page_, 0xa5, page_);
    const auto end = reinterpret_cast<uintptr_t>(pages + page_ * 2 - 32);
    auto *data = reinterpret_cast<unsigned char *>((end - size) & ~uintptr_t(7));
    std::memset(data, 0, size);
    blocks_.push_back({pages, data, size, kind, true});
    if (kind == Kind::Heap)
        ++heap_allocations;
    return data;
}
size_t Arena::Register(const void *p, size_t n, Kind kind, bool writable) {
    views_.push_back({p, n, kind, writable});
    return views_.size() - 1;
}
void Arena::Unregister(size_t token) {
    if (token < views_.size()) {
        views_[token].data = nullptr;
        views_[token].size = 0;
    }
}
static bool Inside(const void *p, size_t n, const void *base, size_t size) {
    if (!p || !base)
        return false;
    const auto a = reinterpret_cast<uintptr_t>(p), b = reinterpret_cast<uintptr_t>(base);
    return a >= b && n <= size && a - b <= size - n;
}
bool Arena::Contains(const void *p, size_t n, Kind kind, bool write) const {
    if (!p)
        return false;
    for (const auto &b : blocks_)
        if (b.live && (b.kind == kind || (kind == Kind::Bytes && b.kind == Kind::Heap)) &&
            Inside(p, n, b.data, b.size))
            return true;
    for (const auto &v : views_) {
        if (v.kind != kind || (write && !v.writable) || !Inside(p, n, v.data, v.size))
            continue;
        bool dead = false;
        for (const auto &b : blocks_)
            if (!b.live && Inside(v.data, v.size, b.data, b.size))
                dead = true;
        if (!dead)
            return true;
    }
    return false;
}
bool Arena::Owns(const void *p, Kind kind) const {
    for (const auto &b : blocks_)
        if (b.live && b.kind == kind && b.data == p)
            return true;
    return false;
}
bool Arena::Release(void *p) {
    if (!p)
        return true;
    for (auto &b : blocks_)
        if (b.live && b.data == p) {
            if (!VirtualFree(b.pages, 0, MEM_RELEASE))
                return false;
            b.live = false;
            if (b.kind == Kind::Heap)
                ++heap_frees;
            return true;
        }
    ++invalid_frees;
    return false;
}
bool Arena::Canaries() const {
    for (const auto &b : blocks_)
        if (b.live) {
            for (auto *p = b.pages + page_; p < b.pages + 2 * page_; ++p)
                if ((p < b.data || p >= b.data + b.size) && *p != 0xa5)
                    return false;
        }
    return true;
}
bool Arena::GuardPages() const {
    for (const auto &b : blocks_)
        if (b.live) {
            MEMORY_BASIC_INFORMATION first{}, last{};
            if (!VirtualQuery(b.pages, &first, sizeof(first)) ||
                !VirtualQuery(b.pages + 2 * page_, &last, sizeof(last)) ||
                first.State != MEM_RESERVE || last.State != MEM_RESERVE)
                return false;
        }
    return true;
}
unsigned Arena::LiveHeaps() const {
    unsigned n = 0;
    for (const auto &b : blocks_)
        if (b.live && b.kind == Kind::Heap)
            ++n;
    return n;
}
void Initialize(NameString *s) {
    std::memset(s, 0, sizeof(*s));
    s->leading = 0x7155aa91;
    s->capacity = 15;
    arena->Register(s->storage.bytes, 16, Kind::Bytes, true);
}
const char *Data(const NameString *s) {
    return s->capacity < 16 ? s->storage.bytes : s->storage.heap;
}
bool ValidString(const NameString *s) {
    if (!arena || !arena->Contains(s, sizeof(*s), Kind::String))
        return false;
    if ((s->capacity != 15 && (s->capacity < 16 || s->capacity > 2047)) || s->length > s->capacity)
        return false;
    const char *data = Data(s);
    if (s->capacity >= 16 && (!arena->Owns(data, Kind::Heap) ||
                              !arena->Contains(data, size_t(s->capacity) + 1, Kind::Heap)))
        return false;
    return data[s->length] == 0;
}
void Put(NameString *s, const char *bytes) {
    const size_t n = std::strlen(bytes);
    if (n > 2047)
        throw std::runtime_error("test literal too long");
    ScopedReadView read_view(*arena, bytes, n + 1);
    if (!InvokeAssignment(s, bytes, static_cast<uint32_t>(n)))
        throw std::runtime_error("fixture assignment failed");
}
static uint8_t Rol8(uint8_t x, unsigned n) {
    return static_cast<uint8_t>((x << n) | (x >> (8 - n)));
}
static uint8_t Ror8(uint8_t x, unsigned n) {
    return static_cast<uint8_t>((x >> n) | (x << (8 - n)));
}
static uint16_t Rol16(uint16_t x, unsigned n) {
    return static_cast<uint16_t>((x << n) | (x >> (16 - n)));
}
static uint16_t Ror16(uint16_t x, unsigned n) {
    return static_cast<uint16_t>((x >> n) | (x << (16 - n)));
}
uint16_t DecodeId(uint16_t x) {
    return static_cast<uint16_t>(Rol16(static_cast<uint16_t>(x ^ 0xa9a9), 7) ^ 0x5656);
}
static void EncodeField(unsigned char (&out)[9], const char *text) {
    const size_t n = std::strlen(text);
    for (size_t i = 0; i < 9; ++i) {
        const auto c = i < n ? static_cast<uint8_t>(text[i]) : uint8_t(0);
        out[i] = static_cast<uint8_t>(Ror8(static_cast<uint8_t>(c ^ 0xb3), 3) ^ 0x4c);
    }
}
bool DecodeField(const unsigned char (&encoded)[9], char (&out)[10]) {
    bool terminated = false;
    for (unsigned i = 0; i < 9; ++i) {
        out[i] = static_cast<char>(Rol8(static_cast<uint8_t>(encoded[i] ^ 0x4c), 3) ^ 0xb3);
        if (!out[i])
            terminated = true;
    }
    out[9] = 0;
    return terminated;
}
void SetOfficer(Officer *o, uint16_t id, const char *surname, const char *given) {
    o->encoded_id = static_cast<uint16_t>(Ror16(static_cast<uint16_t>(id ^ 0x5656), 7) ^ 0xa9a9);
    EncodeField(o->surname, surname);
    EncodeField(o->given, given);
}
BuildIdentity ExpectedIdentity() {
    BuildIdentity id{};
    const char *hex = "b835b6a50129f7bb07e6310490d7a259cbab65e6396ee61c735e1ea4b413c14d";
    const auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (unsigned i = 0; i < 32; ++i)
        id.image_sha256[i] =
            static_cast<unsigned char>((digit(hex[2 * i]) << 4) | digit(hex[2 * i + 1]));
    const unsigned char bytes[] = {0xe8, 0xa7, 0xec, 0xff, 0xff};
    std::memcpy(id.call_bytes, bytes, 5);
    id.machine = 0x14c;
    id.image_base = 0x400000;
    id.call_site = 0x5b2754;
    id.original_entry = 0x5b1400;
    id.return_site = 0x5b2759;
    id.externally_validated = true;
    return id;
}
static bool SameText(const NameString *s, const char *bytes, uint32_t n) {
    return ValidString(s) && s->length == n && !std::memcmp(Data(s), bytes, n);
}
bool Capture(Manager *m, NameString *expr, NameString *out, uint32_t flag, Snapshot &s) {
    if (!arena || !arena->Contains(environment, sizeof(*environment), Kind::Environment) ||
        !arena->Contains(m, sizeof(*m), Kind::Manager) || out == expr ||
        !SameText(expr, "spk1.Bname2", 11) || !ValidString(out) || out->capacity != 15 || flag)
        return false;
    const auto expected = ExpectedIdentity();
    if (std::memcmp(environment->build.image_sha256, expected.image_sha256, 32) ||
        std::memcmp(environment->build.call_bytes, expected.call_bytes, 5) ||
        environment->build.machine != expected.machine ||
        environment->build.image_base != expected.image_base ||
        environment->build.call_site != expected.call_site ||
        environment->build.original_entry != expected.original_entry ||
        environment->build.return_site != expected.return_site ||
        !environment->build.externally_validated || environment->resource_type != 52 ||
        environment->property != 5 || environment->custom || !environment->historical_provenance ||
        m->mode != 2)
        return false;
    if (!arena->Contains(m->context, sizeof(EventContext), Kind::Context) ||
        m->context->vtable_token != 0x79c034 || m->context->request != 0x15c8 ||
        !arena->Contains(m->file, sizeof(Resource), Kind::Resource) ||
        !SameText(&m->file->filename, "rifevent01", 10) || m->file->message_base != 24660 ||
        !arena->Contains(m->spk_begin, sizeof(Officer *) * 10, Kind::Slots))
        return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(m->command_begin),
                    end = reinterpret_cast<uintptr_t>(m->command_end);
    // Deliberately narrow synthetic profile; actual live stack depth is not established here.
    if (end < begin || end - begin != sizeof(uint32_t) ||
        !arena->Contains(m->command_begin, sizeof(uint32_t), Kind::Commands) ||
        *m->command_begin != 36)
        return false;
    Officer *officer = m->spk_begin[1];
    if (officer != environment->canonical_officer ||
        !arena->Contains(officer, sizeof(Officer), Kind::Officer) ||
        DecodeId(officer->encoded_id) != 0x394)
        return false;
    char surname[10]{}, given[10]{};
    if (!DecodeField(officer->surname, surname) || !DecodeField(officer->given, given) ||
        std::memcmp(surname, japanese_surname, 5) || std::memcmp(given, japanese_given, 5))
        return false;
    std::memcpy(&s.environment, environment, sizeof(*environment));
    s.manager = m;
    s.officer = officer;
    s.speakers = m->spk_begin;
    s.file = m->file;
    s.context = m->context;
    s.begin = m->command_begin;
    s.end = m->command_end;
    s.next_statement = *m->command_begin;
    std::memcpy(s.officer_bytes, officer, sizeof(*officer));
    return true;
}
Fixture::Fixture() {
    arena = &memory;
    options = MockOptions{};
    counters = Counters{};
    env = memory.Object<Environment>(Kind::Environment);
    environment = env;
    manager = memory.Object<Manager>(Kind::Manager);
    officer = memory.Object<Officer>(Kind::Officer);
    bp_decoy = memory.Object<Officer>(Kind::Officer);
    resource = memory.Object<Resource>(Kind::Resource);
    context = memory.Object<EventContext>(Kind::Context);
    speakers = static_cast<Officer **>(memory.Allocate(10 * sizeof(Officer *), Kind::Slots));
    bps = static_cast<Officer **>(memory.Allocate(10 * sizeof(Officer *), Kind::Slots));
    commands = static_cast<uint32_t *>(memory.Allocate(4 * sizeof(uint32_t), Kind::Commands));
    expression = memory.Object<NameString>(Kind::String);
    output = memory.Object<NameString>(Kind::String);
    if (!env || !manager || !officer || !bp_decoy || !resource || !context || !speakers || !bps ||
        !commands || !expression || !output)
        throw std::runtime_error("fixture allocation failed");
    memory.Register(&resource->filename, sizeof(NameString), Kind::String, true);
    Initialize(&resource->filename);
    Initialize(expression);
    Initialize(output);
    Put(&resource->filename, "rifevent01");
    Put(expression, "spk1.Bname2");
    Put(output, "initial");
    SetOfficer(officer, 0x394, japanese_surname, japanese_given);
    SetOfficer(bp_decoy, 0x3ad, "other", "other");
    env->build = ExpectedIdentity();
    env->canonical_officer = officer;
    env->resource_type = 52;
    env->property = 5;
    env->historical_provenance = true;
    speakers[1] = officer;
    bps[1] = bp_decoy;
    manager->spk_begin = speakers;
    manager->bp_begin = bps;
    manager->file = resource;
    manager->mode = 2;
    commands[0] = 36;
    manager->command_begin = commands;
    manager->command_end = commands + 1;
    manager->context = context;
    context->vtable_token = 0x79c034;
    context->request = 0x15c8;
    resource->message_base = 24660;
    memory.Register(japanese_given, sizeof(japanese_given), Kind::Bytes);
    memory.Register(english_given, sizeof(english_given), Kind::Bytes);
    options.output = japanese_given;
    options.output_length = 4;
    counters = Counters{};
}
Fixture::~Fixture() {
    environment = nullptr;
    arena = nullptr;
}
} // namespace synthetic

extern "C" synthetic::NameString *__cdecl AssignmentCore(synthetic::NameString *out,
                                                         const char *src, uint32_t n) {
    using namespace synthetic;
    ++counters.assignments;
    if (!ValidString(out) || !arena->Contains(out, sizeof(*out), Kind::String, true) || n > 2047 ||
        (n && !arena->Contains(src, n, Kind::Bytes)))
        return nullptr;
    if (n > out->capacity) {
        const uint32_t capacity = n | 15u;
        auto *data = static_cast<char *>(arena->Allocate(size_t(capacity) + 1, Kind::Heap));
        if (!data)
            return nullptr;
        if (n)
            std::memmove(data, src, n);
        data[n] = 0;
        if (out->capacity >= 16 && !arena->Release(out->storage.heap))
            return nullptr;
        out->storage.heap = data;
        out->capacity = capacity;
    } else {
        char *data = out->capacity < 16 ? out->storage.bytes : out->storage.heap;
        if (n)
            std::memmove(data, src, n);
        data[n] = 0;
    }
    out->length = n;
    return out;
}
extern "C" uint32_t __cdecl MockEvaluatorCore(synthetic::Manager *m, synthetic::NameString *expr,
                                              synthetic::NameString *out, uint32_t flag) {
    using namespace synthetic;
    ++counters.evaluations;
    counters.manager = m;
    counters.expression = expr;
    counters.output = out;
    counters.flag = flag;
    if (!arena || !arena->Contains(m, sizeof(*m), Kind::Manager))
        return options.status;
    if (options.reenter) {
        options.reenter = false;
        // A real nested invocation gets its own original evaluation; neither may substitute.
        counters.nested_status = InvokeAdapter(m, expr, options.nested_output, flag);
    }
    if (ValidString(expr) && ValidString(out))
        InvokeAssignment(out, options.output, options.output_length);
    if (options.mutate_source &&
        arena->Contains(environment, sizeof(*environment), Kind::Environment) &&
        arena->Contains(environment->canonical_officer, sizeof(Officer), Kind::Officer))
        environment->canonical_officer->given[0] ^= 1;
    if (options.mutate_context)
        m->mode = 3;
    return options.status;
}
extern "C" uint32_t __cdecl AdapterCore(synthetic::Manager *m, synthetic::NameString *expr,
                                        synthetic::NameString *out, uint32_t flag) {
    using namespace synthetic;
    ++counters.adapter_invocations;
    if (depth) {
        ++counters.reentry_refusals;
        ++reentry_epoch;
        return InvokeEvaluator(m, expr, out, flag);
    }
    ++depth;
    const unsigned epoch = reentry_epoch;
    Snapshot before{}, after{};
    const bool captured = Capture(m, expr, out, flag, before);
    const uint32_t status = InvokeEvaluator(m, expr, out, flag);
    if (captured && status == 1 && epoch == reentry_epoch && Capture(m, expr, out, flag, after) &&
        !std::memcmp(&before, &after, sizeof(before)) && ValidString(out) && out->capacity == 15 &&
        out->length == 4 && !std::memcmp(Data(out), japanese_given, 5)) {
        if (InvokeAssignment(out, english_given, 8))
            ++counters.substitutions;
    }
    --depth;
    return status;
}
