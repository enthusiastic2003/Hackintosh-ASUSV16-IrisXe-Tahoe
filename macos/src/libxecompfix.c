// libxecompfix: runtime patch of Apple's Metal compiler service (MTLCompiler.framework), loaded into
// MTLCompilerService by our compiler plugin (see tools/install-xecompfix.sh). No Apple file is modified.
//
// Problem: linking a GL vertex+fragment pair through Apple's GL-on-Metal layer fails with
//   "Link failed: The type of fragment input user(slotN) does not match the type of the corresponding vertex shader output"
// for every signed-integer flat varying (int, ivec2..4); unsigned and float varyings link. WebRender uses flat ivec
// varyings in every shader, so each pipeline falls back to software vertex processing, which breaks beyond 8 varyings.
//
// The check (MTLCompiler, function that reports the message) does
//     ecx = 1; if (FS_type == VS_type) return OK;           // cmp [rbp-0x68], eax ; je ok
// and only normalises unsigned->signed on the vertex side when the fragment record's flag 0x40 is set.
// This patch replaces the compare with a call into xe_types_equal(), which also accepts types that differ only in
// signedness (UInt*/UShort*/UChar* against Int*/Short*/Char*). The bits are identical, so the link is sound.
//
// Build: clang -dynamiclib -arch x86_64 -mmacosx-version-min=13.0 -O2 libxecompfix.c -o libxecompfix.dylib
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

static void note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void note(const char *fmt, ...) {
    char b[256]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    syslog(LOG_NOTICE, "%s", b); if (getenv("XE_FIX_STDERR")) fprintf(stderr, "%s\n", b);
}
uint64_t xe_g_target, xe_g_fall;         // continuation addresses inside MTLCompiler (equal / not equal)
static volatile int g_patched, g_logged;

static unsigned norm(unsigned t) {       // Metal data types: UInt33-36 UShort41-44 UChar49-52 map to Int29-32 Short37-40 Char45-48
    if ((t >= 33 && t <= 36) || (t >= 41 && t <= 44) || (t >= 49 && t <= 52)) return t - 4;
    return t;
}
unsigned xe_types_equal(unsigned fs, unsigned vs, unsigned flags) {
    if (fs == vs) return 1;
    if (norm(fs) != norm(vs)) return 0;
    if (g_logged < 12) { g_logged++; note("xecompfix: accepted signedness-only varying mismatch fs=%u vs=%u flags=0x%x", fs, vs, flags); }
    return 1;
}

// On entry (jumped to from the patched site): eax = vertex output type, [rbp-0x68] = fragment input type,
// [rbp-0x64] = fragment record flags, ecx = 1 (must survive). Continues at xe_g_target if equal, xe_g_fall otherwise.
__asm__(
    ".text\n.globl _xe_stub\n_xe_stub:\n"
    "pushq %rax\n pushq %rcx\n pushq %rdx\n pushq %rsi\n pushq %rdi\n pushq %r8\n pushq %r9\n pushq %r10\n pushq %r11\n"
    "movl %eax, %esi\n movl -0x68(%rbp), %edi\n movl -0x64(%rbp), %edx\n"
    "movq %rsp, %r10\n andq $-16, %rsp\n pushq %r10\n pushq %r10\n"
    "call _xe_types_equal\n"
    "popq %r10\n popq %r10\n movq %r10, %rsp\n"
    "testl %eax, %eax\n"
    "popq %r11\n popq %r10\n popq %r9\n popq %r8\n popq %rdi\n popq %rsi\n popq %rdx\n popq %rcx\n popq %rax\n"
    "jnz 1f\n"
    "jmpq *_xe_g_fall(%rip)\n"
    "1:\n jmpq *_xe_g_target(%rip)\n");
extern void xe_stub(void);

static int write_code(uint64_t addr, const void *bytes, size_t n) {
    uint64_t page = addr & ~0xFFFULL; size_t len = ((addr + n + 0xFFF) & ~0xFFFULL) - page;
    if (mach_vm_protect(mach_task_self(), page, len, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)) return -1;
    memcpy((void *)addr, bytes, n);
    mach_vm_protect(mach_task_self(), page, len, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    return 0;
}

static void patch_image(const struct mach_header *mh, const char *name) {
    size_t nl = strlen(name);
    if (g_patched || !strstr(name, "MTLCompiler.framework/Versions/") || nl < 12 || strcmp(name + nl - 12, "/MTLCompiler")) return;
    unsigned long sz = 0; uint8_t *text = getsegmentdata((const struct mach_header_64 *)mh, "__TEXT", &sz);
    if (!text) return;
    static const uint8_t pat[] = { 0xb9, 0x01, 0x00, 0x00, 0x00, 0x39, 0x45, 0x98, 0x74 };   // mov ecx,1; cmp [rbp-0x68],eax; je rel8
    uint8_t *site = NULL; int hits = 0;
    for (unsigned long i = 0; i + sizeof pat + 1 < sz; i++)
        if (!memcmp(text + i, pat, sizeof pat)) { site = text + i + 5; hits++; }
    if (hits != 1) { note("xecompfix: %s: pattern hits=%d, not patching", name, hits); return; }
    uint64_t a = (uint64_t)site; int8_t rel = (int8_t)site[4];
    xe_g_fall = a + 5; xe_g_target = a + 5 + rel;
    // thunk (jmp [rip+0]; .quad xe_stub) in the zero padding at the end of this image's __TEXT, within rel32 reach of the site.
    // User code cannot allocate inside the shared cache region, so the image's own padding is the only nearby memory.
    uint8_t *end = text + sz, *p = end; while (p > text && p[-1] == 0) p--;                       // last non-zero byte of the image text
    uint8_t *cave = (uint8_t *)(((uintptr_t)p + 15) & ~(uintptr_t)15);
    uint8_t *page_end = (uint8_t *)(((uintptr_t)end + 0xFFF) & ~(uintptr_t)0xFFF);               // padding runs to the page boundary
    if (page_end - cave < 32) { note("xecompfix: no padding cave after __TEXT (%ld bytes)", (long)(page_end - cave)); return; }
    for (uint8_t *q = cave; q < cave + 32; q++) if (*q) { note("xecompfix: padding cave is not zero"); return; }
    uint8_t thunk[14] = { 0xff, 0x25, 0, 0, 0, 0 }; uint64_t stub = (uint64_t)&xe_stub; memcpy(thunk + 6, &stub, 8);
    if (write_code((uint64_t)cave, thunk, sizeof thunk)) { note("xecompfix: could not write the thunk at %p", (void *)cave); return; }
    mach_vm_address_t page = (mach_vm_address_t)cave;
    int64_t d = (int64_t)page - (int64_t)(a + 5);
    uint8_t jmp[5] = { 0xe9 }; int32_t d32 = (int32_t)d; memcpy(jmp + 1, &d32, 4);
    if (write_code(a, jmp, 5)) { note("xecompfix: could not write the patch at %p", (void *)a); return; }
    g_patched = 1;
    note("xecompfix: patched %s at %p (target %p, fall %p, thunk %p)", name, (void *)a, (void *)xe_g_target, (void *)xe_g_fall, (void *)page);
}

static void on_image(const struct mach_header *mh, intptr_t slide) {
    (void)slide;
    const char *name = NULL; for (uint32_t i = 0; i < _dyld_image_count(); i++) if (_dyld_get_image_header(i) == mh) { name = _dyld_get_image_name(i); break; }
    if (name) patch_image(mh, name);
}
__attribute__((constructor)) static void init(void) {
    note("xecompfix: loaded");
    _dyld_register_func_for_add_image(on_image);       // also called for images that are already loaded
}
