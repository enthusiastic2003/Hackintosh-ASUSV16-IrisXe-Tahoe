// Index buffer rewritten between draws, at the Metal level (no OpenGL): which update path gives stale indices?
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#define N 256
static NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
"struct V { float2 p; float3 c; };\n"
"struct O { float4 pos [[position]]; float3 c; };\n"
"vertex O vs(uint vid [[vertex_id]], const device packed_float2 *pos [[buffer(0)]], const device packed_float3 *col [[buffer(1)]]) { O o; o.pos = float4(pos[vid], 0, 1); o.c = col[vid]; return o; }\n"
"fragment float4 fs(O in [[stage_in]]) { return float4(in.c, 1); }\n"
"kernel void cp(const device uint *s [[buffer(0)]], device uint *d [[buffer(1)]], uint t [[thread_position_in_grid]]) { d[t] = s[t]; }\n";
int main(void) { @autoreleasepool {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); id<MTLCommandQueue> q = [dev newCommandQueue];
    NSError *err = nil; id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err]; if (!lib) { printf("compile: %s\n", [[err description] UTF8String]); return 2; }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm; id<MTLRenderPipelineState> rps = [dev newRenderPipelineStateWithDescriptor:pd error:&err]; if (!rps) { printf("pso: %s\n", [[err description] UTF8String]); return 2; }
    id<MTLComputePipelineState> cps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"cp"] error:&err];
    printf("device: %s\n", [[dev name] UTF8String]);
    float pos[9][2], col[9][3];
    for (int k = 0; k < 3; k++) for (int i = 0; i < 3; i++) { pos[k*3+i][0] = i == 1 ? 3 : -1; pos[k*3+i][1] = i == 2 ? 3 : -1; col[k*3+i][0] = k == 0; col[k*3+i][1] = k == 1; col[k*3+i][2] = k == 2; }
    id<MTLBuffer> vp = [dev newBufferWithBytes:pos length:sizeof pos options:MTLResourceStorageModeShared], vc = [dev newBufferWithBytes:col length:sizeof col options:MTLResourceStorageModeShared];
    id<MTLBuffer> ib = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
    id<MTLBuffer> stage = [dev newBufferWithLength:N * 16 options:MTLResourceStorageModeShared];
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:N mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget; td.storageMode = MTLStorageModeShared; id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
    const char *names[] = { "M0 CPU write into the index buffer, wait for each draw", "M1 blit copy staging->index, same command buffer as the draw, no waits",
        "M2 blit copy and draw in separate command buffers, no waits", "M3 compute-kernel copy, same command buffer as the draw, no waits",
        "M4 as M1 plus a buffer memory barrier before the draw", "M5 as M1 but the draw reads a DIFFERENT index buffer each row (no reuse)",
        "M6 as M3 (compute copy) plus a buffer barrier in the render encoder before the draw", "M7 as M3 but the compute copy is its own command buffer, no waits",
        "M8 as M3 plus a buffer barrier inside the compute encoder after the dispatch", "M9 as M3 but the copy is followed by a tiny blit fill (a blitter operation in between)",
        "M10 compute copy into a DIFFERENT index buffer for every row (no reuse), no waits", "M11 compute copy into the same buffer, wait for the GPU after every row" };
    id<MTLBuffer> ibs[N]; for (int i = 0; i < N; i++) ibs[i] = [dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
    for (int mode = 0; mode < 12; mode++) {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.colorAttachments[0].texture = tex;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1); rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        { id<MTLCommandBuffer> c = [q commandBuffer]; id<MTLRenderCommandEncoder> e = [c renderCommandEncoderWithDescriptor:rp]; [e endEncoding]; [c commit]; [c waitUntilCompleted]; }
        rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
        id<MTLCommandBuffer> last = nil;
        for (int i = 0; i < N; i++) {
            int k = i % 3; uint32_t idx[3] = { k*3, k*3+1, k*3+2 };
            id<MTLBuffer> dst = (mode == 5 || mode == 10) ? ibs[i] : ib;
            memcpy((char *)stage.contents + i * 16, idx, 12);
            if (mode == 0) memcpy(dst.contents, idx, 12);
            id<MTLCommandBuffer> c = [q commandBuffer];
            if (mode == 1 || mode == 4 || mode == 5) { id<MTLBlitCommandEncoder> b = [c blitCommandEncoder]; [b copyFromBuffer:stage sourceOffset:i*16 toBuffer:dst destinationOffset:0 size:12]; [b endEncoding]; }
            if (mode == 2) { id<MTLBlitCommandEncoder> b = [c blitCommandEncoder]; [b copyFromBuffer:stage sourceOffset:i*16 toBuffer:dst destinationOffset:0 size:12]; [b endEncoding]; [c commit]; c = [q commandBuffer]; }
            if (mode == 3 || mode == 6 || mode == 7 || mode == 8 || mode == 9 || mode == 10 || mode == 11) { id<MTLComputeCommandEncoder> ce = [c computeCommandEncoder]; [ce setComputePipelineState:cps]; [ce setBuffer:stage offset:i*16 atIndex:0]; [ce setBuffer:dst offset:0 atIndex:1]; [ce dispatchThreads:MTLSizeMake(3,1,1) threadsPerThreadgroup:MTLSizeMake(3,1,1)];
                if (mode == 8) [ce memoryBarrierWithScope:MTLBarrierScopeBuffers];
                [ce endEncoding];
                if (mode == 9) { id<MTLBlitCommandEncoder> b = [c blitCommandEncoder]; [b fillBuffer:stage range:NSMakeRange(N * 16 - 4, 4) value:0]; [b endEncoding]; }
                if (mode == 7) { [c commit]; c = [q commandBuffer]; } }
            id<MTLRenderCommandEncoder> e = [c renderCommandEncoderWithDescriptor:rp];
            [e setRenderPipelineState:rps]; [e setVertexBuffer:vp offset:0 atIndex:0]; [e setVertexBuffer:vc offset:0 atIndex:1];
            if (mode == 4 || mode == 6) [e memoryBarrierWithScope:MTLBarrierScopeBuffers afterStages:MTLRenderStageVertex beforeStages:MTLRenderStageVertex];
            [e setScissorRect:(MTLScissorRect){ 0, (NSUInteger)i, 8, 1 }];
            [e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:3 indexType:MTLIndexTypeUInt32 indexBuffer:dst indexBufferOffset:0];
            [e endEncoding]; [c commit]; if (mode == 0 || mode == 11) [c waitUntilCompleted]; last = c;
        }
        [last waitUntilCompleted];
        static uint8_t px[8 * N * 4]; [tex getBytes:px bytesPerRow:8 * 4 fromRegion:MTLRegionMake2D(0, 0, 8, N) mipmapLevel:0];
        int bad = 0; for (int i = 0; i < N; i++) { uint8_t *c = &px[(i * 8 + 3) * 4]; int k = i % 3; if (c[0] != (k == 0) * 255 || c[1] != (k == 1) * 255 || c[2] != (k == 2) * 255) bad++; }
        printf("%-72s %3d wrong of %d\n", names[mode], bad, N);
    }
} return 0; }
