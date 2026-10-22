// xe-metal-verify: GPU correctness probe for the patched Metal stack. Each test runs the same work on the GPU and on the CPU and compares.
// Built to find out why Geekbench's ML-style workloads (Face Tracking, Super Resolution) score 0: they use half precision, matrix multiplies,
// SIMD-group operations and so on. Each test is a separate run (a failure or hang then points at one operation):
//   xe-metal-verify list
//   xe-metal-verify <fma32|half|reduce|simd|atomic|texture|mm32|mm16|softmax>
//   build: clang -fobjc-arc -framework Metal -framework MetalPerformanceShaders -framework Foundation -o xe-metal-verify xe-metal-verify.m
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <QuartzCore/QuartzCore.h>
#include <math.h>
#include <string.h>

static id<MTLDevice> dev;
static id<MTLCommandQueue> q;

static uint16_t f2h(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000; int32_t e = (int32_t)((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) { if (e < -10) return (uint16_t)sign; m |= 0x800000; int sh = 14 - e; uint32_t r = m >> sh; if ((m >> (sh - 1)) & 1) r++; return (uint16_t)(sign | r); }
    if (e >= 31) return (uint16_t)(sign | 0x7c00);
    uint16_t h = (uint16_t)(sign | (e << 10) | (m >> 13)); if (m & 0x1000) h++; return h;
}
static float h2f(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff, x;
    if (e == 0) { if (!m) x = sign; else { float f = ldexpf((float)m, -24); uint32_t t; memcpy(&t, &f, 4); x = sign | t; } }
    else if (e == 31) x = sign | 0x7f800000 | (m << 13);
    else x = sign | ((e + 112) << 23) | (m << 13);
    float f; memcpy(&f, &x, 4); return f;
}
static float hround(float f) { return h2f(f2h(f)); }

static NSString *const kSrc = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void k_fma32(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device float *o [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  float x = a[i]; for (int k = 0; k < 64; k++) x = fma(x, b[i], 0.5f); o[i] = x; }\n"
"kernel void k_half(device const half *a [[buffer(0)]], device const half *b [[buffer(1)]], device half *o [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  half x = a[i]; for (int k = 0; k < 8; k++) x = fma(x, b[i], (half)0.25h); o[i] = x; }\n"
"kernel void k_reduce(device const float *a [[buffer(0)]], device float *o [[buffer(1)]], uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]], uint grp [[threadgroup_position_in_grid]]) {\n"
"  threadgroup float t[256]; t[lid] = a[gid]; threadgroup_barrier(mem_flags::mem_threadgroup);\n"
"  for (uint s = 128; s > 0; s >>= 1) { if (lid < s) t[lid] += t[lid + s]; threadgroup_barrier(mem_flags::mem_threadgroup); }\n"
"  if (lid == 0) o[grp] = t[0]; }\n"
"kernel void k_simd(device const float *a [[buffer(0)]], device float *o [[buffer(1)]], uint i [[thread_position_in_grid]]) {\n"
"  o[i] = simd_sum(a[i]); }\n"
"kernel void k_atomic(device const uint *a [[buffer(0)]], device atomic_uint *h [[buffer(1)]], uint i [[thread_position_in_grid]]) {\n"
"  atomic_fetch_add_explicit(&h[a[i] & 63u], 1u, memory_order_relaxed); }\n"
"kernel void k_texw(texture2d<float, access::write> t [[texture(0)]], uint2 p [[thread_position_in_grid]]) {\n"
"  t.write(float4(float(p.x), float(p.y), float(p.x + p.y), 1.0), p); }\n"
"kernel void k_texr(texture2d<float, access::sample> t [[texture(0)]], device float4 *o [[buffer(0)]], uint2 p [[thread_position_in_grid]], uint2 sz [[threads_per_grid]]) {\n"
"  constexpr sampler s(coord::pixel, filter::nearest, address::clamp_to_edge);\n"
"  o[p.y * sz.x + p.x] = t.sample(s, float2(p) + 0.5); }\n";

static int gFails = 0;
static void verdict(const char *name, double maxErr, double tol, double ms) {
    BOOL ok = isfinite(maxErr) && maxErr <= tol;
    printf("[xe-verify] %-8s %s  max error %.3g (tolerance %.3g), %.1f ms\n", name, ok ? "PASS" : "FAIL", maxErr, tol, ms);
    if (!ok) gFails++;
}
static double now_ms(void) { return CACurrentMediaTime() * 1000.0; }
static BOOL finish(id<MTLCommandBuffer> cb, const char *name) {
    [cb commit]; [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) { printf("[xe-verify] %-8s FAIL  command buffer status=%ld error=%s\n", name, (long)cb.status, cb.error ? cb.error.localizedDescription.UTF8String : "none"); gFails++; return NO; }
    return YES;
}
static id<MTLComputePipelineState> pso(id<MTLLibrary> lib, const char *fn) {
    NSError *e = nil; id<MTLFunction> f = [lib newFunctionWithName:[NSString stringWithUTF8String:fn]];
    if (!f) { printf("[xe-verify] cannot find function %s\n", fn); return nil; }
    id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:f error:&e];
    if (!p) printf("[xe-verify] pipeline %s failed: %s\n", fn, e.localizedDescription.UTF8String);
    return p;
}

static void test_fma32(id<MTLLibrary> lib) {
    const int n = 1 << 16; float *a = malloc(n * 4), *b = malloc(n * 4), *r = malloc(n * 4);
    for (int i = 0; i < n; i++) { a[i] = (i % 251) / 251.0f; b[i] = 0.5f + (i % 97) / 200.0f; }
    for (int i = 0; i < n; i++) { float x = a[i]; for (int k = 0; k < 64; k++) x = fmaf(x, b[i], 0.5f); r[i] = x; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:n*4 options:0], B = [dev newBufferWithBytes:b length:n*4 options:0], O = [dev newBufferWithLength:n*4 options:0];
    id<MTLComputePipelineState> p = pso(lib, "k_fma32"); if (!p) { gFails++; return; }
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p]; [e setBuffer:A offset:0 atIndex:0]; [e setBuffer:B offset:0 atIndex:1]; [e setBuffer:O offset:0 atIndex:2];
    [e dispatchThreads:MTLSizeMake(n,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)]; [e endEncoding];
    if (!finish(cb, "fma32")) return; double ms = now_ms() - t0;
    float *g = O.contents; double me = 0; for (int i = 0; i < n; i++) me = fmax(me, fabs(g[i] - r[i]));
    verdict("fma32", me, 1e-5, ms);
}
static void test_half(id<MTLLibrary> lib) {
    const int n = 1 << 16; uint16_t *a = malloc(n * 2), *b = malloc(n * 2), *o; float *r = malloc(n * 4);
    for (int i = 0; i < n; i++) { a[i] = f2h((i % 251) / 251.0f); b[i] = f2h(0.5f + (i % 97) / 200.0f); }
    for (int i = 0; i < n; i++) { float x = h2f(a[i]), y = h2f(b[i]); for (int k = 0; k < 8; k++) x = hround(fmaf(x, y, 0.25f)); r[i] = x; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:n*2 options:0], B = [dev newBufferWithBytes:b length:n*2 options:0], O = [dev newBufferWithLength:n*2 options:0];
    id<MTLComputePipelineState> p = pso(lib, "k_half"); if (!p) { gFails++; return; }
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p]; [e setBuffer:A offset:0 atIndex:0]; [e setBuffer:B offset:0 atIndex:1]; [e setBuffer:O offset:0 atIndex:2];
    [e dispatchThreads:MTLSizeMake(n,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)]; [e endEncoding];
    if (!finish(cb, "half")) return; double ms = now_ms() - t0;
    o = O.contents; double me = 0; for (int i = 0; i < n; i++) me = fmax(me, fabs(h2f(o[i]) - r[i]));
    verdict("half", me, 2e-3, ms);
}
static void test_reduce(id<MTLLibrary> lib) {
    const int n = 1 << 16, g = n / 256; float *a = malloc(n * 4); double *r = calloc(g, 8);
    for (int i = 0; i < n; i++) { a[i] = (float)(i % 10); r[i / 256] += a[i]; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:n*4 options:0], O = [dev newBufferWithLength:g*4 options:0];
    id<MTLComputePipelineState> p = pso(lib, "k_reduce"); if (!p) { gFails++; return; }
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p]; [e setBuffer:A offset:0 atIndex:0]; [e setBuffer:O offset:0 atIndex:1];
    [e dispatchThreads:MTLSizeMake(n,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)]; [e endEncoding];
    if (!finish(cb, "reduce")) return; double ms = now_ms() - t0;
    float *o = O.contents; double me = 0; for (int i = 0; i < g; i++) me = fmax(me, fabs(o[i] - r[i]));
    verdict("reduce", me, 0.0, ms);
}
static void test_simd(id<MTLLibrary> lib) {
    id<MTLComputePipelineState> p = pso(lib, "k_simd"); if (!p) { printf("[xe-verify] simd     FAIL  SIMD-group functions did not compile/link\n"); gFails++; return; }
    const int n = 4096; int w = (int)p.threadExecutionWidth; float *a = malloc(n * 4), *r = malloc(n * 4);
    for (int i = 0; i < n; i++) a[i] = (float)(i % 7);
    for (int g = 0; g < n; g += w) { float s = 0; for (int k = 0; k < w; k++) s += a[g + k]; for (int k = 0; k < w; k++) r[g + k] = s; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:n*4 options:0], O = [dev newBufferWithLength:n*4 options:0];
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p]; [e setBuffer:A offset:0 atIndex:0]; [e setBuffer:O offset:0 atIndex:1];
    [e dispatchThreads:MTLSizeMake(n,1,1) threadsPerThreadgroup:MTLSizeMake(w * 4,1,1)]; [e endEncoding];
    if (!finish(cb, "simd")) return; double ms = now_ms() - t0;
    float *o = O.contents; double me = 0; for (int i = 0; i < n; i++) me = fmax(me, fabs(o[i] - r[i]));
    printf("[xe-verify] simd width = %d\n", w);
    verdict("simd", me, 0.0, ms);
}
static void test_atomic(id<MTLLibrary> lib) {
    const int n = 1 << 16; uint32_t *a = malloc(n * 4); uint32_t ref[64] = {0};
    for (int i = 0; i < n; i++) { a[i] = (uint32_t)(i * 2654435761u) >> 7; ref[a[i] & 63]++; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:n*4 options:0], H = [dev newBufferWithLength:64*4 options:0]; memset(H.contents, 0, 256);
    id<MTLComputePipelineState> p = pso(lib, "k_atomic"); if (!p) { gFails++; return; }
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p]; [e setBuffer:A offset:0 atIndex:0]; [e setBuffer:H offset:0 atIndex:1];
    [e dispatchThreads:MTLSizeMake(n,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)]; [e endEncoding];
    if (!finish(cb, "atomic")) return; double ms = now_ms() - t0;
    uint32_t *h = H.contents; double me = 0; for (int i = 0; i < 64; i++) me = fmax(me, fabs((double)h[i] - ref[i]));
    verdict("atomic", me, 0.0, ms);
}
static void test_texture(id<MTLLibrary> lib) {
    const int W = 64, Hh = 48; MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:W height:Hh mipmapped:NO];
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d]; id<MTLBuffer> O = [dev newBufferWithLength:W*Hh*16 options:0];
    id<MTLComputePipelineState> pw = pso(lib, "k_texw"), pr = pso(lib, "k_texr"); if (!pw || !pr) { gFails++; return; }
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:pw]; [e setTexture:t atIndex:0]; [e dispatchThreads:MTLSizeMake(W,Hh,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [e setComputePipelineState:pr]; [e setTexture:t atIndex:0]; [e setBuffer:O offset:0 atIndex:0]; [e dispatchThreads:MTLSizeMake(W,Hh,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [e endEncoding]; if (!finish(cb, "texture")) return; double ms = now_ms() - t0;
    float *o = O.contents; double me = 0;
    for (int y = 0; y < Hh; y++) for (int x = 0; x < W; x++) { float *px = o + (y * W + x) * 4; me = fmax(me, fabs(px[0] - x)); me = fmax(me, fabs(px[1] - y)); me = fmax(me, fabs(px[2] - (x + y))); me = fmax(me, fabs(px[3] - 1)); }
    verdict("texture", me, 0.0, ms);
}
static void test_mm(BOOL half) {
    const int M = 128, K = 256, N = 64; size_t es = half ? 2 : 4; MPSDataType dt = half ? MPSDataTypeFloat16 : MPSDataTypeFloat32;
    float *a = malloc(M*K*4), *b = malloc(K*N*4); double *r = calloc(M*N, 8);
    for (int i = 0; i < M*K; i++) a[i] = ((i * 37) % 101 - 50) / 100.0f;
    for (int i = 0; i < K*N; i++) b[i] = ((i * 53) % 103 - 51) / 100.0f;
    void *ab = malloc(M*K*es), *bb = malloc(K*N*es);
    for (int i = 0; i < M*K; i++) { if (half) { ((uint16_t*)ab)[i] = f2h(a[i]); a[i] = h2f(((uint16_t*)ab)[i]); } else ((float*)ab)[i] = a[i]; }
    for (int i = 0; i < K*N; i++) { if (half) { ((uint16_t*)bb)[i] = f2h(b[i]); b[i] = h2f(((uint16_t*)bb)[i]); } else ((float*)bb)[i] = b[i]; }
    for (int m = 0; m < M; m++) for (int k = 0; k < K; k++) for (int n = 0; n < N; n++) r[m*N + n] += (double)a[m*K + k] * b[k*N + n];
    id<MTLBuffer> A = [dev newBufferWithBytes:ab length:M*K*es options:0], B = [dev newBufferWithBytes:bb length:K*N*es options:0], C = [dev newBufferWithLength:M*N*es options:0];
    MPSMatrix *mA = [[MPSMatrix alloc] initWithBuffer:A descriptor:[MPSMatrixDescriptor matrixDescriptorWithRows:M columns:K rowBytes:K*es dataType:dt]];
    MPSMatrix *mB = [[MPSMatrix alloc] initWithBuffer:B descriptor:[MPSMatrixDescriptor matrixDescriptorWithRows:K columns:N rowBytes:N*es dataType:dt]];
    MPSMatrix *mC = [[MPSMatrix alloc] initWithBuffer:C descriptor:[MPSMatrixDescriptor matrixDescriptorWithRows:M columns:N rowBytes:N*es dataType:dt]];
    MPSMatrixMultiplication *mm = [[MPSMatrixMultiplication alloc] initWithDevice:dev transposeLeft:NO transposeRight:NO resultRows:M resultColumns:N interiorColumns:K alpha:1.0 beta:0.0];
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; [mm encodeToCommandBuffer:cb leftMatrix:mA rightMatrix:mB resultMatrix:mC];
    if (!finish(cb, half ? "mm16" : "mm32")) return; double ms = now_ms() - t0;
    double me = 0, scale = 0; for (int i = 0; i < M*N; i++) scale = fmax(scale, fabs(r[i]));
    for (int i = 0; i < M*N; i++) { double g = half ? h2f(((uint16_t*)C.contents)[i]) : ((float*)C.contents)[i]; me = fmax(me, fabs(g - r[i])); }
    printf("[xe-verify] result magnitude up to %.2f\n", scale);
    verdict(half ? "mm16" : "mm32", me / (scale > 0 ? scale : 1), half ? 5e-3 : 1e-5, ms);
}
static void test_softmax(void) {
    const int R = 32, C = 100; float *a = malloc(R*C*4); double *r = malloc(R*C*8);
    for (int i = 0; i < R*C; i++) a[i] = ((i * 29) % 97 - 48) / 12.0f;
    for (int y = 0; y < R; y++) { double mx = -1e30, s = 0; for (int x = 0; x < C; x++) mx = fmax(mx, a[y*C + x]); for (int x = 0; x < C; x++) s += exp(a[y*C + x] - mx); for (int x = 0; x < C; x++) r[y*C + x] = exp(a[y*C + x] - mx) / s; }
    id<MTLBuffer> A = [dev newBufferWithBytes:a length:R*C*4 options:0], O = [dev newBufferWithLength:R*C*4 options:0];
    MPSMatrixDescriptor *d = [MPSMatrixDescriptor matrixDescriptorWithRows:R columns:C rowBytes:C*4 dataType:MPSDataTypeFloat32];
    MPSMatrix *mA = [[MPSMatrix alloc] initWithBuffer:A descriptor:d], *mO = [[MPSMatrix alloc] initWithBuffer:O descriptor:d];
    MPSMatrixSoftMax *sm = [[MPSMatrixSoftMax alloc] initWithDevice:dev];
    double t0 = now_ms(); id<MTLCommandBuffer> cb = [q commandBuffer]; [sm encodeToCommandBuffer:cb inputMatrix:mA resultMatrix:mO];
    if (!finish(cb, "softmax")) return; double ms = now_ms() - t0;
    float *o = O.contents; double me = 0; for (int i = 0; i < R*C; i++) me = fmax(me, fabs(o[i] - r[i]));
    verdict("softmax", me, 1e-5, ms);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        const char *t = argc > 1 ? argv[1] : "list";
        dev = MTLCreateSystemDefaultDevice(); if (!dev) { printf("[xe-verify] no Metal device\n"); return 2; }
        if (!strcmp(t, "list")) { printf("device: %s\ntests: fma32 half reduce simd atomic texture mm32 mm16 softmax\n", dev.name.UTF8String); return 0; }
        q = [dev newCommandQueue];
        NSError *err = nil; id<MTLLibrary> lib = [dev newLibraryWithSource:kSrc options:nil error:&err];
        if (!lib) { printf("[xe-verify] shader library FAILED to compile: %s\n", err.localizedDescription.UTF8String); return 3; }
        if (!strcmp(t, "fma32")) test_fma32(lib); else if (!strcmp(t, "half")) test_half(lib); else if (!strcmp(t, "reduce")) test_reduce(lib);
        else if (!strcmp(t, "simd")) test_simd(lib); else if (!strcmp(t, "atomic")) test_atomic(lib); else if (!strcmp(t, "texture")) test_texture(lib);
        else if (!strcmp(t, "mm32")) test_mm(NO); else if (!strcmp(t, "mm16")) test_mm(YES); else if (!strcmp(t, "softmax")) test_softmax();
        else { printf("unknown test %s\n", t); return 2; }
    }
    return gFails ? 1 : 0;
}
