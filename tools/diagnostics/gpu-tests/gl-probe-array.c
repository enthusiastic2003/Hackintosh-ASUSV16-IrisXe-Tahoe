// GL_TEXTURE_2D_ARRAY as render target and as sampled texture, the way WebRender uses its texture cache and render targets:
// per-layer framebuffer attachments, draws into one layer, sampling layers in shaders, blits and copies between layers.
// Every layer is read back and compared, so drawing into the wrong layer shows up as a wrong layer.
//   clang -w gl-probe-array.c -framework OpenGL -o gl-probe-array && ./gl-probe-array
#define GL_SILENCE_DEPRECATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#define W 256
#define H 256
#define L 6
static GLuint cs(GLenum t, const char *s) { GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, 0); glCompileShader(o);
    GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok); if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, 0, l); printf("shader error: %s\n", l); exit(3); } return o; }
static GLuint prog(const char *vs, const char *fs) { GLuint p = glCreateProgram(); glAttachShader(p, cs(GL_VERTEX_SHADER, vs)); glAttachShader(p, cs(GL_FRAGMENT_SHADER, fs)); glLinkProgram(p); return p; }
static int total_fail;
static void report(const char *n, int bad, int of, const char *extra) { printf("%-66s %s (%d bad of %d)%s%s\n", n, bad ? "FAIL" : "ok  ", bad, of, extra && bad ? "  " : "", bad && extra ? extra : ""); if (bad) total_fail++; }
static unsigned char col[L][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 }, { 0, 255, 255, 255 }, { 255, 0, 255, 255 } };
static GLuint fbo, tex, vao;
static void attach(GLuint t, int layer) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t, 0, layer); }
static void layer_pixels(GLuint t, int layer, unsigned char *out) { attach(t, layer); glFinish(); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, out); }
static int count_not(const unsigned char *px, const unsigned char *want) { int bad = 0; for (int i = 0; i < W * H; i++) if (abs(px[i * 4] - want[0]) > 2 || abs(px[i * 4 + 1] - want[1]) > 2 || abs(px[i * 4 + 2] - want[2]) > 2) bad++; return bad; }
static GLuint new_array(GLenum fmt, int layers) { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D_ARRAY, t); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, fmt, W, H, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); return t; }
static void clear_layer(GLuint t, int layer, const unsigned char *c) { attach(t, layer); glViewport(0, 0, W, H); glDisable(GL_SCISSOR_TEST); glClearColor(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, c[3] / 255.f); glClear(GL_COLOR_BUFFER_BIT); }

int main(void) {
    CGLPixelFormatAttribute at[] = { kCGLPFAAccelerated, kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFADepthSize, 24, 0 };
    CGLPixelFormatObj pf; GLint np; CGLContextObj ctx; if (CGLChoosePixelFormat(at, &pf, &np) || CGLCreateContext(pf, NULL, &ctx)) { printf("no context\n"); return 2; }
    CGLSetCurrentContext(ctx); printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenFramebuffers(1, &fbo);
    unsigned char *px = malloc(W * H * 4), *want = malloc(W * H * 4);
    const char *VS = "#version 150\nvoid main(){ vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID],0,1); }\n";

    printf("--- clear and read back each layer of an RGBA8 %dx%dx%d array through per-layer framebuffer attachments\n", W, H, L);
    tex = new_array(GL_RGBA8, L);
    for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
    { int bad = 0; char ex[160] = ""; for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d got %d,%d,%d want %d,%d,%d", l, px[0], px[1], px[2], col[l][0], col[l][1], col[l][2]); bad += b != 0; }
      report("each layer cleared to its own colour, read back per layer", bad, L, ex); }

    printf("--- draw into ONE layer; the other layers must keep their colour\n");
    { GLuint p = prog(VS, "#version 150\nout vec4 o; void main(){ o = vec4(1,1,1,1); }\n");
      for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      attach(tex, 3); glViewport(0, 0, W, H); glUseProgram(p); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
      int bad = 0; char ex[160] = ""; unsigned char white[4] = { 255, 255, 255, 255 };
      for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, l == 3 ? white : col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d: %d texels wrong (first %d,%d,%d)", l, b, px[0], px[1], px[2]); bad += b != 0; }
      report("white fullscreen draw into layer 3 only", bad, L, ex); }

    printf("--- sample layers in a shader (what WebRender's image and text batches do)\n");
    { GLuint pf_ = prog(VS, "#version 150\nuniform sampler2DArray t; uniform float layer; out vec4 o; void main(){ o = texelFetch(t, ivec3(gl_FragCoord.xy, int(layer)), 0); }\n");
      GLuint ps_ = prog(VS, "#version 150\nuniform sampler2DArray t; uniform float layer; out vec4 o; void main(){ o = texture(t, vec3(gl_FragCoord.xy / 256.0, layer)); }\n");
      GLuint dst = new_array(GL_RGBA8, 1); for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      for (int mode = 0; mode < 2; mode++) { int bad = 0; char ex[160] = ""; GLuint p = mode ? ps_ : pf_;
        for (int l = 0; l < L; l++) { attach(dst, 0); glViewport(0, 0, W, H); glUseProgram(p); glUniform1i(glGetUniformLocation(p, "t"), 0); glUniform1f(glGetUniformLocation(p, "layer"), (float)l);
          glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, tex); glDrawArrays(GL_TRIANGLES, 0, 3); layer_pixels(dst, 0, px); int b = count_not(px, col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d sampled as %d,%d,%d", l, px[0], px[1], px[2]); bad += b != 0; }
        report(mode ? "texture(sampler2DArray) of every layer (bilinear)" : "texelFetch(sampler2DArray) of every layer", bad, L, ex); } }

    printf("--- the array as render target AND source in consecutive passes (render task -> cache -> page)\n");
    { GLuint p = prog(VS, "#version 150\nuniform sampler2DArray t; out vec4 o; void main(){ vec4 c = texelFetch(t, ivec3(gl_FragCoord.xy, 1), 0); o = vec4(c.rgb * 0.5, 1.0); }\n");
      for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      attach(tex, 4); glViewport(0, 0, W, H); glUseProgram(p); glUniform1i(glGetUniformLocation(p, "t"), 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, tex); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
      unsigned char half[4] = { 0, 127, 0, 255 }; int bad = 0; char ex[160] = "";
      for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, l == 4 ? half : col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d: %d wrong (first %d,%d,%d)", l, b, px[0], px[1], px[2]); bad += b != 0; }
      report("layer 4 = half of layer 1 (read layer 1, write layer 4)", bad, L, ex); }

    printf("--- blits and copies between layers\n");
    { for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      GLuint rd; glGenFramebuffers(1, &rd); glBindFramebuffer(GL_READ_FRAMEBUFFER, rd); glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo); glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 5);
      glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST); glFinish();
      int bad = 0; char ex[160] = ""; for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, l == 5 ? col[0] : col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d: %d wrong (first %d,%d,%d)", l, b, px[0], px[1], px[2]); bad += b != 0; }
      report("glBlitFramebuffer layer 0 -> layer 5", bad, L, ex);
      for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      attach(tex, 2); glBindTexture(GL_TEXTURE_2D_ARRAY, tex); glCopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, 0, 0, W, H); glFinish();   // copy FB layer 2 into layer 1
      bad = 0; ex[0] = 0; for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, l == 1 ? col[2] : col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d: %d wrong (first %d,%d,%d)", l, b, px[0], px[1], px[2]); bad += b != 0; }
      report("glCopyTexSubImage3D framebuffer layer 2 -> array layer 1", bad, L, ex); }

    printf("--- partial-region updates inside a layer (texture cache style): upload a rect into layer 2, draw a rect into layer 4\n");
    { for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      static unsigned char rect[100 * 60 * 4]; for (int i = 0; i < 100 * 60; i++) { rect[i * 4] = 10; rect[i * 4 + 1] = 20; rect[i * 4 + 2] = 30; rect[i * 4 + 3] = 255; }
      glBindTexture(GL_TEXTURE_2D_ARRAY, tex); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 37, 53, 2, 100, 60, 1, GL_RGBA, GL_UNSIGNED_BYTE, rect);
      GLuint p = prog(VS, "#version 150\nout vec4 o; void main(){ o = vec4(1,1,1,1); }\n"); attach(tex, 4); glViewport(0, 0, W, H); glEnable(GL_SCISSOR_TEST); glScissor(70, 90, 120, 50); glUseProgram(p); glDrawArrays(GL_TRIANGLES, 0, 3); glDisable(GL_SCISSOR_TEST); glFinish();
      int bad = 0, badother = 0; for (int l = 0; l < L; l++) { layer_pixels(tex, l, px);
          for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const unsigned char *g = px + (y * W + x) * 4; unsigned char e[3] = { col[l][0], col[l][1], col[l][2] };
              if (l == 2 && x >= 37 && x < 137 && y >= 53 && y < 113) { e[0] = 10; e[1] = 20; e[2] = 30; } if (l == 4 && x >= 70 && x < 190 && y >= 90 && y < 140) { e[0] = e[1] = e[2] = 255; }
              if (abs(g[0] - e[0]) > 2 || abs(g[1] - e[1]) > 2 || abs(g[2] - e[2]) > 2) { if (l == 2 || l == 4) bad++; else badother++; } } }
      report("sub-rect upload into layer 2 and scissored draw into layer 4", bad + badother, W * H * L, badother ? "other layers were touched" : NULL); }

    printf("--- depth attachment as an array layer (opaque pass into a layered render target)\n");
    { GLuint d; glGenTextures(1, &d); glBindTexture(GL_TEXTURE_2D_ARRAY, d); glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, W, H, L, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      GLuint p = prog("#version 150\nuniform float z; uniform vec4 c; flat out vec4 fc; void main(){ vec2 q[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3)); gl_Position = vec4(q[gl_VertexID], z, 1); fc = c; }\n", "#version 150\nflat in vec4 fc; out vec4 o; void main(){ o = fc; }\n");
      for (int l = 0; l < L; l++) clear_layer(tex, l, col[l]);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 2); glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, d, 0, 2);
      glViewport(0, 0, W, H); glClearDepth(1.0); glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glUseProgram(p);
      glUniform1f(glGetUniformLocation(p, "z"), 0.2f); glUniform4f(glGetUniformLocation(p, "c"), 1, 1, 1, 1); glDrawArrays(GL_TRIANGLES, 0, 3);
      glUniform1f(glGetUniformLocation(p, "z"), 0.6f); glUniform4f(glGetUniformLocation(p, "c"), 0, 0, 0, 1); glDrawArrays(GL_TRIANGLES, 0, 3);       // farther: must be rejected
      glFinish(); glDisable(GL_DEPTH_TEST);
      unsigned char white[4] = { 255, 255, 255, 255 }; int bad = 0; char ex[160] = ""; for (int l = 0; l < L; l++) { layer_pixels(tex, l, px); int b = count_not(px, l == 2 ? white : col[l]); if (b && !ex[0]) snprintf(ex, sizeof ex, "layer %d: %d wrong (first %d,%d,%d)", l, b, px[0], px[1], px[2]); bad += b != 0; }
      report("depth test into layer 2 (near white first, far black rejected)", bad, L, ex); }
    printf("--- %d failing group(s)\n", total_fail); return total_fail != 0; }
