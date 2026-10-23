// xe-vtcmp: is the hardware video decoder bit-exact? Encodes 30 frames (H.264 and HEVC) with the software encoder, decodes the SAME bitstream
// with the hardware decoder (required) and with the software decoder, in 420v, and compares every byte of every frame.
//   clang -fobjc-arc -w xe-vtcmp.m -framework Foundation -framework VideoToolbox -framework CoreMedia -framework CoreVideo -o xe-vtcmp && ./xe-vtcmp
#import <VideoToolbox/VideoToolbox.h>
#import <CoreVideo/CoreVideo.h>
#include <stdio.h>
#include <stdlib.h>
#define W 1280
#define H 720
static NSMutableArray *g_enc;
static void encode(CMVideoCodecType codec, int pattern) {
    g_enc = [NSMutableArray array]; VTCompressionSessionRef cs;
    OSStatus st = VTCompressionSessionCreate(NULL, W, H, codec, (__bridge CFDictionaryRef)@{ (id)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder: @NO }, NULL, NULL, NULL, NULL, &cs);
    if (st) { printf("  encoder: st=%d\n", (int)st); return; }
    VTSessionSetProperty(cs, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(cs, kVTCompressionPropertyKey_RealTime, kCFBooleanFalse);
    VTSessionSetProperty(cs, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef)@10);        // several I frames and P frames
    for (int f = 0; f < 30; f++) {
        CVPixelBufferRef pb; CVPixelBufferCreate(NULL, W, H, '420v', (__bridge CFDictionaryRef)@{ (id)kCVPixelBufferIOSurfacePropertiesKey: @{} }, &pb);
        CVPixelBufferLockBaseAddress(pb, 0); uint8_t *Y = CVPixelBufferGetBaseAddressOfPlane(pb, 0); size_t by = CVPixelBufferGetBytesPerRowOfPlane(pb, 0);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) Y[y * by + x] = pattern ? (uint8_t)(16 + ((x * 3 + y * 2 + f * 11) % 220)) : (uint8_t)((x + y + f * 8) ^ (x >> 3));
        uint8_t *C = CVPixelBufferGetBaseAddressOfPlane(pb, 1); size_t bc = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
        for (int y = 0; y < H / 2; y++) for (int x = 0; x < W; x += 2) { C[y * bc + x] = 90 + (x / 16 + f) % 80; C[y * bc + x + 1] = 100 + (y / 16) % 60; }
        CVPixelBufferUnlockBaseAddress(pb, 0);
        VTCompressionSessionEncodeFrameWithOutputHandler(cs, pb, CMTimeMake(f, 30), kCMTimeInvalid, NULL, NULL, ^(OSStatus s, VTEncodeInfoFlags fl, CMSampleBufferRef sb) { if (!s && sb) [g_enc addObject:(__bridge id)sb]; });
        CVPixelBufferRelease(pb);
    }
    VTCompressionSessionCompleteFrames(cs, kCMTimeInvalid); VTCompressionSessionInvalidate(cs); CFRelease(cs);
}
static NSArray *decode(NSDictionary *spec, const char *what, OSStatus *err, OSType fmt) {
    NSMutableArray *out = [NSMutableArray array]; *err = 0; if (!g_enc.count) return out;
    CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription((__bridge CMSampleBufferRef)g_enc[0]); VTDecompressionSessionRef ds = NULL;
    NSDictionary *dst = @{ (id)kCVPixelBufferPixelFormatTypeKey: @((uint32_t)fmt), (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };   // NOTE: @('420v') would box a single char
    OSStatus st = VTDecompressionSessionCreate(NULL, fd, (__bridge CFDictionaryRef)spec, (__bridge CFDictionaryRef)dst, NULL, &ds);
    if (st) { printf("  %-9s cannot create session: %d\n", what, (int)st); *err = st; return out; }
    CFBooleanRef hw = NULL; VTSessionCopyProperty(ds, kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder, NULL, &hw); printf("  %-9s session created, hardware=%s\n", what, hw && CFBooleanGetValue(hw) ? "yes" : "no");
    for (id sb in g_enc) {
        __block CVPixelBufferRef got = NULL; __block OSStatus ds_st = 0;
        VTDecompressionSessionDecodeFrameWithOutputHandler(ds, (__bridge CMSampleBufferRef)sb, 0, NULL, ^(OSStatus s, VTDecodeInfoFlags fl, CVImageBufferRef ib, CMTime pts, CMTime dur) { ds_st = s; if (!s && ib) got = CVPixelBufferRetain(ib); });
        VTDecompressionSessionWaitForAsynchronousFrames(ds);
        if (got) [out addObject:(__bridge_transfer id)got]; else { [out addObject:[NSNull null]]; *err = ds_st; }
    }
    VTDecompressionSessionInvalidate(ds); CFRelease(ds); return out;
}
static void compare(NSArray *a, NSArray *b, const char *label) {
    long totalDiff = 0, totalBytes = 0; int framesBad = 0, framesMissing = 0, maxd = 0; int firstBadFrame = -1;
    for (NSUInteger i = 0; i < a.count && i < b.count; i++) {
        if (a[i] == [NSNull null] || b[i] == [NSNull null]) { framesMissing++; continue; }
        CVPixelBufferRef pa = (__bridge CVPixelBufferRef)a[i], pb = (__bridge CVPixelBufferRef)b[i]; long d = 0;
        CVPixelBufferLockBaseAddress(pa, kCVPixelBufferLock_ReadOnly); CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        for (int plane = 0; plane < 2; plane++) { size_t pw = CVPixelBufferGetWidthOfPlane(pa, plane) * (plane ? 2 : 1), ph = CVPixelBufferGetHeightOfPlane(pa, plane);
            const uint8_t *x = CVPixelBufferGetBaseAddressOfPlane(pa, plane), *y = CVPixelBufferGetBaseAddressOfPlane(pb, plane); size_t bx = CVPixelBufferGetBytesPerRowOfPlane(pa, plane), by = CVPixelBufferGetBytesPerRowOfPlane(pb, plane);
            for (size_t r = 0; r < ph; r++) for (size_t c = 0; c < pw; c++) { int dd = abs((int)x[r * bx + c] - (int)y[r * by + c]); if (dd) { d++; if (dd > maxd) maxd = dd; } totalBytes++; } }
        CVPixelBufferUnlockBaseAddress(pa, kCVPixelBufferLock_ReadOnly); CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        if (d) { framesBad++; if (firstBadFrame < 0) firstBadFrame = (int)i; } totalDiff += d;
    }
    printf("  %-30s %s: %d of %lu frames differ, %ld of %ld bytes differ, max byte difference %d%s", label, (framesBad || framesMissing) ? "MISMATCH" : "bit-exact", framesBad, (unsigned long)a.count, totalDiff, totalBytes, maxd, framesMissing ? "" : "\n");
    if (framesMissing) printf(", %d frames missing\n", framesMissing);
}
int main(void) { @autoreleasepool {
    struct { const char *n; CMVideoCodecType c; } K[] = { { "H.264", kCMVideoCodecType_H264 }, { "HEVC", kCMVideoCodecType_HEVC } };
    for (int k = 0; k < 2; k++) for (int pat = 0; pat < 2; pat++) {
        printf("%s, test pattern %d:\n", K[k].n, pat); encode(K[k].c, pat); printf("  %lu encoded frames\n", (unsigned long)g_enc.count);
        OSStatus e1, e2; NSArray *hw = decode(@{ (id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES }, "hardware", &e1, '420v');
        NSArray *sw = decode(@{ (id)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder: @NO }, "software", &e2, '420v');
        if (hw.count && sw.count) compare(hw, sw, "hardware vs software (420v)");
        OSStatus e3, e4; NSArray *hb = decode(@{ (id)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES }, "hw BGRA", &e3, 'BGRA'); int got = 0; for (id o in hb) if (o != [NSNull null]) got++;
        printf("  hardware + BGRA output: %d of %lu frames decoded (last status %d)\n", got, (unsigned long)g_enc.count, (int)e3); (void)e4; }
    return 0; } }
