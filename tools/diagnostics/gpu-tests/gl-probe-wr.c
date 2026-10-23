// What WebRender does besides plain uploads, each with a checkable result:
//   IOSurface rectangle textures (CPU fill -> sample, and render into one -> CPU read), glBlitFramebuffer,
//   instanced draws, dual-source blending, texture swizzle, glMapBufferRange uploads.
//   clang gl-probe-wr.c -framework OpenGL -framework IOSurface -framework CoreFoundation -o gl-probe-wr
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>

static int total_fail;
static void report(const char *n, int bad, int of) { printf("%-64s %s (%d bad of %d)\n", n, bad ? "FAIL" : "ok  ", bad, of); if (bad) total_fail++; }
static GLuint compile(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader error: %s\n", l); exit(3); } return o; }
static const char *VS_FS = "#version 150\nvoid main(){ vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID],0,1); }\n";
static GLuint program(const char *vs, const char *fs) {
    GLuint p = glCreateProgram(); glAttachShader(p, compile(GL_VERTEX_SHADER, vs)); glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "p"); glBindAttribLocation(p, 1, "i"); glBindFragDataLocationIndexed(p, 0, 0, "o"); glBindFragDataLocationIndexed(p, 0, 1, "o1");
    glLinkProgram(p); return p;
}
static inline int B(int x, int y, int c) { return (x * 7 + y * 13 + c * 31 + 5) & 0xff; }

static IOSurfaceRef make_surface(int w, int h) {
    int bpe = 4; uint32_t pf = 'BGRA';
    CFNumberRef nw = CFNumberCreate(0, kCFNumberIntType, &w), nh = CFNumberCreate(0, kCFNumberIntType, &h),
                nb = CFNumberCreate(0, kCFNumberIntType, &bpe), np = CFNumberCreate(0, kCFNumberSInt32Type, &pf);
    const void *k[] = { kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat };
    const void *v[] = { nw, nh, nb, np };
    CFDictionaryRef d = CFDictionaryCreate(0, k, v, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef s = IOSurfaceCreate(d); CFRelease(d); CFRelease(nw); CFRelease(nh); CFRelease(nb); CFRelease(np); return s;
}

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx;
    if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx);
    printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    GLuint fbo, fbo2; glGenFramebuffers(1, &fbo); glGenFramebuffers(1, &fbo2);

    // ---- IOSurface rectangle textures
    GLuint progRect = program(VS_FS, "#version 150\nuniform sampler2DRect t; out vec4 o; void main(){ o = texture(t, gl_FragCoord.xy); }\n");
    GLuint progFill = program(VS_FS, "#version 150\nout vec4 o; void main(){ ivec2 p = ivec2(gl_FragCoord.xy); o = vec4(float((p.x*7+p.y*13+5)&255)/255., float((p.x*7+p.y*13+36)&255)/255., float((p.x*7+p.y*13+67)&255)/255., 1.); }\n");
    static const int WS[] = { 1, 3, 7, 16, 17, 31, 33, 63, 64, 65, 100, 127, 129, 255, 257, 500, 1000, 1366, 1920,
                              1563, 1920, 2560, 3000 };
    static const int HS[] = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 817, 1083, 1440, 1700 };
    int badA = 0, badB = 0, nA = 0; char la[200] = "", lb[200] = "";
    for (unsigned i = 0; i < sizeof(WS) / sizeof(WS[0]); i++) {
        int w = WS[i], h = HS[i]; IOSurfaceRef s = make_surface(w, h);
        if (!s) { printf("IOSurfaceCreate failed w=%d\n", w); continue; }
        size_t bpr = IOSurfaceGetBytesPerRow(s); nA++;
        // (a) CPU fill -> bind as rectangle texture -> sample into an FBO target -> compare
        IOSurfaceLock(s, 0, NULL); uint8_t *base = IOSurfaceGetBaseAddress(s);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { uint8_t *p = base + y * bpr + x * 4; p[2] = B(x, y, 0); p[1] = B(x, y, 1); p[0] = B(x, y, 2); p[3] = 255; }
        IOSurfaceUnlock(s, 0, NULL);
        GLuint ts, rt; glGenTextures(1, &ts); glBindTexture(GL_TEXTURE_RECTANGLE, ts);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        CGLError ce = CGLTexImageIOSurface2D(ctx, GL_TEXTURE_RECTANGLE, GL_RGBA, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, s, 0);
        glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
        glViewport(0, 0, w, h); glUseProgram(progRect); glUniform1i(glGetUniformLocation(progRect, "t"), 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, ts); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        uint8_t *rb = malloc((size_t)w * h * 4); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rb);
        int bad = ce != 0; for (int y = 0; y < h && !bad; y++) for (int x = 0; x < w; x++) { uint8_t *p = rb + ((size_t)y * w + x) * 4; if (p[0] != B(x, y, 0) || p[1] != B(x, y, 1) || p[2] != B(x, y, 2)) { bad = 1; break; } }
        if (bad) { badA++; char t[24]; snprintf(t, sizeof t, "%d(bpr%zu) ", w, bpr); strncat(la, t, sizeof la - strlen(la) - 1); }
        // (b) render INTO the IOSurface-backed texture, then read the surface on the CPU
        glBindFramebuffer(GL_FRAMEBUFFER, fbo2); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, ts, 0);
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glViewport(0, 0, w, h); glUseProgram(progFill); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        IOSurfaceLock(s, kIOSurfaceLockReadOnly, NULL); base = IOSurfaceGetBaseAddress(s);
        int badb = st != GL_FRAMEBUFFER_COMPLETE;
        for (int y = 0; y < h && !badb; y++) for (int x = 0; x < w; x++) {
            uint8_t *p = base + y * bpr + x * 4; int e = (x * 7 + y * 13 + 5) & 255;
            if (p[2] != e || p[1] != ((e + 31) & 255) || p[0] != ((e + 62) & 255)) { badb = 1; break; } }
        IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, NULL);
        if (badb) { badB++; char t[24]; snprintf(t, sizeof t, "%d(bpr%zu) ", w, bpr); strncat(lb, t, sizeof lb - strlen(lb) - 1); }
        free(rb); glDeleteTextures(1, &ts); glDeleteTextures(1, &rt); CFRelease(s);
    }
    report("IOSurface: CPU fill -> rectangle texture -> sample", badA, nA); if (badA) printf("    widths: %s\n", la);
    report("IOSurface: render into rectangle texture -> CPU read", badB, nA); if (badB) printf("    widths: %s\n", lb);

    // ---- glBlitFramebuffer
    {
        int W = 100, Hh = 40, bad = 0;
        GLuint a, b; glGenTextures(1, &a); glGenTextures(1, &b);
        glBindTexture(GL_TEXTURE_2D, a); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, Hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, b); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, Hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        uint8_t *src = malloc(W * Hh * 4), *dst = malloc(W * Hh * 4);
        for (int y = 0; y < Hh; y++) for (int x = 0; x < W; x++) for (int c = 0; c < 4; c++) src[(y * W + x) * 4 + c] = c == 3 ? 255 : B(x, y, c);
        glBindTexture(GL_TEXTURE_2D, a); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, Hh, GL_RGBA, GL_UNSIGNED_BYTE, src);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo2); glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b, 0);
        glBlitFramebuffer(0, 0, W, Hh, 0, 0, W, Hh, GL_COLOR_BUFFER_BIT, GL_NEAREST); glFinish();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo2); glReadPixels(0, 0, W, Hh, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        for (int i = 0; i < W * Hh * 4; i++) if (src[i] != dst[i]) bad++;
        report("glBlitFramebuffer 1:1 RGBA8 (100x40)", bad, W * Hh * 4);
        // sub-rect blit with an offset (partial copy)
        memset(dst, 0, W * Hh * 4); glBindTexture(GL_TEXTURE_2D, b); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, Hh, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo2);
        glBlitFramebuffer(11, 5, 61, 25, 3, 7, 53, 27, GL_COLOR_BUFFER_BIT, GL_NEAREST); glFinish();
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo2); glReadPixels(0, 0, W, Hh, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        bad = 0; for (int y = 7; y < 27; y++) for (int x = 3; x < 53; x++) for (int c = 0; c < 3; c++) if (dst[(y * W + x) * 4 + c] != src[((y - 2) * W + (x + 8)) * 4 + c]) bad++;
        report("glBlitFramebuffer sub-rect with offset", bad, 50 * 20 * 3);
        free(src); free(dst);
    }

    // ---- instanced draw with attribute divisor: 6 quads, colour per instance
    {
        const char *vs = "#version 150\nin vec2 p; in vec4 i; out vec4 c; void main(){ gl_Position = vec4(p * 0.2 + i.xy, 0, 1); c = vec4(i.z, i.w, 0, 1); }\n";
        const char *fs = "#version 150\nin vec4 c; out vec4 o; void main(){ o = c; }\n";
        GLuint pr = program(vs, fs);
        float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 }; float inst[6 * 4];
        for (int k = 0; k < 6; k++) { inst[k * 4] = -0.75f + 0.3f * k; inst[k * 4 + 1] = 0; inst[k * 4 + 2] = k / 6.0f + 0.1f; inst[k * 4 + 3] = 1 - k / 6.0f; }
        GLuint vb[2]; glGenBuffers(2, vb);
        glBindBuffer(GL_ARRAY_BUFFER, vb[0]); glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW); glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, vb[1]); glBufferData(GL_ARRAY_BUFFER, sizeof inst, inst, GL_STATIC_DRAW); glVertexAttribPointer(1, 4, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(1); glVertexAttribDivisor(1, 1);
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 100, 50, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        glViewport(0, 0, 100, 50); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(pr);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 6); glFinish();
        uint8_t px[100 * 50 * 4]; glReadPixels(0, 0, 100, 50, GL_RGBA, GL_UNSIGNED_BYTE, px);
        int bad = 0; for (int k = 0; k < 6; k++) { int cx = (int)((inst[k * 4] * 0.5f + 0.5f) * 100), cy = 25; uint8_t *p = px + (cy * 100 + cx) * 4;
            int er = (int)(inst[k * 4 + 2] * 255 + 0.5f), eg = (int)(inst[k * 4 + 3] * 255 + 0.5f); if (abs(p[0] - er) > 2 || abs(p[1] - eg) > 2) bad++; }
        report("instanced draw (divisor 1), colour per instance", bad, 6);
        glDisableVertexAttribArray(1); glVertexAttribDivisor(1, 0);
    }

    // ---- dual-source blending (subpixel text)
    {
        GLuint pr = program(VS_FS, "#version 150\nout vec4 o; out vec4 o1; void main(){ o = vec4(1.0, 0.5, 0.25, 1.0); o1 = vec4(0.5, 1.0, 0.0, 1.0); }\n");
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        glViewport(0, 0, 16, 16); glClearColor(0.5f, 0.5f, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR); glUseProgram(pr); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish(); glDisable(GL_BLEND);
        uint8_t px[16 * 16 * 4]; glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, px);
        // result = src*s1 + dst*(1-s1): r = 1*.5 + .5*.5 = .75, g = .5*1 + .5*0 = .5, b = .25*0 + .5*1 = .5
        int ok = abs(px[0] - 191) < 4 && abs(px[1] - 128) < 4 && abs(px[2] - 128) < 4;
        report("dual-source blend (SRC1_COLOR)", !ok, 1); if (!ok) printf("    got %d %d %d want 191 128 128\n", px[0], px[1], px[2]);
    }

    // ---- texture swizzle (R8 shown as alpha, BGRA via swizzle)
    {
        GLuint pr = program(VS_FS, "#version 150\nuniform sampler2D t; out vec4 o; void main(){ o = texelFetch(t, ivec2(gl_FragCoord.xy), 0); }\n");
        uint8_t src[4 * 4]; for (int i = 0; i < 16; i++) src[i] = 16 * i + 8;
        GLuint tx, rt; glGenTextures(1, &tx); glBindTexture(GL_TEXTURE_2D, tx); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 4, 4, 0, GL_RED, GL_UNSIGNED_BYTE, src);
        GLint sw[4] = { GL_ONE, GL_ONE, GL_ONE, GL_RED }; glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, sw);
        glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
        glViewport(0, 0, 4, 4); glUseProgram(pr); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tx); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        uint8_t px[4 * 4 * 4]; glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, px);
        int bad = 0; for (int i = 0; i < 16; i++) if (px[i * 4] != 255 || px[i * 4 + 1] != 255 || px[i * 4 + 2] != 255 || abs(px[i * 4 + 3] - src[i]) > 1) bad++;
        report("texture swizzle (R8 -> 1,1,1,R)", bad, 16);
    }

    // ---- glMapBufferRange upload + draw
    {
        GLuint pr = program("#version 150\nin vec2 p; void main(){ gl_Position = vec4(p,0,1); }\n", "#version 150\nout vec4 o; void main(){ o = vec4(1,0,0,1); }\n");
        GLuint vb; glGenBuffers(1, &vb); glBindBuffer(GL_ARRAY_BUFFER, vb); glBufferData(GL_ARRAY_BUFFER, 1024, NULL, GL_DYNAMIC_DRAW);
        float *m = glMapBufferRange(GL_ARRAY_BUFFER, 64, 24, GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        float tri[6] = { -1, -1, 3, -1, -1, 3 }; if (m) { memcpy(m, tri, sizeof tri); glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, 24); glUnmapBuffer(GL_ARRAY_BUFFER); }
        glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, (void *)64); glEnableVertexAttribArray(0);
        GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        glViewport(0, 0, 8, 8); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(pr); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
        uint8_t px[8 * 8 * 4]; glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, px);
        int bad = 0; for (int i = 0; i < 64; i++) if (px[i * 4] != 255) bad++;
        report("glMapBufferRange(flush explicit, unsynchronized) then draw", bad, 64);
    }
    printf("--- %d failing group(s)\n", total_fail);
    return total_fail != 0;
}
