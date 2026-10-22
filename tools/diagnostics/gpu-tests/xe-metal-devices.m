// Minimal Metal bring-up probe, safest level first (each level = a separate run, so a failure points at one step):
//   xe-metal-devices list      enumerate devices only (loads the Metal driver plugin, no GPU work)
//   xe-metal-devices queue     + command queue, a shared buffer and a compute pipeline state (no submission)
//   xe-metal-devices submit    + commit one EMPTY command buffer and wait (first real GPU work)
//   xe-metal-devices compute   + a 1-thread compute kernel that writes 42 to a buffer and checks it
// Build: clang -fobjc-arc -framework Metal -framework Foundation -o build/xe-metal-devices tools/diagnostics/gpu-tests/xe-metal-devices.m
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    @autoreleasepool {
        const char *level = argc > 1 ? argv[1] : "list";
        setvbuf(stdout, NULL, _IOLBF, 0);
        NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();
        printf("[xe-metal] MTLCopyAllDevices: %lu device(s)\n", (unsigned long)devs.count);
        for (id<MTLDevice> d in devs) {
            printf("[xe-metal]   name='%s' registryID=0x%llx lowPower=%d unifiedMemory=%d maxThreadsPerThreadgroup=%lux%lu\n",
                   d.name.UTF8String, d.registryID, d.isLowPower, d.hasUnifiedMemory,
                   (unsigned long)d.maxThreadsPerThreadgroup.width, (unsigned long)d.maxThreadsPerThreadgroup.height);
        }
        if (!strcmp(level, "list")) return devs.count ? 0 : 2;
        id<MTLDevice> dev = devs.firstObject;
        if (!dev) { printf("[xe-metal] no device\n"); return 2; }
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLBuffer> buf = [dev newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        printf("[xe-metal] queue=%s buffer=%s\n", q ? "ok" : "NIL", buf ? "ok" : "NIL");
        NSError *err = nil;
        NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\nkernel void k(device uint *o [[buffer(0)]], uint i [[thread_position_in_grid]]) { o[i] = 42; }\n";
        id<MTLLibrary> lib = nil; id<MTLComputePipelineState> pso = nil;
        if (!strcmp(level, "queue") || !strcmp(level, "compute")) {
            lib = [dev newLibraryWithSource:src options:nil error:&err];
            printf("[xe-metal] library from source: %s %s\n", lib ? "ok" : "FAILED", err ? err.localizedDescription.UTF8String : "");
            if (lib) { pso = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&err];
                       printf("[xe-metal] pipeline state: %s %s\n", pso ? "ok" : "FAILED", err ? err.localizedDescription.UTF8String : ""); }
        }
        if (!strcmp(level, "queue")) return (q && buf) ? 0 : 3;
        id<MTLCommandBuffer> cb = [q commandBuffer];
        if (!strcmp(level, "compute") && pso) {
            memset(buf.contents, 0, 4096);
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:pso]; [enc setBuffer:buf offset:0 atIndex:0];
            [enc dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
            [enc endEncoding];
        }
        printf("[xe-metal] committing...\n");
        [cb commit]; [cb waitUntilCompleted];
        printf("[xe-metal] command buffer status=%ld error=%s\n", (long)cb.status, cb.error ? cb.error.localizedDescription.UTF8String : "none");
        if (!strcmp(level, "compute")) { printf("[xe-metal] result=%u (expect 42)\n", ((uint32_t *)buf.contents)[0]); return ((uint32_t *)buf.contents)[0] == 42 ? 0 : 4; }
        return cb.status == MTLCommandBufferStatusCompleted ? 0 : 5;
    }
}
