#include "model.h"
#include <cstdio>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace synthetic;

namespace {
unsigned assertions = 0, failures = 0;
struct TestResult {
    std::string name, error;
};
std::vector<TestResult> results;
std::set<uint32_t> alignments;
std::vector<ProbeResult> positive_probes;
void Check(bool value, const char *what) {
    ++assertions;
    if (!value)
        throw std::runtime_error(what);
}
void Test(const char *name, const std::function<void()> &body) {
    try {
        body();
        results.push_back({name, ""});
    } catch (const std::exception &e) {
        ++failures;
        results.push_back({name, e.what()});
    }
}
void Abi(const ProbeResult &r) {
    Check(r.esp_before == r.esp_after, "ESP imbalance / RET12");
    Check(r.ebx == 0x11223344 && r.esi == 0x55667788 && r.edi == 0x99aabbcc && r.ebp == 0x1234abcd,
          "nonvolatile register corruption");
    Check((r.flags & 0x400) == 0, "DF must be clear");
    Check(r.canaries == 1, "stack canary corruption");
    Check(g_assignment_stack_ok, "RET8 assignment stack imbalance");
}
ProbeResult Call(Fixture &f, Entry entry = &AdapterAbi, uint32_t padding = 0) {
    ProbeResult r{};
    Probe(entry, f.manager, f.expression, f.output, 0, &r, padding);
    Abi(r);
    return r;
}
void Safe(const Fixture &f) {
    Check(g_assignment_stack_ok, "assignment-only path ESP imbalance");
    Check(f.memory.Canaries(), "arena redzone changed");
    Check(f.memory.GuardPages(), "guard pages not reserved/inaccessible");
    Check(f.memory.invalid_frees == 0, "invalid free on tested adapter/string path");
}
std::string Hex(const void *data, size_t n) {
    const auto *p = static_cast<const unsigned char *>(data);
    const char *h = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        out += h[p[i] >> 4];
        out += h[p[i] & 15];
    }
    return out;
}
struct State {
    bool registered = false, valid = false;
    uint32_t leading = 0, length = 0, capacity = 0;
    std::string payload, raw_invalid;
    bool operator==(const State &s) const {
        return registered == s.registered && valid == s.valid && leading == s.leading &&
               length == s.length && capacity == s.capacity && payload == s.payload &&
               raw_invalid == s.raw_invalid;
    }
};
State StringState(const NameString *s) {
    State r;
    if (!arena->Contains(s, sizeof(*s), Kind::String))
        return r;
    r.registered = true;
    r.leading = s->leading;
    r.length = s->length;
    r.capacity = s->capacity;
    r.valid = ValidString(s);
    if (r.valid)
        r.payload = Hex(Data(s), size_t(s->length) + 1);
    else
        r.raw_invalid = Hex(s, sizeof(*s));
    return r;
}
struct Input {
    Fixture &f;
    Manager *manager;
    NameString *expr;
    NameString *out;
    uint32_t flag = 0;
    explicit Input(Fixture &fixture)
        : f(fixture), manager(fixture.manager), expr(fixture.expression), out(fixture.output) {}
};
using Edit = std::function<void(Input &)>;
struct Outcome {
    State output, expression, argument_output;
    std::string officer;
    uint32_t status = 0, mode = 0;
};
Outcome RunNegative(Entry entry, const Edit &edit) {
    Fixture f;
    Input in(f);
    edit(in);
    counters = Counters{};
    g_assignment_stack_ok = true;
    ProbeResult r{};
    Probe(entry, in.manager, in.expr, in.out, in.flag, &r, 0);
    Abi(r);
    Safe(f);
    Check(counters.evaluations == 1, "original evaluator not called exactly once");
    Check(counters.manager == in.manager && counters.expression == in.expr &&
              counters.output == in.out && counters.flag == in.flag,
          "ECX/stack argument order incorrect");
    Check(counters.substitutions == 0, "negative guard substituted");
    return {StringState(f.output),
            StringState(f.expression),
            StringState(in.out),
            Hex(f.officer, sizeof(Officer)),
            r.eax,
            f.manager->mode};
}
void Negative(const char *name, const Edit &edit) {
    Test(name, [&] {
        const auto baseline = RunNegative(&MockEvaluatorAbi, edit);
        const auto actual = RunNegative(&AdapterAbi, edit);
        Check(baseline.output == actual.output && baseline.expression == actual.expression &&
                  baseline.argument_output == actual.argument_output,
              "adapter changed baseline output");
        Check(baseline.officer == actual.officer && baseline.mode == actual.mode,
              "adapter changed source/context");
        Check(baseline.status == actual.status, "original full EAX status not preserved");
    });
}
std::string Escape(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out;
}
} // namespace
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    for (uint32_t padding : {0u, 4u, 8u, 12u}) {
        const std::string name = "positive_alignment_padding_" + std::to_string(padding);
        Test(name.c_str(), [&] {
            Fixture f;
            const std::string before = Hex(f.officer, sizeof(Officer));
            const uint32_t opaque = f.output->leading, capacity = f.output->capacity;
            const unsigned allocations = f.memory.heap_allocations;
            const auto r = Call(f, &AdapterAbi, padding);
            Check(r.eax == 1 && counters.evaluations == 1 && counters.adapter_invocations == 1,
                  "positive call/status count");
            Check(counters.substitutions == 1 && counters.assignments == 2,
                  "expected original and replacement assignment");
            Check(counters.manager == f.manager && counters.expression == f.expression &&
                      counters.output == f.output && counters.flag == 0,
                  "original ABI arguments");
            Check(ValidString(f.output) && f.output->length == 8 &&
                      !std::memcmp(Data(f.output), "Nobunaga", 9),
                  "replacement bytes/NUL");
            Check(f.output->leading == opaque && f.output->capacity == capacity,
                  "ownership/leading metadata changed");
            Check(f.memory.heap_allocations == allocations, "inline replacement allocated");
            Check(Hex(f.officer, sizeof(Officer)) == before, "persistent officer bytes changed");
            Check(f.bps[1] != f.speakers[1], "spk/bp decoy fixture ineffective");
            Check((g_eval_mod16 % 4) == 0 && (g_assign_mod16 % 4) == 0, "nested call alignment");
            alignments.insert(g_adapter_mod16);
            positive_probes.push_back(r);
            Safe(f);
        });
    }
    Test("all_valid_x86_four_byte_alignment_residues", [&] {
        Check(alignments == std::set<uint32_t>({0, 4, 8, 12}), "entry alignment coverage");
    });
    Test("probe_detects_bad_nonvolatile_registers", [&] {
        Fixture f;
        ProbeResult r{};
        Probe(&BadRegistersControl, f.manager, f.expression, f.output, 0, &r, 0);
        Check(r.ebx == 0 && r.esi == 0 && r.edi == 0 && r.ebp == 0,
              "probe register negative control");
        Check(r.esp_before == r.esp_after && r.canaries == 1, "probe recovery");
        Safe(f);
    });
    Test("probe_detects_bad_callee_cleanup", [&] {
        Fixture f;
        ProbeResult r{};
        Probe(&BadCleanupControl, f.manager, f.expression, f.output, 0, &r, 0);
        Check(r.esp_before == r.esp_after + 4, "probe cleanup negative control");
        Safe(f);
    });
    Test("probe_detects_bad_direction_flag", [&] {
        Fixture f;
        ProbeResult r{};
        Probe(&BadDirectionControl, f.manager, f.expression, f.output, 0, &r, 0);
        Check((r.flags & 0x400) != 0, "probe DF negative control");
        Safe(f);
    });
    Negative("null_environment", [](Input &) { environment = nullptr; });
    Negative("invalid_environment",
             [](Input &) { environment = reinterpret_cast<Environment *>(1); });
    Negative("invalid_filename_state", [](Input &i) { i.f.resource->filename.length = 16; });
    Negative("forged_heap_pointer", [](Input &i) {
        i.f.output->capacity = 31;
        i.f.output->length = 4;
        i.f.output->storage.heap = reinterpret_cast<char *>(1);
    });
    Negative("unregistered_officer_after_canonical_match", [](Input &i) {
        i.f.env->canonical_officer = reinterpret_cast<Officer *>(1);
        i.f.speakers[1] = i.f.env->canonical_officer;
    });
    Negative("null_officer_after_canonical_match", [](Input &i) {
        i.f.env->canonical_officer = nullptr;
        i.f.speakers[1] = nullptr;
    });
    Negative("wrong_officer_id",
             [](Input &i) { SetOfficer(i.f.officer, 0x3ad, japanese_surname, japanese_given); });
    Negative("wrong_surname",
             [](Input &i) { SetOfficer(i.f.officer, 0x394, "other", japanese_given); });
    Negative("wrong_given_or_renamed",
             [](Input &i) { SetOfficer(i.f.officer, 0x394, japanese_surname, "other"); });
    Negative("wrong_requested_property", [](Input &i) { i.f.env->property = 3; });
    Negative("wrong_expression_property", [](Input &i) { Put(i.f.expression, "spk1.Bname1"); });
    Negative("wrong_speaker_slot", [](Input &i) { Put(i.f.expression, "spk2.Bname2"); });
    Negative("wrong_speaker_pointer", [](Input &i) { i.f.speakers[1] = i.f.bp_decoy; });
    Negative("wrong_canonical_pointer",
             [](Input &i) { i.f.env->canonical_officer = i.f.bp_decoy; });
    Negative("wrong_event_request", [](Input &i) { i.f.context->request = 0x15c9; });
    Negative("wrong_statement_index", [](Input &i) { i.f.commands[0] = 35; });
    Negative("wrong_mode", [](Input &i) { i.f.manager->mode = 0; });
    Negative("wrong_resource_name", [](Input &i) { Put(&i.f.resource->filename, "tutorial"); });
    Negative("wrong_resource_type", [](Input &i) { i.f.env->resource_type = 51; });
    Negative("wrong_message_base", [](Input &i) { i.f.resource->message_base = 11279; });
    Negative("unexpected_instruction_fingerprint",
             [](Input &i) { i.f.env->build.call_bytes[1] ^= 1; });
    Negative("unsupported_executable_hash", [](Input &i) { i.f.env->build.image_sha256[0] ^= 1; });
    Negative("identity_not_externally_validated",
             [](Input &i) { i.f.env->build.externally_validated = false; });
    Negative("wrong_machine", [](Input &i) { i.f.env->build.machine = 0x8664; });
    Negative("wrong_image_base", [](Input &i) { i.f.env->build.image_base += 0x1000; });
    Negative("wrong_call_site", [](Input &i) { i.f.env->build.call_site += 1; });
    Negative("wrong_original_entry", [](Input &i) { i.f.env->build.original_entry += 1; });
    Negative("wrong_return_site", [](Input &i) { i.f.env->build.return_site += 1; });
    Negative("original_failure_status", [](Input &) { options.status = 0; });
    Negative("unrecognized_nonzero_status", [](Input &) { options.status = 0x13572468; });
    Negative("unexpected_output_capacity", [](Input &i) { i.f.output->capacity = 14; });
    Negative("invalid_output_length", [](Input &i) { i.f.output->length = 16; });
    Negative("invalid_output_terminator",
             [](Input &i) { i.f.output->storage.bytes[i.f.output->length] = 'x'; });
    Negative("unexpected_original_output", [](Input &) {
        options.output = "other";
        options.output_length = 5;
        arena->Register(options.output, 6, Kind::Bytes);
    });
    Negative("heap_output_excluded_from_adapter",
             [](Input &i) { Put(i.f.output, "a synthetic long original output"); });
    Negative("source_changed_during_original", [](Input &) { options.mutate_source = true; });
    Negative("context_changed_during_original", [](Input &) { options.mutate_context = true; });
    Negative("custom_without_mapping", [](Input &i) { i.f.env->custom = true; });
    Negative("unknown_historical_provenance",
             [](Input &i) { i.f.env->historical_provenance = false; });
    Negative("unterminated_encoded_name", [](Input &i) { std::memset(i.f.officer->given, 0, 9); });
    Negative("wrong_context_vtable", [](Input &i) { i.f.context->vtable_token = 0; });
    Negative("empty_command_stack", [](Input &i) { i.f.manager->command_end = i.f.commands; });
    Negative("reversed_command_stack", [](Input &i) {
        i.f.manager->command_begin = i.f.commands + 1;
        i.f.manager->command_end = i.f.commands;
    });
    Negative("unaligned_command_extent", [](Input &i) {
        i.f.manager->command_end =
            reinterpret_cast<uint32_t *>(reinterpret_cast<uintptr_t>(i.f.commands) + 3);
    });
    Negative("unsupported_stack_depth",
             [](Input &i) { i.f.manager->command_end = i.f.commands + 2; });
    Negative("null_manager", [](Input &i) { i.manager = nullptr; });
    Negative("invalid_manager", [](Input &i) { i.manager = reinterpret_cast<Manager *>(1); });
    Negative("null_expression", [](Input &i) { i.expr = nullptr; });
    Negative("invalid_expression", [](Input &i) { i.expr = reinterpret_cast<NameString *>(1); });
    Negative("null_output", [](Input &i) { i.out = nullptr; });
    Negative("invalid_output", [](Input &i) { i.out = reinterpret_cast<NameString *>(1); });
    Negative("officer_memory_not_a_string",
             [](Input &i) { i.out = reinterpret_cast<NameString *>(i.f.officer); });
    Negative("null_context", [](Input &i) { i.f.manager->context = nullptr; });
    Negative("invalid_context",
             [](Input &i) { i.f.manager->context = reinterpret_cast<EventContext *>(1); });
    Negative("invalid_file", [](Input &i) { i.f.manager->file = reinterpret_cast<Resource *>(1); });
    Negative("invalid_speaker_vector",
             [](Input &i) { i.f.manager->spk_begin = reinterpret_cast<Officer **>(1); });
    Negative("invalid_speaker_pointer",
             [](Input &i) { i.f.speakers[1] = reinterpret_cast<Officer *>(1); });
    Negative("invalid_command_begin", [](Input &i) {
        i.f.manager->command_begin = reinterpret_cast<uint32_t *>(1);
        i.f.manager->command_end = reinterpret_cast<uint32_t *>(5);
    });
    Negative("fallback_flag_preserved", [](Input &i) { i.flag = 7; });
    Negative("expression_output_alias", [](Input &i) { i.out = i.expr; });
    Test("reentrant_outer_and_inner_refused", [&] {
        Fixture f;
        auto *inner = f.memory.Object<NameString>(Kind::String);
        Initialize(inner);
        Put(inner, "nested");
        const std::string before = Hex(f.officer, sizeof(Officer));
        options.reenter = true;
        options.nested_output = inner;
        counters = Counters{};
        auto r = Call(f);
        Check(r.eax == 1 && counters.nested_status == 1, "reentrant status");
        Check(counters.adapter_invocations == 2 && counters.evaluations == 2 &&
                  counters.reentry_refusals == 1,
              "once per invocation in reentry");
        Check(counters.substitutions == 0, "outer or inner reentry substituted");
        Check(f.output->length == 4 && inner->length == 4 &&
                  !std::memcmp(Data(inner), japanese_given, 5),
              "reentry output not original");
        Check(Hex(f.officer, sizeof(Officer)) == before, "reentry source changed");
        Safe(f);
    });
    Test("repeat_10000_calls", [&] {
        Fixture f;
        const std::string before = Hex(f.officer, sizeof(Officer));
        const unsigned allocated = f.memory.heap_allocations;
        for (unsigned n = 0; n < 10000; ++n) {
            auto r = Call(f);
            Check(r.eax == 1 && f.output->length == 8, "repeat call");
        }
        Check(counters.evaluations == 10000 && counters.substitutions == 10000,
              "repeat once/count");
        Check(f.memory.heap_allocations == allocated && Hex(f.officer, sizeof(Officer)) == before,
              "repeat allocation/source");
        Safe(f);
    });
    for (const char *name : {"Nobunaga", "Mitsuhide", "Oda Nobunaga", "Matsudaira Motoyasu",
                             "Chosokabe Motochika and Oda Nobunaga"}) {
        const std::string title =
            "owned_assignment_" + std::to_string(std::strlen(name)) + "_bytes";
        Test(title.c_str(), [=] {
            Fixture f;
            const uint32_t leading = f.output->leading;
            const size_t n = std::strlen(name);
            Put(f.output, name);
            Check(ValidString(f.output) && f.output->length == n, "long assignment valid");
            Check(!std::memcmp(Data(f.output), name, n + 1) && f.output->leading == leading,
                  "long copy/leading");
            Check((n <= 15 && f.output->capacity == 15 && f.memory.LiveHeaps() == 0) ||
                      (n > 15 && f.output->capacity >= n && f.memory.LiveHeaps() == 1),
                  "inline/owned heap");
            if (f.output->capacity >= 16) {
                Check(f.memory.Release(f.output->storage.heap), "heap release");
                Initialize(f.output);
            }
            Check(f.memory.LiveHeaps() == 0 && f.memory.heap_allocations == f.memory.heap_frees,
                  "heap ownership balanced");
            Safe(f);
        });
    }
    Test("direct_assignment_RET8_full_ABI", [&] {
        Fixture f;
        const char *text = "Nobunaga";
        ScopedReadView source(f.memory, text, 9);
        ProbeResult r{};
        ProbeAssignment(&MockAssignmentAbi, f.output, text, 8, &r, 0);
        Abi(r);
        Check(r.eax == reinterpret_cast<uint32_t>(f.output), "assignment EAX pointer");
        Check(f.output->length == 8 && !std::memcmp(Data(f.output), text, 9),
              "assignment direct arguments");
        Safe(f);
    });
    Test("assignment_probe_detects_bad_registers", [&] {
        Fixture f;
        ProbeResult r{};
        ProbeAssignment(&BadAssignmentRegistersControl, f.output, nullptr, 0, &r, 0);
        Check(r.ebx == 0 && r.esi == 0 && r.edi == 0 && r.ebp == 0, "assignment register control");
        Safe(f);
    });
    Test("assignment_probe_detects_bad_RET4", [&] {
        Fixture f;
        ProbeResult r{};
        ProbeAssignment(&BadAssignmentCleanupControl, f.output, nullptr, 0, &r, 0);
        Check(r.esp_before == r.esp_after + 4, "assignment cleanup control");
        Safe(f);
    });
    Test("assignment_probe_detects_bad_DF", [&] {
        Fixture f;
        ProbeResult r{};
        ProbeAssignment(&BadAssignmentDirectionControl, f.output, nullptr, 0, &r, 0);
        Check((r.flags & 0x400) != 0, "assignment DF control");
        Safe(f);
    });
    Test("owned_heap_successful_growth_and_direct_ABI", [&] {
        Fixture f;
        Put(f.output, "Matsudaira Motoyasu");
        char *previous = f.output->storage.heap;
        const auto opaque = f.output->leading;
        const unsigned allocated = f.memory.heap_allocations, freed = f.memory.heap_frees;
        const char *longer = "A significantly longer English surname and given name combination";
        const auto n = static_cast<uint32_t>(std::strlen(longer));
        ScopedReadView source(f.memory, longer, size_t(n) + 1);
        ProbeResult r{};
        ProbeAssignment(&MockAssignmentAbi, f.output, longer, n, &r, 4);
        Abi(r);
        Check(r.eax == reinterpret_cast<uint32_t>(f.output) && f.output->leading == opaque,
              "heap assignment return/leading");
        Check(ValidString(f.output) && f.output->length == n &&
                  !std::memcmp(Data(f.output), longer, size_t(n) + 1),
              "heap growth payload");
        Check(f.memory.heap_allocations == allocated + 1 && f.memory.heap_frees == freed + 1 &&
                  f.memory.LiveHeaps() == 1 && !f.memory.Owns(previous, Kind::Heap),
              "heap growth ownership");
        Check(!f.memory.Contains(previous, 1, Kind::Heap), "stale pointer still authorized");
        Safe(f);
    });
    Test("scoped_borrow_and_Put_do_not_authorize_dangling_buffers", [&] {
        Fixture f;
        const char *saved = nullptr;
        {
            std::string temporary = "a temporary independently allocated source string";
            saved = temporary.c_str();
            Put(f.output, temporary.c_str());
            Check(!f.memory.Contains(saved, 1, Kind::Bytes), "Put retained borrowed pointer");
            {
                ScopedReadView source(f.memory, saved, temporary.size() + 1);
                Check(f.memory.Contains(saved, temporary.size() + 1, Kind::Bytes),
                      "scoped view not registered");
            }
            Check(!f.memory.Contains(saved, 1, Kind::Bytes), "scoped view not revoked");
        }
        Check(!f.memory.Contains(saved, 1, Kind::Bytes), "destroyed source remained authorized");
        Safe(f);
    });
    Test("inline_overlap_assignment", [&] {
        Fixture f;
        Put(f.output, "abcdef");
        auto *result = InvokeAssignment(f.output, Data(f.output) + 2, 4);
        Check(result == f.output && !std::memcmp(Data(f.output), "cdef", 5), "inline alias copy");
        Safe(f);
    });
    Test("heap_overlap_assignment_and_empty", [&] {
        Fixture f;
        Put(f.output, "Matsudaira Motoyasu");
        char *owned = f.output->storage.heap;
        Check(InvokeAssignment(f.output, Data(f.output) + 3, 8) == f.output, "heap alias result");
        Check(f.output->storage.heap == owned && f.output->length == 8, "heap alias ownership");
        Check(InvokeAssignment(f.output, nullptr, 0) == f.output && f.output->length == 0 &&
                  Data(f.output)[0] == 0,
              "empty assignment");
        Check(f.output->storage.heap == owned && f.memory.Release(owned),
              "empty retains ownership");
        Initialize(f.output);
        Safe(f);
    });
    Test("allocation_failure_preserves_inline_object", [&] {
        Fixture f;
        const State before = StringState(f.output);
        const char *text = "A sufficiently long English officer name";
        f.memory.Register(text, std::strlen(text) + 1, Kind::Bytes);
        f.memory.fail_next_heap = true;
        Check(InvokeAssignment(f.output, text, static_cast<uint32_t>(std::strlen(text))) == nullptr,
              "allocation failure not surfaced");
        Check(StringState(f.output) == before, "failed allocation changed output");
        Safe(f);
    });
    Test("allocation_failure_preserves_heap_object", [&] {
        Fixture f;
        Put(f.output, "Matsudaira Motoyasu");
        const State before = StringState(f.output);
        char *owned = f.output->storage.heap;
        const char *text = "A significantly longer English surname and given name combination";
        f.memory.Register(text, std::strlen(text) + 1, Kind::Bytes);
        f.memory.fail_next_heap = true;
        Check(InvokeAssignment(f.output, text, static_cast<uint32_t>(std::strlen(text))) == nullptr,
              "heap failure not surfaced");
        Check(StringState(f.output) == before && f.output->storage.heap == owned,
              "heap failure changed output");
        Check(f.memory.Release(owned), "heap release");
        Initialize(f.output);
        Safe(f);
    });
    Test("freed_heap_string_refused_without_dereference", [&] {
        Fixture f;
        Put(f.output, "Matsudaira Motoyasu");
        Check(f.memory.Release(f.output->storage.heap), "fixture free");
        const auto before = Hex(f.output, sizeof(NameString));
        counters = Counters{};
        auto result = Call(f);
        Check(result.eax == 1 && counters.evaluations == 1 && counters.substitutions == 0,
              "freed output flow");
        Check(Hex(f.output, sizeof(NameString)) == before, "freed output metadata changed");
        Safe(f);
    });
    Test("allocator_refuses_double_free_and_foreign_pointer", [&] {
        Fixture f;
        void *heap = f.memory.Allocate(32, Kind::Heap);
        Check(heap != nullptr, "diagnostic allocation");
        Check(f.memory.Release(heap), "first free");
        Check(!f.memory.Release(heap), "double-free detector");
        Check(!f.memory.Release(reinterpret_cast<void *>(1)), "foreign-free detector");
        Check(f.memory.invalid_frees == 2 && f.memory.LiveHeaps() == 0, "diagnostic refusals");
        Check(f.memory.Canaries(), "diagnostic canaries");
    });
    Test("nine_byte_field_is_distinct_from_dynamic_string", [&] {
        Fixture f;
        char decoded[10]{};
        SetOfficer(f.officer, 0x394, "Oda", "Nobunaga");
        Check(DecodeField(f.officer->given, decoded), "eight bytes plus source NUL fits");
        SetOfficer(f.officer, 0x394, "Oda", "Mitsuhide");
        Check(!DecodeField(f.officer->given, decoded), "nine payload bytes have no source NUL");
        Put(f.output, "Mitsuhide");
        Check(ValidString(f.output) && f.output->length == 9 && f.output->capacity == 15,
              "dynamic string independently holds nine bytes plus NUL");
        // The real reader's separately zeroed scratch can terminate a nine-byte
        // payload; this strict matching decoder is not a universal game validator.
        Safe(f);
    });
    Test("documented_name_and_id_codec", [&] {
        Fixture f;
        char surname[10]{}, given[10]{};
        Check(DecodeId(f.officer->encoded_id) == 0x394, "ID decode");
        Check(DecodeField(f.officer->surname, surname) && DecodeField(f.officer->given, given),
              "field terminators");
        Check(!std::memcmp(surname, japanese_surname, 5) && !std::memcmp(given, japanese_given, 5),
              "CP932 decode");
        for (uint16_t id : {uint16_t(0x241), uint16_t(0x394), uint16_t(0x3ad), uint16_t(0x36a),
                            uint16_t(0xa20)}) {
            SetOfficer(f.officer, id, japanese_surname, japanese_given);
            Check(DecodeId(f.officer->encoded_id) == id, "ID codec range");
        }
        Safe(f);
    });
    Test("full_name_spacing_separate_from_adapter", [&] {
        const std::string surname = "Oda", given = "Nobunaga";
        Check(surname + given == "OdaNobunaga", "original no separator model");
        Check(surname + " " + given == "Oda Nobunaga", "future display-only spacing");
    });
    Test("eleven_byte_display_truncation_model", [&] {
        std::string text = "Oda Nobunaga";
        if (text.size() > 10)
            text.erase(11, text.size() - 10);
        Check(text.size() == 11 && text == "Oda Nobunag", "byte erasure model");
        std::string pair(10, 'a');
        pair.push_back(static_cast<char>(0x82));
        pair.push_back(static_cast<char>(0xa0));
        if (pair.size() > 10)
            pair.erase(11, pair.size() - 10);
        Check(pair.size() == 11 && static_cast<unsigned char>(pair.back()) == 0x82,
              "modeled split CP932 boundary");
    });
    std::printf(
        "{\"schema\":\"tenka.synthetic.x86.v1\",\"architecture\":\"x86\",\"name_string_bytes\":%zu,"
        "\"tests\":%zu,\"assertions\":%u,\"failures\":%u,\"entry_mod16\":[",
        sizeof(NameString), results.size(), assertions, failures);
    bool first = true;
    for (auto x : alignments) {
        std::printf("%s%u", first ? "" : ",", x);
        first = false;
    }
    std::printf("],\"positive_abi\":[");
    first = true;
    for (const auto &r : positive_probes) {
        std::printf("%s{\"eax\":%u,\"esp_balanced\":%s,\"ebx\":%u,\"esi\":%u,\"edi\":%u,\"ebp\":%u,"
                    "\"df\":%u,\"canaries\":%u}",
                    first ? "" : ",", r.eax, r.esp_before == r.esp_after ? "true" : "false", r.ebx,
                    r.esi, r.edi, r.ebp, (r.flags >> 10) & 1, r.canaries);
        first = false;
    }
    std::printf("],\"cases\":[");
    first = true;
    for (const auto &t : results) {
        const auto n = Escape(t.name), e = Escape(t.error);
        std::printf("%s{\"name\":\"%s\",\"pass\":%s,\"error\":\"%s\"}", first ? "" : ",", n.c_str(),
                    t.error.empty() ? "true" : "false", e.c_str());
        first = false;
    }
    std::printf("]}\n");
    return failures ? 1 : 0;
}
