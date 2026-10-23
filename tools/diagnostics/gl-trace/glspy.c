// glspy: logs the OpenGL state and draw stream of a process (used on Firefox's GPU helper). Loaded with DYLD_INSERT_LIBRARIES;
// interposes the state-changing and drawing calls, keeps a shadow of the state, and writes one line per draw/clear/blit to
// $GLSPY_OUT (default /tmp/glspy-<pid>.log). Logging stops after GLSPY_MAX lines (default 400000).
//   clang -dynamiclib -arch x86_64 -O1 glspy.c -framework OpenGL -o glspy.dylib
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <OpenGL/OpenGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <mach/mach_time.h>

#define DYLD_INTERPOSE(_r, _o) __attribute__((used)) static struct { const void *r; const void *o; } _interpose_##_o __attribute__((section("__DATA,__interpose"))) = { (const void *)(unsigned long)&_r, (const void *)(unsigned long)&_o }

static FILE *lf; static long nlines, maxlines = 400000; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static unsigned long seq;
static struct { int depth_test, blend, scissor, cull, stencil; GLenum dfunc; int dmask; GLenum sfc, dfc, sfa, dfa, beq; unsigned cmask; GLuint prog, fbo_draw; GLint vp[4], sc[4]; } S =
    { 0, 0, 0, 0, 0, GL_LESS, 1, GL_ONE, GL_ZERO, GL_ONE, GL_ZERO, GL_FUNC_ADD, 15, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
static struct { GLenum att; GLuint tex; GLint layer, level; } fa[3]; static int nfa;                    // attachments of the current draw fbo (shadow)
static void ensure(void) { if (lf) return; const char *o = getenv("GLSPY_OUT"); const char *dir = getenv("GLSPY_DIR"); char p[512]; if (dir) { snprintf(p, sizeof p, "%s/glspy-%d.log", dir, getpid()); o = p; } else if (!o) { snprintf(p, sizeof p, "/tmp/glspy-%d.log", getpid()); o = p; } lf = fopen(o, "w"); if (getenv("GLSPY_MAX")) maxlines = atol(getenv("GLSPY_MAX")); if (lf) { setvbuf(lf, NULL, _IOFBF, 1 << 16); fprintf(lf, "# glspy start pid %d\n", getpid()); } }
#define LOG(...) do { pthread_mutex_lock(&mu); ensure(); if (lf && nlines < maxlines) { fprintf(lf, __VA_ARGS__); nlines++; if ((nlines & 1023) == 0) fflush(lf); } pthread_mutex_unlock(&mu); } while (0)
static const char *capname(GLenum c) { switch (c) { case GL_DEPTH_TEST: return "DEPTH_TEST"; case GL_BLEND: return "BLEND"; case GL_SCISSOR_TEST: return "SCISSOR"; case GL_CULL_FACE: return "CULL"; case GL_STENCIL_TEST: return "STENCIL"; default: return NULL; } }
static void state_str(char *b, size_t n) { snprintf(b, n, "prog=%u fbo=%u vp=%d,%d,%d,%d z=%d/%x/m%d blend=%d(%x,%x|%x,%x eq%x) sc=%d[%d,%d,%d,%d] cm=%x", S.prog, S.fbo_draw, S.vp[0], S.vp[1], S.vp[2], S.vp[3], S.depth_test, S.dfunc, S.dmask, S.blend, S.sfc, S.dfc, S.sfa, S.dfa, S.beq, S.scissor, S.sc[0], S.sc[1], S.sc[2], S.sc[3], S.cmask); }
static void fb_str(char *b, size_t n) { b[0] = 0; size_t o = 0; for (int i = 0; i < nfa && o + 40 < n; i++) o += snprintf(b + o, n - o, " %s:tex%u/l%d", fa[i].att == GL_DEPTH_ATTACHMENT ? "D" : "C", fa[i].tex, fa[i].layer); }

static void my_glEnable(GLenum c) { const char *n = capname(c); if (n) { if (c == GL_DEPTH_TEST) S.depth_test = 1; else if (c == GL_BLEND) S.blend = 1; else if (c == GL_SCISSOR_TEST) S.scissor = 1; else if (c == GL_CULL_FACE) S.cull = 1; else if (c == GL_STENCIL_TEST) S.stencil = 1; } glEnable(c); }
static void my_glDisable(GLenum c) { const char *n = capname(c); if (n) { if (c == GL_DEPTH_TEST) S.depth_test = 0; else if (c == GL_BLEND) S.blend = 0; else if (c == GL_SCISSOR_TEST) S.scissor = 0; else if (c == GL_CULL_FACE) S.cull = 0; else if (c == GL_STENCIL_TEST) S.stencil = 0; } glDisable(c); }
static void my_glDepthFunc(GLenum f) { S.dfunc = f; glDepthFunc(f); }
static void my_glDepthMask(GLboolean m) { S.dmask = m; glDepthMask(m); }
static void my_glBlendFunc(GLenum s, GLenum d) { S.sfc = S.sfa = s; S.dfc = S.dfa = d; glBlendFunc(s, d); }
static void my_glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { S.sfc = a; S.dfc = b; S.sfa = c; S.dfa = d; glBlendFuncSeparate(a, b, c, d); }
static void my_glBlendEquation(GLenum e) { S.beq = e; glBlendEquation(e); }
static void my_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { S.cmask = r | (g << 1) | (b << 2) | (a << 3); glColorMask(r, g, b, a); }
static void my_glUseProgram(GLuint p) { S.prog = p; glUseProgram(p); }
static void my_glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { S.vp[0] = x; S.vp[1] = y; S.vp[2] = w; S.vp[3] = h; glViewport(x, y, w, h); }
static void my_glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { S.sc[0] = x; S.sc[1] = y; S.sc[2] = w; S.sc[3] = h; glScissor(x, y, w, h); }
static void my_glBindFramebuffer(GLenum t, GLuint f) { if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) { S.fbo_draw = f; nfa = 0; } glBindFramebuffer(t, f); LOG("%lu BindFramebuffer %x %u\n", ++seq, t, f); }
static void fb_note(GLenum att, GLuint tex, GLint layer, GLint level) { for (int i = 0; i < nfa; i++) if (fa[i].att == att) { fa[i].tex = tex; fa[i].layer = layer; fa[i].level = level; return; } if (nfa < 3) { fa[nfa].att = att; fa[nfa].tex = tex; fa[nfa].layer = layer; fa[nfa].level = level; nfa++; } }
static void my_glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint lv) { if (t != GL_READ_FRAMEBUFFER) fb_note(a, tex, 0, lv); glFramebufferTexture2D(t, a, tt, tex, lv); LOG("%lu FramebufferTexture2D att=%x tex=%u lvl=%d\n", ++seq, a, tex, lv); }
static void my_glFramebufferTextureLayer(GLenum t, GLenum a, GLuint tex, GLint lv, GLint layer) { if (t != GL_READ_FRAMEBUFFER) fb_note(a, tex, layer, lv); glFramebufferTextureLayer(t, a, tex, lv, layer); LOG("%lu FramebufferTextureLayer att=%x tex=%u lvl=%d layer=%d\n", ++seq, a, tex, lv, layer); }
static void my_glClear(GLbitfield m) { char s[300], f[160]; state_str(s, sizeof s); fb_str(f, sizeof f); glClear(m); LOG("%lu Clear mask=%x %s |%s\n", ++seq, m, s, f); }
static void my_glClearDepth(GLdouble d) { glClearDepth(d); LOG("%lu ClearDepth %g\n", ++seq, d); }
static void my_glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { glClearColor(r, g, b, a); LOG("%lu ClearColor %.3f %.3f %.3f %.3f\n", ++seq, r, g, b, a); }
static void my_glDrawArrays(GLenum m, GLint f, GLsizei c) { char s[300], fb[160]; state_str(s, sizeof s); fb_str(fb, sizeof fb); glDrawArrays(m, f, c); LOG("%lu DrawArrays mode=%x first=%d count=%d %s |%s\n", ++seq, m, f, c, s, fb); }
static void my_glDrawArraysInstanced(GLenum m, GLint f, GLsizei c, GLsizei n) { char s[300], fb[160]; state_str(s, sizeof s); fb_str(fb, sizeof fb); glDrawArraysInstanced(m, f, c, n); LOG("%lu DrawArraysInstanced mode=%x first=%d count=%d inst=%d %s |%s\n", ++seq, m, f, c, n, s, fb); }
static void my_glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) { char s[300], fb[160]; state_str(s, sizeof s); fb_str(fb, sizeof fb); glDrawElements(m, c, t, i); LOG("%lu DrawElements mode=%x count=%d type=%x off=%lu %s |%s\n", ++seq, m, c, t, (unsigned long)i, s, fb); }
static void my_glDrawElementsInstanced(GLenum m, GLsizei c, GLenum t, const void *i, GLsizei n) { char s[300], fb[160]; state_str(s, sizeof s); fb_str(fb, sizeof fb); glDrawElementsInstanced(m, c, t, i, n); LOG("%lu DrawElementsInstanced mode=%x count=%d type=%x off=%lu inst=%d %s |%s\n", ++seq, m, c, t, (unsigned long)i, n, s, fb); }
static void my_glBlitFramebuffer(GLint a, GLint b, GLint c, GLint d, GLint e, GLint f, GLint g, GLint h, GLbitfield m, GLenum fl) { glBlitFramebuffer(a, b, c, d, e, f, g, h, m, fl); LOG("%lu BlitFramebuffer src=%d,%d,%d,%d dst=%d,%d,%d,%d mask=%x filter=%x\n", ++seq, a, b, c, d, e, f, g, h, m, fl); }
static void my_glFlush(void) { glFlush(); } static void my_glFinish(void) { glFinish(); LOG("%lu Finish\n", ++seq); }

DYLD_INTERPOSE(my_glEnable, glEnable); DYLD_INTERPOSE(my_glDisable, glDisable); DYLD_INTERPOSE(my_glDepthFunc, glDepthFunc); DYLD_INTERPOSE(my_glDepthMask, glDepthMask);
DYLD_INTERPOSE(my_glBlendFunc, glBlendFunc); DYLD_INTERPOSE(my_glBlendFuncSeparate, glBlendFuncSeparate); DYLD_INTERPOSE(my_glBlendEquation, glBlendEquation); DYLD_INTERPOSE(my_glColorMask, glColorMask);
DYLD_INTERPOSE(my_glUseProgram, glUseProgram); DYLD_INTERPOSE(my_glViewport, glViewport); DYLD_INTERPOSE(my_glScissor, glScissor); DYLD_INTERPOSE(my_glBindFramebuffer, glBindFramebuffer);
DYLD_INTERPOSE(my_glFramebufferTexture2D, glFramebufferTexture2D); DYLD_INTERPOSE(my_glFramebufferTextureLayer, glFramebufferTextureLayer); DYLD_INTERPOSE(my_glClear, glClear); DYLD_INTERPOSE(my_glClearDepth, glClearDepth);
DYLD_INTERPOSE(my_glClearColor, glClearColor); DYLD_INTERPOSE(my_glDrawArrays, glDrawArrays); DYLD_INTERPOSE(my_glDrawArraysInstanced, glDrawArraysInstanced); DYLD_INTERPOSE(my_glDrawElements, glDrawElements);
DYLD_INTERPOSE(my_glDrawElementsInstanced, glDrawElementsInstanced); DYLD_INTERPOSE(my_glBlitFramebuffer, glBlitFramebuffer); DYLD_INTERPOSE(my_glFinish, glFinish);


// ---- shader tracking: which source is behind which program id (written to $GLSPY_DIR/prog<N>-vs.glsl / -fs.glsl)

// ---- text substitutions applied to every shader source before it reaches the driver ($GLSPY_SUBST = file of <<< old === new >>> blocks)
typedef struct { char *o, *n; } Sub; static Sub subs[32]; static int nsubs, subs_loaded;
static void load_subs(void) { if (subs_loaded) return; subs_loaded = 1; const char *fn = getenv("GLSPY_SUBST"); if (!fn) return; FILE *f = fopen(fn, "r"); if (!f) return; fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f); char *buf = malloc(sz + 1); fread(buf, 1, sz, f); buf[sz] = 0; fclose(f);
    char *p = buf; while (nsubs < 32) { char *a = strstr(p, "<<<\n"); if (!a) break; a += 4; char *m = strstr(a, "\n===\n"); if (!m) break; char *e = strstr(m + 5, "\n>>>"); if (!e) break;
        subs[nsubs].o = strndup(a, m - a); subs[nsubs].n = strndup(m + 5, e - (m + 5)); nsubs++; p = e + 4; } free(buf); }
static char *apply_subs(const char *src, int *count) { load_subs(); char *cur = strdup(src); *count = 0;
    for (int i = 0; i < nsubs; i++) { size_t ol = strlen(subs[i].o), nl = strlen(subs[i].n); if (!ol) continue; char *hit; char *scan = cur;
        while ((hit = strstr(scan, subs[i].o))) { size_t pre = hit - cur, post = strlen(hit + ol); char *nb = malloc(pre + nl + post + 1); memcpy(nb, cur, pre); memcpy(nb + pre, subs[i].n, nl); memcpy(nb + pre + nl, hit + ol, post + 1); free(cur); cur = nb; scan = cur + pre + nl; (*count)++; } }
    return cur; }
#define MAXSH 8192
static char *shsrc[MAXSH]; static GLenum shtype[MAXSH]; static GLuint attached[MAXSH][4]; static int nat[MAXSH];
static void header(const char *src, char *out, size_t n);
static GLuint my_glCreateShader(GLenum t) { GLuint s = glCreateShader(t); if (s < MAXSH) shtype[s] = t; return s; }
static void my_glShaderSource(GLuint sh, GLsizei n, const GLchar *const *str, const GLint *len) {
    size_t tot = 1; for (int i = 0; i < n; i++) tot += len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]); char *cat = malloc(tot); size_t o = 0;
    for (int i = 0; i < n; i++) { size_t l = len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]); memcpy(cat + o, str[i], l); o += l; } cat[o] = 0;
    int cnt = 0; char *out = apply_subs(cat, &cnt); free(cat);
    if (sh < MAXSH) { free(shsrc[sh]); shsrc[sh] = strdup(out); }
    if (cnt) { char h[200]; header(out, h, sizeof h); LOG("%lu ShaderSource sh=%u %s: %d substitution(s) applied\n", ++seq, sh, h, cnt); }
    const GLchar *one[1] = { out }; glShaderSource(sh, 1, one, NULL); }
static void my_glAttachShader(GLuint p, GLuint sh) { if (p < MAXSH && nat[p] < 4) attached[p][nat[p]++] = sh; glAttachShader(p, sh); }
static void header(const char *src, char *out, size_t n) { out[0] = 0; if (!src) return; const char *a = strstr(src, "\n// "); if (!a) return; a += 1; const char *e = strstr(a, "\n"); if (e) e = strstr(e + 1, "\n"); size_t l = e ? (size_t)(e - a) : 120; if (l >= n) l = n - 1; memcpy(out, a, l); out[l] = 0; for (char *c = out; *c; c++) if (*c == '\n') *c = ' '; }
static void my_glLinkProgram(GLuint p) { glLinkProgram(p); char dir[400] = ""; const char *d = getenv("GLSPY_DIR"); if (d) snprintf(dir, sizeof dir, "%s", d); char h[200] = "";
    if (p < MAXSH) for (int i = 0; i < nat[p]; i++) { GLuint sh = attached[p][i]; if (sh >= MAXSH || !shsrc[sh]) continue; header(shsrc[sh], h, sizeof h);
        if (d) { char fn[512]; snprintf(fn, sizeof fn, "%s/prog%u-%s.glsl", dir, p, shtype[sh] == GL_VERTEX_SHADER ? "vs" : "fs"); FILE *f = fopen(fn, "w"); if (f) { fputs(shsrc[sh], f); fclose(f); } }
        LOG("%lu LinkProgram prog=%u %s: %s\n", ++seq, p, shtype[sh] == GL_VERTEX_SHADER ? "vs" : "fs", h); } }
DYLD_INTERPOSE(my_glCreateShader, glCreateShader); DYLD_INTERPOSE(my_glShaderSource, glShaderSource); DYLD_INTERPOSE(my_glAttachShader, glAttachShader); DYLD_INTERPOSE(my_glLinkProgram, glLinkProgram);
/* files are created lazily on the first logged call, so processes that never use OpenGL leave nothing behind */
