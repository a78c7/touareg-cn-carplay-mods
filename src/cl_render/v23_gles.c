/*
 * v23_gles.c — 途锐 MH2p v23 统一单画布 60fps OpenGL ES 2.0 + QuickJS 零编译皮肤渲染引擎
 *
 * 由 cluster.c 在 #ifdef USE_DASH 下包含，提供两种运行模式：
 *   1. capture=v23      : CarPlay 实时地图（NVDEC 硬解 -> screen_blit 进三缓冲离屏 pixmap
 *                         -> EGL_KHR_image_pixmap 零拷贝纹理，不兼容自动回退 glTexSubImage2D）
 *                         与上层 QuickJS 60fps 仪表/动效/字体/导航卡在同一窗口 58 ([58, 33]) 合成。
 *   2. capture=v23_idle : 断开 CarPlay 且开启 gles60_mode 时的全屏 60fps 自绘仪表模式。
 *
 * 车机硬件安全红线：
 *   - 所有 GLES2 / EGLImage 函数一律动态解析，绝不直链 libGLESv2 桩符号；
 *   - 绝不调用 glTexImage2D(NULL)（Tegra K1 驱动会崩溃），一律传入零填充 CPU 缓冲；
 *   - /tmp 下绝不调用 rename()（MH2p QNX RAMFS 不支持），一律原地覆写；
 *   - 自定义 JS 与 GLSL 均受独立保险丝保护：JS 异常/连续超 35ms/爆 4MB 当帧无缝切回内置 default_classic
 *     并弹出顶部红条提示，坏 Shader 仅关光效层，绝不黑屏。
 *
 * Copyright (c) 2026 a78c7 (https://github.com/a78c7/touareg-cn-carplay-mods)
 * Licensed under CC BY-NC-SA 4.0 (see LICENSES/CC-BY-NC-SA-4.0.md and LICENSES/THIRD_PARTY_NOTICES.md)
 * FREE & OPEN SOURCE — STRICTLY PROHIBITED FOR COMMERCIAL SALE.
 */

#ifdef USE_DASH

#include "v23_sandbox.h"

/* ---- 动态解析补充 GLES2 & EGLImage 函数 ---- */
typedef void  (*V23_PFN_Uniform1f)(GLint, GLfloat);
typedef void  (*V23_PFN_Uniform2f)(GLint, GLfloat, GLfloat);
typedef void  (*V23_PFN_Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void  (*V23_PFN_Enable)(GLenum);
typedef void  (*V23_PFN_Disable)(GLenum);
typedef void  (*V23_PFN_BlendFunc)(GLenum, GLenum);
typedef void  (*V23_PFN_GetShaderiv)(GLuint, GLenum, GLint *);
typedef void  (*V23_PFN_GetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void  (*V23_PFN_GetProgramiv)(GLuint, GLenum, GLint *);
typedef void  (*V23_PFN_GetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void  (*V23_PFN_DeleteShader)(GLuint);
typedef void  (*V23_PFN_DeleteProgram)(GLuint);
typedef void  (*V23_PFN_DeleteTextures)(GLsizei, const GLuint *);
typedef void  (*V23_PFN_PixelStorei)(GLenum, GLint);
typedef void *(*V23_PFN_EglCreateImageKHR)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint *);
typedef EGLBoolean (*V23_PFN_EglDestroyImageKHR)(EGLDisplay, void *);
typedef void  (*V23_PFN_GlEGLImageTargetTex2D)(GLenum, void *);

#ifndef EGL_NATIVE_PIXMAP_KHR
#define EGL_NATIVE_PIXMAP_KHR 0x30B0
#endif
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#ifndef GL_ALPHA
#define GL_ALPHA 0x1906
#endif

static struct {
    V23_PFN_Uniform1f          Uniform1f;
    V23_PFN_Uniform2f          Uniform2f;
    V23_PFN_Uniform4f          Uniform4f;
    V23_PFN_Enable             Enable;
    V23_PFN_Disable            Disable;
    V23_PFN_BlendFunc          BlendFunc;
    V23_PFN_GetShaderiv        GetShaderiv;
    V23_PFN_GetShaderInfoLog   GetShaderInfoLog;
    V23_PFN_GetProgramiv       GetProgramiv;
    V23_PFN_GetProgramInfoLog  GetProgramInfoLog;
    V23_PFN_DeleteShader       DeleteShader;
    V23_PFN_DeleteProgram      DeleteProgram;
    V23_PFN_DeleteTextures     DeleteTextures;
    V23_PFN_PixelStorei        PixelStorei;
    V23_PFN_EglCreateImageKHR  EglCreateImageKHR;
    V23_PFN_EglDestroyImageKHR EglDestroyImageKHR;
    V23_PFN_GlEGLImageTargetTex2D GlEGLImageTargetTex2D;
} v23gl;

static void *v23_sym(const char *name) {
    return cl_gl_sym(name);
}

/* 直写 /tmp/v23_diag.log（每步立刻落盘，不依赖 stderr 重定向，方便实车排障） */
static void v23_diag_reset(void) {
    int fd = open("/tmp/v23_diag.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) close(fd);
}

static void v23_diag(const char *fmt, ...) {
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(buf) - 2) n = (int)sizeof(buf) - 2;
    buf[n++] = '\n';
    buf[n] = '\0';
    int fd = open("/tmp/v23_diag.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        (void)write(fd, buf, (size_t)n);
        close(fd);
    }
    LOG("%s", buf);
}

static int v23_load_extra_gl(void) {
    memset(&v23gl, 0, sizeof(v23gl));
    v23gl.Uniform1f          = (V23_PFN_Uniform1f)v23_sym("glUniform1f");
    v23gl.Uniform2f          = (V23_PFN_Uniform2f)v23_sym("glUniform2f");
    v23gl.Uniform4f          = (V23_PFN_Uniform4f)v23_sym("glUniform4f");
    v23gl.Enable             = (V23_PFN_Enable)v23_sym("glEnable");
    v23gl.Disable            = (V23_PFN_Disable)v23_sym("glDisable");
    v23gl.BlendFunc          = (V23_PFN_BlendFunc)v23_sym("glBlendFunc");
    v23gl.GetShaderiv        = (V23_PFN_GetShaderiv)v23_sym("glGetShaderiv");
    v23gl.GetShaderInfoLog   = (V23_PFN_GetShaderInfoLog)v23_sym("glGetShaderInfoLog");
    v23gl.GetProgramiv       = (V23_PFN_GetProgramiv)v23_sym("glGetProgramiv");
    v23gl.GetProgramInfoLog  = (V23_PFN_GetProgramInfoLog)v23_sym("glGetProgramInfoLog");
    v23gl.DeleteShader       = (V23_PFN_DeleteShader)v23_sym("glDeleteShader");
    v23gl.DeleteProgram      = (V23_PFN_DeleteProgram)v23_sym("glDeleteProgram");
    v23gl.DeleteTextures     = (V23_PFN_DeleteTextures)v23_sym("glDeleteTextures");
    v23gl.PixelStorei        = (V23_PFN_PixelStorei)v23_sym("glPixelStorei");
    v23gl.EglCreateImageKHR  = (V23_PFN_EglCreateImageKHR)v23_sym("eglCreateImageKHR");
    v23gl.EglDestroyImageKHR = (V23_PFN_EglDestroyImageKHR)v23_sym("eglDestroyImageKHR");
    v23gl.GlEGLImageTargetTex2D = (V23_PFN_GlEGLImageTargetTex2D)v23_sym("glEGLImageTargetTexture2DOES");
    return (gl_CreateShader && gl_DrawArrays && v23gl.Uniform1f && v23gl.Uniform2f && v23gl.Uniform4f) ? 0 : -1;
}

/* ---- 与 cluster.c 共享的 v23 开关（g_v23_mode 已在 cluster.c 顶部定义）与四缓冲离屏 CarPlay 地图桥接器 ----
 * v23.4 根治零拷贝 EGL_KHR_image_pixmap 异步读写追尾与 Tegra K1 纹理缓存残留：
 *   - 槽位从 3 扩至 4（V23_MAP_SLOTS = 4）：
 *       free_idx    : 解码线程独占写入槽（VIC 2D screen_blit 目标，GL 线程绝不访问）；
 *       ready_idx   : 最新已完成转换槽（突发多帧时解码线程仅在 free_idx <-> ready_idx 间交换，绝不触碰在显槽）；
 *       front_idx   : GL 线程当前正在采样的活跃纹理槽；
 *       retired_idx : 上一轮刚从 front_idx 退下的冷却槽，必须至少经历 1 次完整 eglSwapBuffers 且满 15.5ms 后，
 *                     确认 Tegra 3D GPU 异步光栅化已彻底排空对旧纹理的读取，才允许归还至 ready_idx 池。
 *   - GL 显示窗口升级为独立 screen_context_t + 4 个窗口渲染缓冲（V23_WIN_BUFFERS = 4），
 *     消除双缓冲下 displaymanager 异步合成 Display 4 (LVDS2) 时的扫描线追尾白横条纹。 */
#define V23_MAP_SLOTS   4
#define V23_WIN_BUFFERS 4

static volatile sig_atomic_t g_v23_stop = 0;
static void v23_on_sig(int sig) { (void)sig; g_v23_stop = 1; }

typedef struct {
    pthread_mutex_t mx;
    int initialized;
    int width, height;
    screen_pixmap_t pix[V23_MAP_SLOTS];
    screen_buffer_t buf[V23_MAP_SLOTS];
    uint8_t        *ptr[V23_MAP_SLOTS];
    int             stride[V23_MAP_SLOTS];
    int             free_idx;    /* 解码线程独占写入槽 */
    int             ready_idx;   /* 最新已完成待取槽 */
    int             front_idx;   /* GL 线程当前显示槽 */
    int             retired_idx; /* 刚退下等待至少 1 次完整 VSync 排空 GPU 读取的冷却槽 */
    int             has_fresh;   /* ready_idx 是否存有尚未晋升的新帧 */
    uint32_t        seq;         /* 每次新帧 +1 */
    uint32_t        frames_in;
    int             auto_night;  /* 亮度滞回自动昼夜 (0=昼, 1=夜) */
    pthread_t       gl_tid;
    int             gl_started;
} v23_map_bridge_t;

static v23_map_bridge_t g_v23_map = {
    .mx = PTHREAD_MUTEX_INITIALIZER,
    .initialized = 0,
    .free_idx = 0, .ready_idx = 1, .front_idx = 2, .retired_idx = 3, .has_fresh = 0,
    .auto_night = 1
};

static void *v23_render_thread_entry(void *arg);

/* 由 setup_screen() 在 g_v23_mode==1 末尾调用：创建 4 个离屏 RGBX8888 pixmap 并以 1MB 栈启动 60fps GL 线程 */
static int v23_map_setup(int w, int h) {
    if (g_v23_map.initialized) return 0;
    v23_diag_reset();
    if (w <= 0) w = V23_CANVAS_W;
    if (h <= 0) h = V23_CANVAS_H;
    g_v23_map.width = w;
    g_v23_map.height = h;
    g_v23_map.free_idx = 0;
    g_v23_map.ready_idx = 1;
    g_v23_map.front_idx = 2;
    g_v23_map.retired_idx = 3;
    g_v23_map.has_fresh = 0;
    g_v23_map.seq = 0;
    g_v23_map.frames_in = 0;
    g_v23_map.auto_night = 1;
    g_v23_stop = 0;

    /* v23.6 根治 Tegra K1 显存属性冲突导致的偶发 0.1s 红块/白横条花屏：
     * MH2p QNX /sbin/screen NVIDIA 驱动明确报错（Sev 2）：
     *   "NV: USAGE_OPENGL_* is not supported with the USAGE_READ, USAGE_WRITE and USAGE_NATIVE."
     * 当离屏 pixmap 同时挂载 Pitch-Linear (READ|WRITE|NATIVE) 与 Block-Linear 3D (OPENGL_ES2) 标志时，
     * VIC 2D 引擎、CPU 亮度采样与 GK20A 3D 纹理缓存间无硬件一致性保证，在高德白天 2688x960 高码率流下偶发扫到脏缓存。
     * 因此默认采用零驱动冲突的纯 Pitch-Linear 离屏缓冲 + glTexSubImage2D 独立 GPU 纹理上传（仅在 15~20fps 新地图帧到达时上传，
     * 带宽占比 <0.6%、耗时 ~1.2ms，UI 仍满血 60fps 且与 2D 缓冲 100% 物理隔离）；仅建 v23_force_zerocopy 且无 v23_no_zerocopy 时才启用实验零拷贝。 */
    int want_zc = (access("/mnt/app/eso/bin/apps/cluster/v23_force_zerocopy", F_OK) == 0 &&
                   access("/mnt/app/eso/bin/apps/cluster/v23_no_zerocopy", F_OK) != 0);
    int fmt = SCREEN_FORMAT_RGBX8888;
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ |
                (want_zc ? SCREEN_USAGE_OPENGL_ES2 : 0);
    int sz[2] = { w, h };
    v23_diag("v23.6 map_setup start %dx%d slots=%d want_zc=%d", w, h, V23_MAP_SLOTS, want_zc);

    for (int i = 0; i < V23_MAP_SLOTS; i++) {
        if (screen_create_pixmap(&g_v23_map.pix[i], g_dec.scr_ctx) != 0) {
            v23_diag("v23: screen_create_pixmap[%d] failed errno=%d", i, errno);
            return -1;
        }
        screen_set_pixmap_property_iv(g_v23_map.pix[i], SCREEN_PROPERTY_FORMAT, &fmt);
        screen_set_pixmap_property_iv(g_v23_map.pix[i], SCREEN_PROPERTY_USAGE, &usage);
        screen_set_pixmap_property_iv(g_v23_map.pix[i], SCREEN_PROPERTY_BUFFER_SIZE, sz);
        int rc;
        do { rc = screen_create_pixmap_buffer(g_v23_map.pix[i]); } while (rc < 0 && errno == EINTR);
        if (rc != 0 && want_zc) {
            int fb_usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ;
            screen_set_pixmap_property_iv(g_v23_map.pix[i], SCREEN_PROPERTY_USAGE, &fb_usage);
            do { rc = screen_create_pixmap_buffer(g_v23_map.pix[i]); } while (rc < 0 && errno == EINTR);
        }
        if (rc != 0) {
            v23_diag("v23: pixmap_buffer[%d] failed rc=%d errno=%d", i, rc, errno);
            return -1;
        }
        g_v23_map.buf[i] = NULL;
        screen_get_pixmap_property_pv(g_v23_map.pix[i], SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&g_v23_map.buf[i]);
        void *p = NULL; int st = 0;
        screen_get_buffer_property_pv(g_v23_map.buf[i], SCREEN_PROPERTY_POINTER, &p);
        screen_get_buffer_property_iv(g_v23_map.buf[i], SCREEN_PROPERTY_STRIDE, &st);
        g_v23_map.ptr[i] = (uint8_t *)p;
        g_v23_map.stride[i] = st;
        if (p && st > 0) memset(p, 0, (size_t)st * (size_t)h);
    }
    /* 在启动独立 GL 线程跨上下文执行 eglCreateImageKHR 前，强制将 g_dec.scr_ctx 上的 4 个 pixmap 提交到服务端就绪 */
    screen_flush_context(g_dec.scr_ctx, SCREEN_WAIT_IDLE);
    v23_diag("v23: %d pixmaps ready ptr0=%p stride0=%d", V23_MAP_SLOTS, (void *)g_v23_map.ptr[0], g_v23_map.stride[0]);

    g_v23_map.initialized = 1;
    pthread_attr_t pattr;
    pthread_attr_init(&pattr);
    pthread_attr_setstacksize(&pattr, 1024u * 1024u); /* 显式 1 MB 栈，防 QNX 默认子线程小栈溢出 */
    int pt_rc = pthread_create(&g_v23_map.gl_tid, &pattr, v23_render_thread_entry, (void *)(intptr_t)1);
    pthread_attr_destroy(&pattr);
    if (pt_rc != 0) {
        v23_diag("v23: pthread_create render thread failed rc=%d", pt_rc);
        return -1;
    }
    g_v23_map.gl_started = 1;
    v23_diag("v23: gl render thread spawned (stack=1MB, isolated own_ctx)");
    return 0;
}

static void v23_map_shutdown(void) {
    g_v23_stop = 1;
    if (g_v23_map.gl_started) {
        pthread_join(g_v23_map.gl_tid, NULL);
        g_v23_map.gl_started = 0;
    }
    if (g_v23_map.initialized) {
        for (int i = 0; i < V23_MAP_SLOTS; i++) {
            if (g_v23_map.pix[i]) {
                screen_destroy_pixmap(g_v23_map.pix[i]);
                g_v23_map.pix[i] = NULL;
                g_v23_map.buf[i] = NULL;
                g_v23_map.ptr[i] = NULL;
            }
        }
        g_v23_map.initialized = 0;
    }
}

/* 由 cb_display_picture() 在 g_v23_mode==1 时调用（仅运行在解码主线程，独占 g_dec.scr_ctx）：
 * 把已通过 SurfaceWait 校验的解码帧 screen_blit 到专属 free_idx 槽，同步等待 2D 引擎空闲后只与 ready_idx 交换。 */
static void v23_map_on_decoded_frame(int didx, const int *blit_attribs) {
    if (!g_v23_map.initialized || didx < 0 || didx >= MAX_SURFACES) return;
    int slot = g_v23_map.free_idx;
    int rc = screen_blit(g_dec.scr_ctx, g_v23_map.buf[slot], g_dec.decode_bufs[didx], blit_attribs);
    cl_flush_blits_idle();
    if (rc != 0) {
        rdbg_err("v23_map_on_decoded_frame: screen_blit failed rc=%d errno=%d slot=%d didx=%d\n", rc, errno, slot, didx);
        return;
    }

    /* 每 10 帧（约 0.5~2 秒）采样一次地图亮度，零 DSI 自动昼夜切换 */
    int night = g_v23_map.auto_night;
    if ((g_v23_map.frames_in % 10u) == 0u && g_v23_map.ptr[slot] && g_v23_map.stride[slot] >= g_v23_map.width * 4) {
        night = v23_detect_night_rgbx(g_v23_map.ptr[slot], g_v23_map.width, g_v23_map.height,
                                      g_v23_map.stride[slot], night);
    }

    pthread_mutex_lock(&g_v23_map.mx);
    int old_ready = g_v23_map.ready_idx;
    g_v23_map.ready_idx = slot;
    g_v23_map.free_idx = old_ready;
    g_v23_map.has_fresh = 1;
    g_v23_map.seq++;
    g_v23_map.frames_in++;
    g_v23_map.auto_night = night;
    pthread_mutex_unlock(&g_v23_map.mx);
}

/* ---- GLSL ES 1.00 硬件着色器源码 ---- */
static const char *v23_vs_solid =
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_col;\n"
    "varying vec4 v_col;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_col = a_col; }\n";

static const char *v23_fs_solid =
    "precision mediump float;\n"
    "varying vec4 v_col;\n"
    "void main() { gl_FragColor = v_col; }\n";

static const char *v23_vs_quad =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); v_uv = a_uv; }\n";

/* SDF 圆角矩形 + 渐变 + 描边 + 外发光 */
static const char *v23_fs_rrect =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform vec2  u_size;\n"
    "uniform float u_rad;\n"
    "uniform float u_stroke_w;\n"
    "uniform float u_glow;\n"
    "uniform float u_grad_h;\n"
    "uniform vec4  u_fill1;\n"
    "uniform vec4  u_fill2;\n"
    "uniform vec4  u_stroke;\n"
    "uniform vec4  u_glow_col;\n"
    "void main() {\n"
    "  vec2 half_s = u_size * 0.5;\n"
    "  vec2 p = (v_uv - 0.5) * (u_size + vec2(u_glow * 2.0));\n"
    "  float r = min(u_rad, min(half_s.x, half_s.y));\n"
    "  vec2 q = abs(p) - (half_s - vec2(r));\n"
    "  float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "  vec4 outc = vec4(0.0);\n"
    "  if (u_glow > 0.5 && d > 0.0 && u_glow_col.a > 0.001) {\n"
    "    float ga = exp(-d * 3.0 / u_glow) * u_glow_col.a * 0.55;\n"
    "    outc = vec4(u_glow_col.rgb, ga);\n"
    "  }\n"
    "  float fill_m = 1.0 - smoothstep(-0.75, 0.75, d);\n"
    "  if (u_fill1.a > 0.001 && fill_m > 0.001) {\n"
    "    float gt = clamp((u_grad_h > 0.5 ? (p.x / max(1.0, u_size.x)) : (p.y / max(1.0, u_size.y))) + 0.5, 0.0, 1.0);\n"
    "    vec4 fc = mix(u_fill1, u_fill2, gt);\n"
    "    outc = mix(outc, fc, fill_m);\n"
    "  }\n"
    "  if (u_stroke.a > 0.001 && u_stroke_w > 0.1) {\n"
    "    float sm = (1.0 - smoothstep(-0.75, 0.75, abs(d + u_stroke_w * 0.5) - u_stroke_w * 0.5));\n"
    "    outc = mix(outc, u_stroke, sm * u_stroke.a);\n"
    "  }\n"
    "  if (outc.a < 0.002) discard;\n"
    "  gl_FragColor = outc;\n"
    "}\n";

/* SDF 抗锯齿圆/圆环 + 外发光 */
static const char *v23_fs_circle =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform float u_r;\n"
    "uniform float u_stroke_w;\n"
    "uniform float u_glow;\n"
    "uniform vec4  u_fill;\n"
    "uniform vec4  u_stroke;\n"
    "uniform vec4  u_glow_col;\n"
    "void main() {\n"
    "  float pad = u_r + u_stroke_w + u_glow + 2.0;\n"
    "  vec2 p = (v_uv - 0.5) * (pad * 2.0);\n"
    "  float d = length(p) - u_r;\n"
    "  vec4 outc = vec4(0.0);\n"
    "  if (u_glow > 0.5 && d > 0.0 && u_glow_col.a > 0.001) {\n"
    "    float ga = exp(-d * 3.0 / u_glow) * u_glow_col.a * 0.55;\n"
    "    outc = vec4(u_glow_col.rgb, ga);\n"
    "  }\n"
    "  if (u_fill.a > 0.001) {\n"
    "    float fm = 1.0 - smoothstep(-0.75, 0.75, d);\n"
    "    outc = mix(outc, u_fill, fm);\n"
    "  }\n"
    "  if (u_stroke.a > 0.001 && u_stroke_w > 0.1) {\n"
    "    float sm = 1.0 - smoothstep(-0.75, 0.75, abs(d) - u_stroke_w * 0.5);\n"
    "    outc = mix(outc, u_stroke, sm * u_stroke.a);\n"
    "  }\n"
    "  if (outc.a < 0.002) discard;\n"
    "  gl_FragColor = outc;\n"
    "}\n";

/* CarPlay 地图纹理着色器（居中保比例裁剪 + 圆角/圆形蒙版 + 暗角 + 描边 + BGRA/RGBA 通道纠正） */
static const char *v23_fs_map =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2  u_size;\n"
    "uniform vec4  u_crop;      /* (u0, v0, du, dv) */\n"
    "uniform float u_rad;\n"
    "uniform float u_is_circle;\n"
    "uniform float u_alpha;\n"
    "uniform float u_vignette;\n"
    "uniform float u_border_w;\n"
    "uniform vec4  u_border_col;\n"
    "uniform float u_swap_rb;\n"
    "void main() {\n"
    "  vec2 half_s = u_size * 0.5;\n"
    "  vec2 p = (v_uv - 0.5) * u_size;\n"
    "  float d;\n"
    "  if (u_is_circle > 0.5) {\n"
    "    d = length(p) - min(half_s.x, half_s.y);\n"
    "  } else {\n"
    "    float r = min(u_rad, min(half_s.x, half_s.y));\n"
    "    vec2 q = abs(p) - (half_s - vec2(r));\n"
    "    d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;\n"
    "  }\n"
    "  float mask = 1.0 - smoothstep(-0.75, 0.75, d);\n"
    "  if (mask < 0.002) discard;\n"
    "  vec2 muv = u_crop.xy + v_uv * u_crop.zw;\n"
    "  vec4 tc = texture2D(u_tex, muv);\n"
    "  vec3 rgb = (u_swap_rb > 0.5) ? vec3(tc.b, tc.g, tc.r) : tc.rgb;\n"
    "  if (u_vignette > 0.001) {\n"
    "    float vd = length((v_uv - 0.5) * 2.0);\n"
    "    float vf = clamp((vd - 0.5) / 0.85, 0.0, 1.0) * min(0.95, u_vignette);\n"
    "    rgb = mix(rgb, vec3(0.024, 0.035, 0.059), vf);\n"
    "  }\n"
    "  vec4 outc = vec4(rgb, mask * u_alpha);\n"
    "  if (u_border_w > 0.1 && u_border_col.a > 0.001) {\n"
    "    float bm = 1.0 - smoothstep(-0.75, 0.75, abs(d + u_border_w * 0.5) - u_border_w * 0.5);\n"
    "    outc = mix(outc, vec4(u_border_col.rgb, u_border_col.a * mask), bm);\n"
    "  }\n"
    "  gl_FragColor = outc;\n"
    "}\n";

/* RGBA 信息框/导航卡贴图（dash.c / navbox.c 写小端 0xAARRGGBB = BGRA 字节序，需互换 R/B） */
static const char *v23_fs_bgra =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_alpha;\n"
    "void main() {\n"
    "  vec4 c = texture2D(u_tex, v_uv);\n"
    "  gl_FragColor = vec4(c.b, c.g, c.r, c.a * u_alpha);\n"
    "}\n";

/* GL_ALPHA 动态 TTF 字体图集着色器 */
static const char *v23_fs_alpha_text =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec4 u_col;\n"
    "void main() {\n"
    "  float a = texture2D(u_tex, v_uv).a;\n"
    "  if (a < 0.005) discard;\n"
    "  gl_FragColor = vec4(u_col.rgb, u_col.a * a);\n"
    "}\n";

/* ---- 编译与校验 GLES2 Shader（带完整错误捕获，绝不让坏皮肤搞崩驱动） ---- */
static GLuint v23_compile_prog(const char *vs_s, const char *fs_s, char *err_out, size_t err_cap) {
    if (err_out && err_cap) err_out[0] = '\0';
    GLuint vs = gl_CreateShader(GL_VERTEX_SHADER);
    gl_ShaderSource(vs, 1, &vs_s, NULL);
    gl_CompileShader(vs);
    GLint ok = 1;
    if (v23gl.GetShaderiv) v23gl.GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (err_out && err_cap && v23gl.GetShaderInfoLog)
            v23gl.GetShaderInfoLog(vs, (GLsizei)err_cap - 1, NULL, err_out);
        if (v23gl.DeleteShader) v23gl.DeleteShader(vs);
        return 0;
    }

    GLuint fs = gl_CreateShader(GL_FRAGMENT_SHADER);
    gl_ShaderSource(fs, 1, &fs_s, NULL);
    gl_CompileShader(fs);
    ok = 1;
    if (v23gl.GetShaderiv) v23gl.GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (err_out && err_cap && v23gl.GetShaderInfoLog)
            v23gl.GetShaderInfoLog(fs, (GLsizei)err_cap - 1, NULL, err_out);
        if (v23gl.DeleteShader) { v23gl.DeleteShader(vs); v23gl.DeleteShader(fs); }
        return 0;
    }

    GLuint prog = gl_CreateProgram();
    gl_AttachShader(prog, vs);
    gl_AttachShader(prog, fs);
    gl_LinkProgram(prog);
    ok = 1;
    if (v23gl.GetProgramiv) v23gl.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (v23gl.DeleteShader) { v23gl.DeleteShader(vs); v23gl.DeleteShader(fs); }
    if (!ok) {
        if (err_out && err_cap && v23gl.GetProgramInfoLog)
            v23gl.GetProgramInfoLog(prog, (GLsizei)err_cap - 1, NULL, err_out);
        if (v23gl.DeleteProgram) v23gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* ---- 共享 GLES2 TTF 动态字形图集 (1024x1024 GL_ALPHA，512 槽 + O(1) 哈希表，复用 navbox.c 原厂 TTF) ---- */
#define V23_ATLAS_SZ     1024
#define V23_ATLAS_MAX    512
#define V23_ATLAS_HSZ    1024
#define V23_ATLAS_HMASK  (V23_ATLAS_HSZ - 1)

typedef struct {
    uint32_t cp;
    uint16_t px;
    uint8_t  bold;
    uint8_t  used;
    int      w, h, xoff, yoff, adv;
    float    u0, v0, u1, v1;
} v23_aglyph_t;

typedef struct {
    GLuint       tex;
    int          cur_x, cur_y, row_h;
    v23_aglyph_t slots[V23_ATLAS_MAX];
    uint16_t     htab[V23_ATLAS_HSZ]; /* 存 slot_index + 1，0 表示空槽 */
    int          count;
    int          inited;
} v23_atlas_t;

static inline uint32_t v23_atlas_hash(uint32_t cp, uint16_t px, uint8_t bold) {
    return ((cp * 2654435761u) ^ ((uint32_t)px * 40503u) ^ ((uint32_t)bold * 97u)) & V23_ATLAS_HMASK;
}

static void v23_atlas_reset(v23_atlas_t *at) {
    at->cur_x = 2; at->cur_y = 2; at->row_h = 0;
    at->count = 0;
    memset(at->slots, 0, sizeof(at->slots));
    memset(at->htab, 0, sizeof(at->htab));
}

static void v23_atlas_init(v23_atlas_t *at) {
    memset(at, 0, sizeof(*at));
    nav_fonts_init_once();
    gl_GenTextures(1, &at->tex);
    gl_BindTexture(GL_TEXTURE_2D, at->tex);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* 严禁 glTexImage2D(NULL)：分配全零 1MB 缓冲安全初始化 Tegra K1 纹理 */
    uint8_t *zeros = (uint8_t *)calloc((size_t)V23_ATLAS_SZ * (size_t)V23_ATLAS_SZ, 1);
    if (zeros) {
        if (v23gl.PixelStorei) v23gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, V23_ATLAS_SZ, V23_ATLAS_SZ, 0, GL_ALPHA, GL_UNSIGNED_BYTE, zeros);
        free(zeros);
    }
    v23_atlas_reset(at);
    at->inited = 1;
}

static const v23_aglyph_t *v23_atlas_get(v23_atlas_t *at, uint32_t cp, int px, int bold, int *did_upload) {
    if (!at->inited) return NULL;
    if (px < 10) px = 10;
    if (px > 180) px = 180;
    uint16_t upx = (uint16_t)px;
    uint8_t  ubd = (uint8_t)(bold ? 1 : 0);

    uint32_t h = v23_atlas_hash(cp, upx, ubd);
    for (int step = 0; step < V23_ATLAS_HSZ; step++) {
        uint16_t e = at->htab[(h + (uint32_t)step) & V23_ATLAS_HMASK];
        if (e == 0) break;
        v23_aglyph_t *g = &at->slots[e - 1u];
        if (g->used && g->cp == cp && g->px == upx && g->bold == ubd) return g;
    }

    int prefer_lat = (cp < 0x2000u) ? 1 : 0;
    const nav_glyph_ent_t *ng = nav_get_glyph(cp, px, ubd, prefer_lat);
    if (!ng) return NULL;

    if (at->count >= V23_ATLAS_MAX) v23_atlas_reset(at);
    int gw = ng->w, gh = ng->h;
    if (gw > 0 && gh > 0 && ng->bm) {
        if (at->cur_x + gw + 2 >= V23_ATLAS_SZ) {
            at->cur_x = 2;
            at->cur_y += at->row_h + 2;
            at->row_h = 0;
        }
        if (at->cur_y + gh + 2 >= V23_ATLAS_SZ) {
            v23_atlas_reset(at);
        }
    }
    int idx = at->count++;
    v23_aglyph_t *g = &at->slots[idx];
    g->cp = cp; g->px = upx; g->bold = ubd; g->used = 1;
    g->w = gw; g->h = gh; g->xoff = ng->xoff; g->yoff = ng->yoff; g->adv = ng->adv;
    if (gw > 0 && gh > 0 && ng->bm) {
        gl_BindTexture(GL_TEXTURE_2D, at->tex);
        if (v23gl.PixelStorei) v23gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl_TexSubImage2D(GL_TEXTURE_2D, 0, at->cur_x, at->cur_y, gw, gh, GL_ALPHA, GL_UNSIGNED_BYTE, ng->bm);
        if (did_upload) *did_upload = 1;
        g->u0 = (float)at->cur_x / (float)V23_ATLAS_SZ;
        g->v0 = (float)at->cur_y / (float)V23_ATLAS_SZ;
        g->u1 = (float)(at->cur_x + gw) / (float)V23_ATLAS_SZ;
        g->v1 = (float)(at->cur_y + gh) / (float)V23_ATLAS_SZ;
        at->cur_x += gw + 2;
        if (gh > at->row_h) at->row_h = gh;
    } else {
        g->u0 = g->v0 = g->u1 = g->v1 = 0.0f;
    }
    uint32_t ih = v23_atlas_hash(cp, upx, ubd);
    for (int step = 0; step < V23_ATLAS_HSZ; step++) {
        uint32_t slot = (ih + (uint32_t)step) & V23_ATLAS_HMASK;
        if (at->htab[slot] == 0) {
            at->htab[slot] = (uint16_t)(idx + 1);
            break;
        }
    }
    return g;
}

/* ---- 预缓存全部内置与用户 Shader 的 Attrib / Uniform 句柄（渲染循环零字符串查询） ---- */
typedef struct {
    GLint solid_pos, solid_col;
    GLint rrect_pos, rrect_uv, rrect_size, rrect_rad, rrect_sw, rrect_glow, rrect_gradh, rrect_f1, rrect_f2, rrect_st, rrect_gc;
    GLint circ_pos, circ_uv, circ_r, circ_sw, circ_glow, circ_fill, circ_st, circ_gc;
    GLint map_pos, map_uv, map_tex, map_size, map_crop, map_rad, map_circ, map_alpha, map_vig, map_bw, map_bc, map_swap;
    GLint bgra_pos, bgra_uv, bgra_tex, bgra_alpha;
    GLint text_pos, text_uv, text_tex, text_col;
    GLint usr_pos, usr_uv, usr_res, usr_time, usr_rpm, usr_spd, usr_pwr, usr_ped, usr_brk, usr_g, usr_map, usr_night;
} v23_locs_t;

/* ---- 渲染器总上下文（持有全部 GL 程序、纹理、当前帧总线与沙盒状态） ---- */
typedef struct {
    EGLDisplay edpy;
    int win_w, win_h;

    GLuint prog_solid, prog_rrect, prog_circle, prog_map, prog_bgra, prog_text;
    GLuint prog_user;
    v23_locs_t loc;
    GLuint cur_prog;
    GLuint cur_tex0;
    int    user_shader_ok;
    char   shader_err[256];

    /* CarPlay 地图纹理（默认启用实车已验通的 EGLImage 零拷贝；建 v23_no_zerocopy 或驱动失败时自动降级 glTexSubImage2D） */
    int      map_has_stream;
    int      map_zero_copy;
    GLuint   map_tex[V23_MAP_SLOTS];
    void    *map_egl_img[V23_MAP_SLOTS];
    GLuint   map_tex_fb;
    uint8_t *map_tight_buf;
    uint32_t map_last_seq;
    int      map_active_slot;
    int      map_ever_flipped;
    int      map_w, map_h;
    int      win_bufs;

    /* dash.c 信息框纹理[0..1] + navbox.c 导航卡纹理 */
    GLuint dash_tex[2];
    int    dash_en[2], dash_w[2], dash_h[2];
    GLuint nav_tex;
    int    nav_w, nav_h;

    v23_atlas_t atlas;
    v23_bus_t   cur_bus;

    /* 顶部安全提示横幅 */
    char     banner_text[320];
    int      banner_level; /* 0=info, 1=warn, 2=err */
    uint64_t banner_until_us;
} v23_gl_ctx_t;

static void v23_ndc_quad(float v[24], float x, float y, float w, float h, float ww, float wh) {
    float x0 = (x / ww) * 2.0f - 1.0f;
    float x1 = ((x + w) / ww) * 2.0f - 1.0f;
    float y0 = 1.0f - (y / wh) * 2.0f;
    float y1 = 1.0f - ((y + h) / wh) * 2.0f;
    float q[24] = {
        x0, y0, 0.0f, 0.0f,  x1, y0, 1.0f, 0.0f,  x0, y1, 0.0f, 1.0f,
        x1, y0, 1.0f, 0.0f,  x1, y1, 1.0f, 1.0f,  x0, y1, 0.0f, 1.0f
    };
    memcpy(v, q, sizeof(q));
}

static inline float v23_px_nx(float x, float ww) { return (x / ww) * 2.0f - 1.0f; }
static inline float v23_px_ny(float y, float wh) { return 1.0f - (y / wh) * 2.0f; }

static void v23_cache_core_locs(v23_gl_ctx_t *gc) {
    v23_locs_t *L = &gc->loc;
    L->solid_pos   = gl_GetAttribLocation(gc->prog_solid,  "a_pos");
    L->solid_col   = gl_GetAttribLocation(gc->prog_solid,  "a_col");

    L->rrect_pos   = gl_GetAttribLocation(gc->prog_rrect,  "a_pos");
    L->rrect_uv    = gl_GetAttribLocation(gc->prog_rrect,  "a_uv");
    L->rrect_size  = gl_GetUniformLocation(gc->prog_rrect, "u_size");
    L->rrect_rad   = gl_GetUniformLocation(gc->prog_rrect, "u_rad");
    L->rrect_sw    = gl_GetUniformLocation(gc->prog_rrect, "u_stroke_w");
    L->rrect_glow  = gl_GetUniformLocation(gc->prog_rrect, "u_glow");
    L->rrect_gradh = gl_GetUniformLocation(gc->prog_rrect, "u_grad_h");
    L->rrect_f1    = gl_GetUniformLocation(gc->prog_rrect, "u_fill1");
    L->rrect_f2    = gl_GetUniformLocation(gc->prog_rrect, "u_fill2");
    L->rrect_st    = gl_GetUniformLocation(gc->prog_rrect, "u_stroke");
    L->rrect_gc    = gl_GetUniformLocation(gc->prog_rrect, "u_glow_col");

    L->circ_pos    = gl_GetAttribLocation(gc->prog_circle,  "a_pos");
    L->circ_uv     = gl_GetAttribLocation(gc->prog_circle,  "a_uv");
    L->circ_r      = gl_GetUniformLocation(gc->prog_circle, "u_r");
    L->circ_sw     = gl_GetUniformLocation(gc->prog_circle, "u_stroke_w");
    L->circ_glow   = gl_GetUniformLocation(gc->prog_circle, "u_glow");
    L->circ_fill   = gl_GetUniformLocation(gc->prog_circle, "u_fill");
    L->circ_st     = gl_GetUniformLocation(gc->prog_circle, "u_stroke");
    L->circ_gc     = gl_GetUniformLocation(gc->prog_circle, "u_glow_col");

    L->map_pos     = gl_GetAttribLocation(gc->prog_map,  "a_pos");
    L->map_uv      = gl_GetAttribLocation(gc->prog_map,  "a_uv");
    L->map_tex     = gl_GetUniformLocation(gc->prog_map, "u_tex");
    L->map_size    = gl_GetUniformLocation(gc->prog_map, "u_size");
    L->map_crop    = gl_GetUniformLocation(gc->prog_map, "u_crop");
    L->map_rad     = gl_GetUniformLocation(gc->prog_map, "u_rad");
    L->map_circ    = gl_GetUniformLocation(gc->prog_map, "u_is_circle");
    L->map_alpha   = gl_GetUniformLocation(gc->prog_map, "u_alpha");
    L->map_vig     = gl_GetUniformLocation(gc->prog_map, "u_vignette");
    L->map_bw      = gl_GetUniformLocation(gc->prog_map, "u_border_w");
    L->map_bc      = gl_GetUniformLocation(gc->prog_map, "u_border_col");
    L->map_swap    = gl_GetUniformLocation(gc->prog_map, "u_swap_rb");

    L->bgra_pos    = gl_GetAttribLocation(gc->prog_bgra,  "a_pos");
    L->bgra_uv     = gl_GetAttribLocation(gc->prog_bgra,  "a_uv");
    L->bgra_tex    = gl_GetUniformLocation(gc->prog_bgra, "u_tex");
    L->bgra_alpha  = gl_GetUniformLocation(gc->prog_bgra, "u_alpha");

    L->text_pos    = gl_GetAttribLocation(gc->prog_text,  "a_pos");
    L->text_uv     = gl_GetAttribLocation(gc->prog_text,  "a_uv");
    L->text_tex    = gl_GetUniformLocation(gc->prog_text, "u_tex");
    L->text_col    = gl_GetUniformLocation(gc->prog_text, "u_col");
}

static void v23_cache_user_locs(v23_gl_ctx_t *gc) {
    v23_locs_t *L = &gc->loc;
    GLuint p = gc->prog_user;
    L->usr_pos   = p ? gl_GetAttribLocation(p,  "a_pos")      : -1;
    L->usr_uv    = p ? gl_GetAttribLocation(p,  "a_uv")       : -1;
    L->usr_res   = p ? gl_GetUniformLocation(p, "u_res")      : -1;
    L->usr_time  = p ? gl_GetUniformLocation(p, "u_time")     : -1;
    L->usr_rpm   = p ? gl_GetUniformLocation(p, "u_rpm")      : -1;
    L->usr_spd   = p ? gl_GetUniformLocation(p, "u_speed")    : -1;
    L->usr_pwr   = p ? gl_GetUniformLocation(p, "u_power")    : -1;
    L->usr_ped   = p ? gl_GetUniformLocation(p, "u_pedal")    : -1;
    L->usr_brk   = p ? gl_GetUniformLocation(p, "u_brake")    : -1;
    L->usr_g     = p ? gl_GetUniformLocation(p, "u_g")        : -1;
    L->usr_map   = p ? gl_GetUniformLocation(p, "u_map_live") : -1;
    L->usr_night = p ? gl_GetUniformLocation(p, "u_night")    : -1;
}

static inline void v23_use_prog(v23_gl_ctx_t *gc, GLuint prog) {
    if (gc->cur_prog != prog) {
        gl_UseProgram(prog);
        gc->cur_prog = prog;
    }
}

static inline void v23_bind_tex0(v23_gl_ctx_t *gc, GLuint tex) {
    if (gc->cur_tex0 != tex) {
        gl_ActiveTexture(GL_TEXTURE0);
        gl_BindTexture(GL_TEXTURE_2D, tex);
        gc->cur_tex0 = tex;
    }
}

static void v23_draw_solid_tris(v23_gl_ctx_t *gc, const float *verts, int nverts) {
    /* verts: [x_ndc, y_ndc, r, g, b, a] * nverts */
    if (!gc->prog_solid || nverts < 3) return;
    v23_use_prog(gc, gc->prog_solid);
    GLint loc_p = gc->loc.solid_pos;
    GLint loc_c = gc->loc.solid_col;
    gl_EnableVertexAttribArray(loc_p);
    gl_EnableVertexAttribArray(loc_c);
    gl_VertexAttribPointer(loc_p, 2, GL_FLOAT, GL_FALSE, 6 * (GLsizei)sizeof(float), verts);
    gl_VertexAttribPointer(loc_c, 4, GL_FLOAT, GL_FALSE, 6 * (GLsizei)sizeof(float), verts + 2);
    gl_DrawArrays(GL_TRIANGLES, 0, nverts);
}

static void v23_push_v(float *out, int *cnt, float x, float y, float ww, float wh, v23_rgba_t c) {
    float *p = out + (*cnt) * 6;
    p[0] = v23_px_nx(x, ww);
    p[1] = v23_px_ny(y, wh);
    p[2] = c.r; p[3] = c.g; p[4] = c.b; p[5] = c.a;
    (*cnt)++;
}

static inline void v23_push_quad(float *out, int *cnt, float x0, float y0, float x1, float y1,
                                 float x2, float y2, float x3, float y3, float ww, float wh, v23_rgba_t c) {
    v23_push_v(out, cnt, x0, y0, ww, wh, c);
    v23_push_v(out, cnt, x1, y1, ww, wh, c);
    v23_push_v(out, cnt, x2, y2, ww, wh, c);
    v23_push_v(out, cnt, x0, y0, ww, wh, c);
    v23_push_v(out, cnt, x2, y2, ww, wh, c);
    v23_push_v(out, cnt, x3, y3, ww, wh, c);
}

static void v23_draw_quad_solid(v23_gl_ctx_t *gc, float x0, float y0, float x1, float y1,
                                float x2, float y2, float x3, float y3, v23_rgba_t c) {
    float v[36]; int n = 0;
    v23_push_quad(v, &n, x0, y0, x1, y1, x2, y2, x3, y3, (float)gc->win_w, (float)gc->win_h, c);
    v23_draw_solid_tris(gc, v, n);
}

/* ---- 17 个 GLES2 硬件画笔回调 ---- */
static void v23_op_clear(void *u, float r, float g, float b, float a) {
    (void)u;
    gl_ClearColor(r, g, b, a);
    gl_Clear(GL_COLOR_BUFFER_BIT);
}

static void v23_op_draw_map(void *u, float x, float y, float w, float h, const v23_map_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (!gc->map_has_stream || !gc->prog_map || w <= 1.0f || h <= 1.0f) return;

    GLuint tex = gc->map_zero_copy ? gc->map_tex[gc->map_active_slot] : gc->map_tex_fb;
    if (!tex) return;

    /* 按目标窗宽高比居中裁剪，保持高德车标居中不变形 */
    float dst_ar = w / h;
    float src_ar = (float)gc->map_w / (float)(gc->map_h > 0 ? gc->map_h : 1);
    float u0 = 0.0f, v0 = 0.0f, du = 1.0f, dv = 1.0f;
    if (dst_ar < src_ar) {
        du = dst_ar / src_ar;
        u0 = (1.0f - du) * 0.5f;
    } else if (dst_ar > src_ar) {
        dv = src_ar / dst_ar;
        v0 = (1.0f - dv) * 0.5f;
    }

    float q[24];
    v23_ndc_quad(q, x, y, w, h, (float)gc->win_w, (float)gc->win_h);
    v23_use_prog(gc, gc->prog_map);
    v23_bind_tex0(gc, tex);
    gl_Uniform1i(gc->loc.map_tex, 0);
    v23gl.Uniform2f(gc->loc.map_size, w, h);
    v23gl.Uniform4f(gc->loc.map_crop, u0, v0, du, dv);
    v23gl.Uniform1f(gc->loc.map_rad, o->radius);
    v23gl.Uniform1f(gc->loc.map_circ, o->is_circle ? 1.0f : 0.0f);
    v23gl.Uniform1f(gc->loc.map_alpha, o->alpha);
    v23gl.Uniform1f(gc->loc.map_vig, o->vignette);
    v23gl.Uniform1f(gc->loc.map_bw, o->border_width);
    v23gl.Uniform4f(gc->loc.map_bc,
                    o->border_col.r, o->border_col.g, o->border_col.b, o->border_col.valid ? o->border_col.a : 0.0f);
    /* 零拷贝 EGLImage 通常原生按驱动格式采样；CPU 读入 RGBX8888 (BGRA) 走 glTexSubImage2D 时需互换 R/B */
    v23gl.Uniform1f(gc->loc.map_swap, gc->map_zero_copy ? 0.0f : 1.0f);

    GLint lp = gc->loc.map_pos;
    GLint lu = gc->loc.map_uv;
    gl_EnableVertexAttribArray(lp);
    gl_EnableVertexAttribArray(lu);
    gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q);
    gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q + 2);
    gl_DrawArrays(GL_TRIANGLES, 0, 6);
}

static void v23_op_draw_shader(void *u, const v23_shader_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (!gc->user_shader_ok || !gc->prog_user) return;
    if (o->blend_add && v23gl.BlendFunc) v23gl.BlendFunc(GL_SRC_ALPHA, GL_ONE);

    float q[24];
    v23_ndc_quad(q, 0.0f, 0.0f, (float)gc->win_w, (float)gc->win_h, (float)gc->win_w, (float)gc->win_h);
    v23_use_prog(gc, gc->prog_user);
    if (gc->loc.usr_res   >= 0) v23gl.Uniform2f(gc->loc.usr_res,   (float)gc->win_w, (float)gc->win_h);
    if (gc->loc.usr_time  >= 0) v23gl.Uniform1f(gc->loc.usr_time,  (float)gc->cur_bus.sys_time);
    if (gc->loc.usr_rpm   >= 0) v23gl.Uniform1f(gc->loc.usr_rpm,   (float)(gc->cur_bus.rpm / 7000.0));
    if (gc->loc.usr_spd   >= 0) v23gl.Uniform1f(gc->loc.usr_spd,   (float)(gc->cur_bus.speed / 240.0));
    if (gc->loc.usr_pwr   >= 0) v23gl.Uniform1f(gc->loc.usr_pwr,   (float)(gc->cur_bus.power / 265.0));
    if (gc->loc.usr_ped   >= 0) v23gl.Uniform1f(gc->loc.usr_ped,   (float)(gc->cur_bus.pedal / 100.0));
    if (gc->loc.usr_brk   >= 0) v23gl.Uniform1f(gc->loc.usr_brk,   (float)(gc->cur_bus.brake / 80.0));
    if (gc->loc.usr_g     >= 0) v23gl.Uniform2f(gc->loc.usr_g,     (float)gc->cur_bus.glat, (float)gc->cur_bus.glong);
    if (gc->loc.usr_map   >= 0) v23gl.Uniform1f(gc->loc.usr_map,   gc->cur_bus.sys_map_live ? 1.0f : 0.0f);
    if (gc->loc.usr_night >= 0) v23gl.Uniform1f(gc->loc.usr_night, gc->cur_bus.sys_night ? 1.0f : 0.0f);

    if (gc->loc.usr_pos >= 0) { gl_EnableVertexAttribArray(gc->loc.usr_pos); gl_VertexAttribPointer(gc->loc.usr_pos, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q); }
    if (gc->loc.usr_uv  >= 0) { gl_EnableVertexAttribArray(gc->loc.usr_uv);  gl_VertexAttribPointer(gc->loc.usr_uv,  2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q + 2); }
    gl_DrawArrays(GL_TRIANGLES, 0, 6);

    if (o->blend_add && v23gl.BlendFunc) v23gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void v23_op_rect(void *u, float x, float y, float w, float h, const v23_rect_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (!gc->prog_rrect || w <= 0.5f || h <= 0.5f) return;
    float g = o->glow > 0.0f ? o->glow : 0.0f;
    float q[24];
    v23_ndc_quad(q, x - g, y - g, w + g * 2.0f, h + g * 2.0f, (float)gc->win_w, (float)gc->win_h);
    v23_use_prog(gc, gc->prog_rrect);
    v23gl.Uniform2f(gc->loc.rrect_size, w, h);
    v23gl.Uniform1f(gc->loc.rrect_rad, o->radius);
    v23gl.Uniform1f(gc->loc.rrect_sw, o->stroke.valid ? o->line_width : 0.0f);
    v23gl.Uniform1f(gc->loc.rrect_glow, g);
    v23gl.Uniform1f(gc->loc.rrect_gradh, o->grad_h ? 1.0f : 0.0f);
    v23gl.Uniform4f(gc->loc.rrect_f1, o->fill.r, o->fill.g, o->fill.b, o->fill.valid ? o->fill.a : 0.0f);
    v23_rgba_t f2 = o->fill2.valid ? o->fill2 : o->fill;
    v23gl.Uniform4f(gc->loc.rrect_f2, f2.r, f2.g, f2.b, o->fill.valid ? f2.a : 0.0f);
    v23gl.Uniform4f(gc->loc.rrect_st, o->stroke.r, o->stroke.g, o->stroke.b, o->stroke.valid ? o->stroke.a : 0.0f);
    v23gl.Uniform4f(gc->loc.rrect_gc, o->glow_col.r, o->glow_col.g, o->glow_col.b, o->glow_col.valid ? o->glow_col.a : 0.0f);
    GLint lp = gc->loc.rrect_pos;
    GLint lu = gc->loc.rrect_uv;
    gl_EnableVertexAttribArray(lp);
    gl_EnableVertexAttribArray(lu);
    gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q);
    gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q + 2);
    gl_DrawArrays(GL_TRIANGLES, 0, 6);
}

static void v23_op_circle(void *u, float cx, float cy, float r, const v23_circle_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (!gc->prog_circle || r <= 0.5f) return;
    float sw = o->stroke.valid ? o->line_width : 0.0f;
    float g = o->glow > 0.0f ? o->glow : 0.0f;
    float pad = r + sw + g + 2.0f;
    float q[24];
    v23_ndc_quad(q, cx - pad, cy - pad, pad * 2.0f, pad * 2.0f, (float)gc->win_w, (float)gc->win_h);
    v23_use_prog(gc, gc->prog_circle);
    v23gl.Uniform1f(gc->loc.circ_r, r);
    v23gl.Uniform1f(gc->loc.circ_sw, sw);
    v23gl.Uniform1f(gc->loc.circ_glow, g);
    v23gl.Uniform4f(gc->loc.circ_fill, o->fill.r, o->fill.g, o->fill.b, o->fill.valid ? o->fill.a : 0.0f);
    v23gl.Uniform4f(gc->loc.circ_st, o->stroke.r, o->stroke.g, o->stroke.b, o->stroke.valid ? o->stroke.a : 0.0f);
    v23gl.Uniform4f(gc->loc.circ_gc, o->glow_col.r, o->glow_col.g, o->glow_col.b, o->glow_col.valid ? o->glow_col.a : 0.0f);
    GLint lp = gc->loc.circ_pos;
    GLint lu = gc->loc.circ_uv;
    gl_EnableVertexAttribArray(lp);
    gl_EnableVertexAttribArray(lu);
    gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q);
    gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q + 2);
    gl_DrawArrays(GL_TRIANGLES, 0, 6);
}

static void v23_op_line(void *u, float x1, float y1, float x2, float y2, const v23_line_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    float dx = x2 - x1, dy = y2 - y1;
    float len = nav_sqrtf(dx * dx + dy * dy);
    if (len < 0.2f) return;
    float hw = (o->line_width > 0.5f ? o->line_width : 1.0f) * 0.5f;
    float nx = -dy / len * hw, ny = dx / len * hw;
    if (o->glow > 0.5f) {
        v23_rgba_t gc2 = o->glow_col;
        gc2.a *= 0.25f;
        float gnx = -dy / len * (hw + o->glow * 0.35f), gny = dx / len * (hw + o->glow * 0.35f);
        v23_draw_quad_solid(gc, x1 + gnx, y1 + gny, x2 + gnx, y2 + gny, x2 - gnx, y2 - gny, x1 - gnx, y1 - gny, gc2);
    }
    v23_draw_quad_solid(gc, x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, o->color);
}

static void v23_op_poly(void *u, const float *xy, int npts, const v23_circle_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (npts < 2) return;
    if (o->fill.valid && npts >= 3) {
        float verts[(V23_MAX_POLY_PTS - 2) * 3 * 6];
        int nv = 0;
        float ww = (float)gc->win_w, wh = (float)gc->win_h;
        for (int i = 1; i + 1 < npts; i++) {
            v23_push_v(verts, &nv, xy[0], xy[1], ww, wh, o->fill);
            v23_push_v(verts, &nv, xy[i * 2], xy[i * 2 + 1], ww, wh, o->fill);
            v23_push_v(verts, &nv, xy[(i + 1) * 2], xy[(i + 1) * 2 + 1], ww, wh, o->fill);
        }
        v23_draw_solid_tris(gc, verts, nv);
    }
    if (o->stroke.valid) {
        float hw = (o->line_width > 0.5f ? o->line_width : 1.0f) * 0.5f;
        float ww = (float)gc->win_w, wh = (float)gc->win_h;
        float verts[32 * 6 * 6];
        int nv = 0;
        for (int i = 0; i < npts; i++) {
            int j = (i + 1) % npts;
            float x1 = xy[i * 2], y1 = xy[i * 2 + 1], x2 = xy[j * 2], y2 = xy[j * 2 + 1];
            float dx = x2 - x1, dy = y2 - y1;
            float len = nav_sqrtf(dx * dx + dy * dy);
            if (len < 0.2f) continue;
            float nx = -dy / len * hw, ny = dx / len * hw;
            if (o->glow > 0.5f) {
                v23_rgba_t gc2 = o->glow_col;
                gc2.a *= 0.25f;
                float gnx = -dy / len * (hw + o->glow * 0.35f), gny = dx / len * (hw + o->glow * 0.35f);
                if (nv + 12 > 32 * 6) { v23_draw_solid_tris(gc, verts, nv); nv = 0; }
                v23_push_quad(verts, &nv, x1 + gnx, y1 + gny, x2 + gnx, y2 + gny, x2 - gnx, y2 - gny, x1 - gnx, y1 - gny, ww, wh, gc2);
            }
            if (nv + 6 > 32 * 6) { v23_draw_solid_tris(gc, verts, nv); nv = 0; }
            v23_push_quad(verts, &nv, x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, ww, wh, o->stroke);
        }
        if (nv > 0) v23_draw_solid_tris(gc, verts, nv);
    }
}

static void v23_op_arc(void *u, float cx, float cy, float r, float start_deg, float end_deg, const v23_arc_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    float span = end_deg - start_deg;
    if (nav_fabsf(span) < 0.05f || r < 1.0f) return;
    int segs = (int)(nav_fabsf(span) / 4.5f) + 4;
    if (segs > 96) segs = 96;
    float hw = (o->line_width > 0.5f ? o->line_width : 4.0f) * 0.5f;
    float ww = (float)gc->win_w, wh = (float)gc->win_h;
    const float D2R = 0.0174532925f;

    /* 若带 glow，先绘一层柔和外晕 */
    int passes = (o->glow > 0.5f) ? 2 : 1;
    for (int pass = 0; pass < passes; pass++) {
        float w_use = (passes == 2 && pass == 0) ? (hw + o->glow * 0.45f) : hw;
        v23_rgba_t col = (passes == 2 && pass == 0) ? o->glow_col : o->color;
        if (passes == 2 && pass == 0) col.a *= 0.24f;
        float verts[96 * 6 * 6];
        int nv = 0;
        for (int i = 0; i < segs; i++) {
            float a0 = (start_deg + span * ((float)i / (float)segs)) * D2R;
            float a1 = (start_deg + span * ((float)(i + 1) / (float)segs)) * D2R;
            float c0 = nav_cosf(a0), s0 = nav_sinf(a0);
            float c1 = nav_cosf(a1), s1 = nav_sinf(a1);
            float ri = r - w_use; if (ri < 0.5f) ri = 0.5f;
            float ro = r + w_use;
            v23_push_v(verts, &nv, cx + c0 * ri, cy + s0 * ri, ww, wh, col);
            v23_push_v(verts, &nv, cx + c0 * ro, cy + s0 * ro, ww, wh, col);
            v23_push_v(verts, &nv, cx + c1 * ro, cy + s1 * ro, ww, wh, col);
            v23_push_v(verts, &nv, cx + c0 * ri, cy + s0 * ri, ww, wh, col);
            v23_push_v(verts, &nv, cx + c1 * ro, cy + s1 * ro, ww, wh, col);
            v23_push_v(verts, &nv, cx + c1 * ri, cy + s1 * ri, ww, wh, col);
        }
        v23_draw_solid_tris(gc, verts, nv);
    }
    if (o->cap_round) {
        v23_rgba_t none = { 0, 0, 0, 0, 0 };
        v23_circle_opts_t co = { o->color, none, o->glow_col, 0.0f, 0.0f };
        float a0 = start_deg * D2R, a1 = end_deg * D2R;
        v23_op_circle(u, cx + nav_cosf(a0) * r, cy + nav_sinf(a0) * r, hw, &co);
        v23_op_circle(u, cx + nav_cosf(a1) * r, cy + nav_sinf(a1) * r, hw, &co);
    }
}

static void v23_op_ticks(void *u, float cx, float cy, float r_in, float r_out, float start_deg, float end_deg, int count, const v23_ticks_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    const float D2R = 0.0174532925f;
    float ww = (float)gc->win_w, wh = (float)gc->win_h;
    float verts[32 * 6 * 6];
    int nv = 0;
    for (int i = 0; i <= count; i++) {
        float f = (float)i / (float)count;
        float ang = (start_deg + (end_deg - start_deg) * f) * D2R;
        int is_maj = (i % o->major_every) == 0;
        int is_red = o->has_red && (f >= o->red_from_frac);
        float r0 = is_maj ? (r_in - o->major_len) : r_in;
        float ca = nav_cosf(ang), sa = nav_sinf(ang);
        float x1 = cx + ca * r0, y1 = cy + sa * r0;
        float x2 = cx + ca * r_out, y2 = cy + sa * r_out;
        float lw = is_maj ? o->major_width : o->line_width;
        float hw = (lw > 0.5f ? lw : 1.0f) * 0.5f;
        float nx = -sa * hw, ny = ca * hw;
        v23_rgba_t col = is_red ? o->red_col : o->color;
        if (nv + 6 > 32 * 6) { v23_draw_solid_tris(gc, verts, nv); nv = 0; }
        v23_push_quad(verts, &nv, x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny, ww, wh, col);
    }
    if (nv > 0) v23_draw_solid_tris(gc, verts, nv);
}

static void v23_op_needle(void *u, float cx, float cy, float len, float tail, float angle_deg, const v23_needle_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    float a = angle_deg * 0.0174532925f;
    float ca = nav_cosf(a), sa = nav_sinf(a);
    float px = -sa, py = ca;
    float w = o->width > 1.0f ? o->width : 5.0f;

    float tip_x = cx + ca * len, tip_y = cy + sa * len;
    float m1_x = cx + px * (w * 0.55f), m1_y = cy + py * (w * 0.55f);
    float m2_x = cx - px * (w * 0.55f), m2_y = cy - py * (w * 0.55f);
    float t1_x = cx - ca * tail + px * (w * 0.32f), t1_y = cy - sa * tail + py * (w * 0.32f);
    float t2_x = cx - ca * tail - px * (w * 0.32f), t2_y = cy - sa * tail - py * (w * 0.32f);

    float ww = (float)gc->win_w, wh = (float)gc->win_h;
    float verts[9 * 6]; int nv = 0;
    /* 针尖三角形（色渐变至 tip_col） */
    v23_push_v(verts, &nv, tip_x, tip_y, ww, wh, o->tip_col);
    v23_push_v(verts, &nv, m1_x, m1_y, ww, wh, o->color);
    v23_push_v(verts, &nv, m2_x, m2_y, ww, wh, o->color);
    /* 尾部梯形 */
    v23_push_v(verts, &nv, m1_x, m1_y, ww, wh, o->color);
    v23_push_v(verts, &nv, t1_x, t1_y, ww, wh, o->color);
    v23_push_v(verts, &nv, t2_x, t2_y, ww, wh, o->color);
    v23_push_v(verts, &nv, m1_x, m1_y, ww, wh, o->color);
    v23_push_v(verts, &nv, t2_x, t2_y, ww, wh, o->color);
    v23_push_v(verts, &nv, m2_x, m2_y, ww, wh, o->color);
    v23_draw_solid_tris(gc, verts, nv);

    if (o->hub_radius > 0.5f) {
        v23_rgba_t st = { 1.0f, 1.0f, 1.0f, 0.28f, 1 };
        v23_circle_opts_t co = { o->hub_col, st, st, 2.0f, 0.0f };
        v23_op_circle(u, cx, cy, o->hub_radius, &co);
    }
}

static void v23_op_seg_bar(void *u, float x, float y, float w, float h, float frac, const v23_segbar_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float slant = h * 0.42f, gap = 3.0f;
    float sw = (w - slant - gap * (float)(o->segs - 1)) / (float)o->segs;
    if (sw < 1.0f) sw = 1.0f;
    float lit_pos = frac * (float)o->segs;
    float pulse = o->pulse ? (0.72f + 0.28f * nav_sinf((float)gc->cur_bus.sys_time * 20.0f)) : 1.0f;
    float ww = (float)gc->win_w, wh = (float)gc->win_h;

    v23_rgba_t bg_norm = { 42.0f/255.0f, 56.0f/255.0f, 70.0f/255.0f, 0.62f, 1 };
    v23_rgba_t bg_red  = { 138.0f/255.0f, 32.0f/255.0f, 48.0f/255.0f, 0.75f, 1 };

    float verts[32 * 6 * 6];
    int nv = 0;
    for (int s = 0; s < o->segs; s++) {
        int is_red = (s >= o->segs - o->red_segs);
        int is_warn = (!is_red && o->red_segs >= 3 && s >= o->segs - o->red_segs - 2);
        float fill = lit_pos - (float)s;
        if (fill < 0.0f) fill = 0.0f;
        if (fill > 1.0f) fill = 1.0f;
        float sx = x + (float)s * (sw + gap);
        if (nv + 12 > 32 * 6) { v23_draw_solid_tris(gc, verts, nv); nv = 0; }
        v23_push_quad(verts, &nv, sx + slant, y, sx + slant + sw, y, sx + sw, y + h, sx, y + h, ww, wh, is_red ? bg_red : bg_norm);
        if (fill > 0.02f) {
            v23_rgba_t c = is_red ? o->red_col : (is_warn ? o->warn_col : o->color);
            c.a *= fill * pulse;
            v23_push_quad(verts, &nv, sx + slant, y, sx + slant + sw, y, sx + sw, y + h, sx, y + h, ww, wh, c);
        }
    }
    if (nv > 0) v23_draw_solid_tris(gc, verts, nv);
}

static void v23_op_bi_bar(void *u, float x, float y, float w, float h, float sf, const v23_bibar_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (sf < -1.0f) sf = -1.0f;
    if (sf > 1.0f) sf = 1.0f;
    float mid = x + w * 0.5f, ww = (float)gc->win_w, wh = (float)gc->win_h;
    float verts[4 * 6 * 6];
    int nv = 0;
    v23_rgba_t bg = { 42.0f/255.0f, 56.0f/255.0f, 70.0f/255.0f, 0.65f, 1 };
    v23_push_quad(verts, &nv, x, y + 1, x + w, y + 1, x + w, y + h - 1, x, y + h - 1, ww, wh, bg);
    float cur = mid + sf * (w * 0.5f - 4.0f);
    float lx = cur < mid ? cur : mid, rx = cur > mid ? cur : mid;
    v23_push_quad(verts, &nv, lx, y + 1, rx, y + 1, rx, y + h - 1, lx, y + h - 1, ww, wh, sf >= 0.0f ? o->pos_col : o->neg_col);
    v23_rgba_t mk = { 0.92f, 0.96f, 1.0f, 1.0f, 1 };
    v23_push_quad(verts, &nv, cur - 2, y - 1, cur + 2, y - 1, cur + 2, y + h + 1, cur - 2, y + h + 1, ww, wh, mk);
    v23_rgba_t ct = { 0.86f, 0.91f, 0.96f, 0.85f, 1 };
    v23_push_quad(verts, &nv, mid - 1, y - 2, mid + 1, y - 2, mid + 1, y + h + 2, mid - 1, y + h + 2, ww, wh, ct);
    v23_draw_solid_tris(gc, verts, nv);
}

static void v23_op_range_bar(void *u, float x, float y, float w, float h, float f, const v23_rangebar_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float ww = (float)gc->win_w, wh = (float)gc->win_h;
    float verts[4 * 6 * 6];
    int nv = 0;
    v23_rgba_t bg = { 42.0f/255.0f, 56.0f/255.0f, 70.0f/255.0f, 0.65f, 1 };
    v23_push_quad(verts, &nv, x, y + 1, x + w, y + 1, x + w, y + h - 1, x, y + h - 1, ww, wh, bg);
    if (f > 0.005f) v23_push_quad(verts, &nv, x, y + 1, x + w * f, y + 1, x + w * f, y + h - 1, x, y + h - 1, ww, wh, o->color);
    if (o->tick_lo > 0.0f && o->tick_lo < 1.0f) {
        v23_rgba_t tk = { 0.76f, 0.82f, 0.88f, 0.75f, 1 };
        float tx = x + w * o->tick_lo;
        v23_push_quad(verts, &nv, tx, y - 1, tx + 2, y - 1, tx + 2, y + h + 1, tx, y + h + 1, ww, wh, tk);
    }
    if (o->tick_hi > 0.0f && o->tick_hi < 1.0f) {
        v23_rgba_t tk = { 1.0f, 0.43f, 0.43f, 0.8f, 1 };
        float tx = x + w * o->tick_hi;
        v23_push_quad(verts, &nv, tx, y - 1, tx + 2, y - 1, tx + 2, y + h + 1, tx, y + h + 1, ww, wh, tk);
    }
    v23_draw_solid_tris(gc, verts, nv);
}

static void v23_op_text(void *u, const char *str, float x, float y, const v23_text_opts_t *o) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    if (!str || !str[0] || !gc->prog_text || !gc->atlas.inited) return;
    int px = (int)(o->size + 0.5f);
    if (gc->atlas.count + 48 >= V23_ATLAS_MAX) v23_atlas_reset(&gc->atlas);

    /* 第一遍量总宽并预热字形（O(1) 哈希查找；若触发新字形上传同步更新 cur_tex0） */
    int did_up = 0;
    float total_w = 0.0f;
    const char *p = str;
    uint32_t cp;
    while ((cp = nav_utf8_next(&p)) != 0) {
        const v23_aglyph_t *g = v23_atlas_get(&gc->atlas, cp, px, o->bold, &did_up);
        if (g) total_w += (float)g->adv;
    }
    if (did_up) gc->cur_tex0 = gc->atlas.tex;

    float scale_x = 1.0f;
    if (o->max_width > 4.0f && total_w > o->max_width) scale_x = o->max_width / total_w;
    float draw_w = total_w * scale_x;

    float pen_x = x;
    if (o->align == 1) pen_x -= draw_w * 0.5f;
    else if (o->align == 2) pen_x -= draw_w;

    float base_y = y;
    if (o->baseline == 1) base_y += (float)px * 0.35f;
    else if (o->baseline == 2) base_y += (float)px * 0.85f;

    v23_use_prog(gc, gc->prog_text);
    v23_bind_tex0(gc, gc->atlas.tex);
    gl_Uniform1i(gc->loc.text_tex, 0);
    v23gl.Uniform4f(gc->loc.text_col, o->color.r, o->color.g, o->color.b, o->color.a);
    GLint lp = gc->loc.text_pos;
    GLint lu = gc->loc.text_uv;
    gl_EnableVertexAttribArray(lp);
    gl_EnableVertexAttribArray(lu);

    /* 整串合批绘制（最多 64 字符一次 glDrawArrays，大幅降低 Tegra K1 Draw Call 负载） */
    float batch[64 * 24];
    int nq = 0;
    float ww = (float)gc->win_w, wh = (float)gc->win_h;
    p = str;
    while ((cp = nav_utf8_next(&p)) != 0) {
        const v23_aglyph_t *g = v23_atlas_get(&gc->atlas, cp, px, o->bold, &did_up);
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            if (nq >= 64) {
                if (did_up) { gc->cur_tex0 = gc->atlas.tex; did_up = 0; }
                gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), batch);
                gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), batch + 2);
                gl_DrawArrays(GL_TRIANGLES, 0, nq * 6);
                nq = 0;
            }
            float gx0 = pen_x + (float)g->xoff * scale_x;
            float gy0 = base_y + (float)g->yoff;
            float gx1 = gx0 + (float)g->w * scale_x;
            float gy1 = gy0 + (float)g->h;
            float x0 = v23_px_nx(gx0, ww), x1 = v23_px_nx(gx1, ww);
            float y0 = v23_px_ny(gy0, wh), y1 = v23_px_ny(gy1, wh);
            float *q = batch + nq * 24;
            q[0]  = x0; q[1]  = y0; q[2]  = g->u0; q[3]  = g->v0;
            q[4]  = x1; q[5]  = y0; q[6]  = g->u1; q[7]  = g->v0;
            q[8]  = x0; q[9]  = y1; q[10] = g->u0; q[11] = g->v1;
            q[12] = x1; q[13] = y0; q[14] = g->u1; q[15] = g->v0;
            q[16] = x1; q[17] = y1; q[18] = g->u1; q[19] = g->v1;
            q[20] = x0; q[21] = y1; q[22] = g->u0; q[23] = g->v1;
            nq++;
        }
        pen_x += (float)g->adv * scale_x;
    }
    if (nq > 0) {
        if (did_up) gc->cur_tex0 = gc->atlas.tex;
        gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), batch);
        gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), batch + 2);
        gl_DrawArrays(GL_TRIANGLES, 0, nq * 6);
    }
}

static void v23_draw_bgra_quad(v23_gl_ctx_t *gc, GLuint tex, float x, float y, float w, float h, float alpha) {
    if (!tex || !gc->prog_bgra || w <= 1.0f || h <= 1.0f) return;
    float q[24];
    v23_ndc_quad(q, x, y, w, h, (float)gc->win_w, (float)gc->win_h);
    v23_use_prog(gc, gc->prog_bgra);
    v23_bind_tex0(gc, tex);
    gl_Uniform1i(gc->loc.bgra_tex, 0);
    v23gl.Uniform1f(gc->loc.bgra_alpha, alpha);
    GLint lp = gc->loc.bgra_pos;
    GLint lu = gc->loc.bgra_uv;
    gl_EnableVertexAttribArray(lp);
    gl_EnableVertexAttribArray(lu);
    gl_VertexAttribPointer(lp, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q);
    gl_VertexAttribPointer(lu, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), q + 2);
    gl_DrawArrays(GL_TRIANGLES, 0, 6);
}

static void v23_op_dash_box(void *u, float x, float y, float w, int side) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    int b = (side != 0) ? 1 : 0;
    if (!g_dash_have || !gc->dash_en[b] || gc->dash_w[b] <= 0 || gc->dash_h[b] <= 0) return;
    float dh = (float)gc->dash_h[b] * (w / (float)gc->dash_w[b]);
    v23_draw_bgra_quad(gc, gc->dash_tex[b], x, y, w, dh, 1.0f);
}

static void v23_op_nav_card(void *u, float x, float y, float w, float h, int alpha) {
    v23_gl_ctx_t *gc = (v23_gl_ctx_t *)u;
    (void)alpha;
    if (!g_nav_active || gc->nav_w <= 0 || gc->nav_h <= 0) return;
    v23_draw_bgra_quad(gc, gc->nav_tex, x, y, w, h, 1.0f);
}

static void v23_op_turn_arrow(void *u, float x, float y, float sz, const char *tp, v23_rgba_t col) {
    if (!tp) tp = "straight";
    float cx = x + sz * 0.5f, cy = y + sz * 0.5f, s = sz / 64.0f;
    v23_line_opts_t lo = { col, col, 7.0f * s, 0.0f, 1 };
    v23_rgba_t none = {0,0,0,0,0};
    v23_circle_opts_t co = { col, none, col, 0.0f, 0.0f };
    if (strcmp(tp, "right") == 0) {
        v23_op_line(u, cx - 10 * s, cy + 22 * s, cx - 10 * s, cy - 4 * s, &lo);
        v23_op_line(u, cx - 10 * s, cy - 4 * s, cx + 10 * s, cy - 4 * s, &lo);
        float tri[6] = { cx + 24*s, cy - 4*s, cx + 8*s, cy - 16*s, cx + 8*s, cy + 8*s };
        v23_op_poly(u, tri, 3, &co);
    } else if (strcmp(tp, "left") == 0) {
        v23_op_line(u, cx + 10 * s, cy + 22 * s, cx + 10 * s, cy - 4 * s, &lo);
        v23_op_line(u, cx + 10 * s, cy - 4 * s, cx - 10 * s, cy - 4 * s, &lo);
        float tri[6] = { cx - 24*s, cy - 4*s, cx - 8*s, cy - 16*s, cx - 8*s, cy + 8*s };
        v23_op_poly(u, tri, 3, &co);
    } else if (strcmp(tp, "slight_right") == 0) {
        v23_op_line(u, cx - 8 * s, cy + 22 * s, cx - 8 * s, cy + 2 * s, &lo);
        v23_op_line(u, cx - 8 * s, cy + 2 * s, cx + 8 * s, cy - 14 * s, &lo);
        float tri[6] = { cx + 18*s, cy - 24*s, cx + 1*s, cy - 19*s, cx + 13*s, cy - 7*s };
        v23_op_poly(u, tri, 3, &co);
    } else if (strcmp(tp, "slight_left") == 0) {
        v23_op_line(u, cx + 8 * s, cy + 22 * s, cx + 8 * s, cy + 2 * s, &lo);
        v23_op_line(u, cx + 8 * s, cy + 2 * s, cx - 8 * s, cy - 14 * s, &lo);
        float tri[6] = { cx - 18*s, cy - 24*s, cx - 1*s, cy - 19*s, cx - 13*s, cy - 7*s };
        v23_op_poly(u, tri, 3, &co);
    } else if (strcmp(tp, "uturn") == 0) {
        /* 中国右行默认左掉头：右侧直行上升 -> 顶部半圆弧 -> 左侧下行箭头 */
        v23_arc_opts_t ao = { col, col, 7.0f * s, 0.0f, 1 };
        v23_op_line(u, cx + 12 * s, cy + 22 * s, cx + 12 * s, cy - 8 * s, &lo);
        v23_op_arc(u, cx, cy - 8 * s, 12.0f * s, 180.0f, 360.0f, &ao);
        v23_op_line(u, cx - 12 * s, cy - 8 * s, cx - 12 * s, cy + 8 * s, &lo);
        float tri[6] = { cx - 12*s, cy + 22*s, cx - 22*s, cy + 6*s, cx - 2*s, cy + 6*s };
        v23_op_poly(u, tri, 3, &co);
    } else if (strcmp(tp, "roundabout") == 0) {
        /* 环岛：淡色完整圆环 + 底部入口 + 右行逆时针高亮弧 + 右上出口箭头 */
        v23_rgba_t dim = col;
        dim.a *= 0.35f;
        v23_arc_opts_t ao_dim = { dim, dim, 5.5f * s, 0.0f, 0 };
        v23_arc_opts_t ao_hi  = { col, col, 6.5f * s, 0.0f, 1 };
        v23_line_opts_t lo_rb = { col, col, 6.5f * s, 0.0f, 1 };
        float rcy = cy - 2.0f * s, rr = 13.0f * s;
        v23_op_arc(u, cx, rcy, rr, 0.0f, 360.0f, &ao_dim);
        v23_op_line(u, cx, cy + 24 * s, cx, rcy + rr, &lo_rb);
        v23_op_arc(u, cx, rcy, rr, -30.0f, 90.0f, &ao_hi);
        v23_op_line(u, cx + 11.3f * s, rcy - 6.5f * s, cx + 18.2f * s, rcy - 10.5f * s, &lo_rb);
        float tri[6] = { cx + 25.0f*s, rcy - 14.5f*s, cx + 13.5f*s, rcy - 14.7f*s, cx + 19.5f*s, rcy - 4.3f*s };
        v23_op_poly(u, tri, 3, &co);
    } else {
        v23_op_line(u, cx, cy + 22 * s, cx, cy - 10 * s, &lo);
        float tri[6] = { cx, cy - 24*s, cx + 12*s, cy - 6*s, cx - 12*s, cy - 6*s };
        v23_op_poly(u, tri, 3, &co);
    }
}

/* ---- 顶部安全提示横幅（报错时明确展示出错行号，0 黑屏） ---- */
static void v23_set_banner(v23_gl_ctx_t *gc, const char *msg, int level, uint64_t dur_sec) {
    snprintf(gc->banner_text, sizeof(gc->banner_text), "%s", msg ? msg : "");
    gc->banner_level = level;
    gc->banner_until_us = v23_now_us() + dur_sec * 1000000ULL;
}

static void v23_draw_banner(v23_gl_ctx_t *gc, uint64_t now_u) {
    if (!gc->banner_text[0] || now_u > gc->banner_until_us) return;
    v23_rgba_t bg_err  = { 185.0f/255.0f, 28.0f/255.0f, 28.0f/255.0f, 0.94f, 1 };
    v23_rgba_t bg_warn = { 180.0f/255.0f, 83.0f/255.0f, 9.0f/255.0f, 0.94f, 1 };
    v23_rgba_t bg_info = { 14.0f/255.0f, 116.0f/255.0f, 144.0f/255.0f, 0.92f, 1 };
    v23_rgba_t bd = { 1.0f, 1.0f, 1.0f, 0.65f, 1 };
    v23_rect_opts_t ro;
    memset(&ro, 0, sizeof(ro));
    ro.fill = (gc->banner_level == 2) ? bg_err : (gc->banner_level == 1 ? bg_warn : bg_info);
    ro.stroke = bd; ro.line_width = 2.0f; ro.radius = 14.0f;
    /* 避开原车顶部信息栏/路名区与车道指引扩展栏（y = 0..190），固定画在全天候安全区顶边 y = 200 */
    v23_op_rect(gc, 320.0f, 200.0f, 1280.0f, 52.0f, &ro);
    v23_text_opts_t to;
    memset(&to, 0, sizeof(to));
    to.size = 22.0f; to.bold = 1; to.align = 1; to.baseline = 1; to.max_width = 1240.0f;
    to.color = (v23_rgba_t){ 1.0f, 1.0f, 1.0f, 1.0f, 1 };
    v23_op_text(gc, gc->banner_text, 960.0f, 226.0f, &to);
}

/* ---- 编译用户可选 GLSL 光效（仅影响 drawShader，失败不拖垮主皮肤） ---- */
static void v23_compile_user_shader(v23_gl_ctx_t *gc, const v23_theme_meta_t *meta) {
    if (gc->prog_user && v23gl.DeleteProgram) {
        if (gc->cur_prog == gc->prog_user) gc->cur_prog = 0;
        v23gl.DeleteProgram(gc->prog_user);
        gc->prog_user = 0;
    }
    gc->user_shader_ok = 0;
    gc->shader_err[0] = '\0';
    v23_cache_user_locs(gc);
    if (!meta || !meta->has_shader || !meta->shader[0]) return;
    if (strlen(meta->shader) > V23_MAX_SHADER_BYTES) {
        snprintf(gc->shader_err, sizeof(gc->shader_err), "Shader 超 8KB 上限");
        return;
    }
    char err[256];
    GLuint p = v23_compile_prog(v23_vs_quad, meta->shader, err, sizeof(err));
    if (!p) {
        snprintf(gc->shader_err, sizeof(gc->shader_err), "GLSL 光效编译失败: %.180s", err[0] ? err : "unknown");
        v23_set_banner(gc, "光效 Shader 编译失败，已自动仅关闭光效层（主皮肤 60fps 正常）", 1, 6);
        return;
    }
    gc->prog_user = p;
    v23_cache_user_locs(gc);
    gc->user_shader_ok = 1;
}

/* ---- 读取 /tmp/theme_status.json 原地写出（严禁 /tmp rename） ---- */
static void v23_write_status_json(const v23_sandbox_t *cur_sb, const v23_gl_ctx_t *gc,
                                  const char *source, int fallback, const char *err_msg,
                                  int fps, int map_fps) {
    char id_e[128], name_e[192], err_e[512], sh_e[384], src_e[64];
    int idx = fallback ? -1 : ((cur_sb && cur_sb->entry_count > 0) ? cur_sb->active_idx : 0);
    const char *tid = (!fallback && cur_sb && cur_sb->entry_count > 0) ? cur_sb->metas[idx].id : "default_classic";
    const char *tnm = (!fallback && cur_sb && cur_sb->entry_count > 0) ? cur_sb->metas[idx].name : "内置默认 60fps 经典版";
    v23_json_escape(tid, id_e, sizeof(id_e));
    v23_json_escape(tnm, name_e, sizeof(name_e));
    v23_json_escape(err_msg ? err_msg : "", err_e, sizeof(err_e));
    v23_json_escape(gc->shader_err, sh_e, sizeof(sh_e));
    v23_json_escape(source ? source : "builtin", src_e, sizeof(src_e));

    char buf[4096];
    int o = snprintf(buf, sizeof(buf),
        "{\"ok\":%s,\"mode\":\"v23_gles60\",\"engine_ver\":\"v23.6\",\"map_slots\":%d,\"win_bufs\":%d,"
        "\"source\":\"%s\",\"theme_id\":\"%s\",\"theme_name\":\"%s\","
        "\"active_idx\":%d,\"theme_count\":%d,\"compile_ms\":%.2f,\"fps\":%d,\"map_fps\":%d,"
        "\"map_zero_copy\":%s,\"night\":%s,\"fallback\":%s,\"error\":%s%s%s,\"shader_error\":%s%s%s,\"themes\":[",
        (!fallback && (!err_msg || !err_msg[0])) ? "true" : "false",
        V23_MAP_SLOTS, gc->win_bufs,
        src_e, id_e, name_e, idx, cur_sb ? cur_sb->entry_count : 1,
        cur_sb ? cur_sb->last_compile_ms : 0.0, fps, map_fps,
        gc->map_zero_copy ? "true" : "false",
        gc->cur_bus.sys_night ? "true" : "false",
        fallback ? "true" : "false",
        (err_msg && err_msg[0]) ? "\"" : "", (err_msg && err_msg[0]) ? err_e : "null", (err_msg && err_msg[0]) ? "\"" : "",
        gc->shader_err[0] ? "\"" : "", gc->shader_err[0] ? sh_e : "null", gc->shader_err[0] ? "\"" : "");
    if (cur_sb) {
        for (int i = 0; i < cur_sb->entry_count && o < (int)sizeof(buf) - 180; i++) {
            char ei[96], en[144];
            v23_json_escape(cur_sb->metas[i].id, ei, sizeof(ei));
            v23_json_escape(cur_sb->metas[i].name, en, sizeof(en));
            o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s{\"id\":\"%s\",\"name\":\"%s\"}",
                          i > 0 ? "," : "", ei, en);
        }
    }
    o += snprintf(buf + o, sizeof(buf) - (size_t)o, "]}\n");
    int fd = open("/tmp/theme_status.json", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        (void)write(fd, buf, (size_t)o);
        close(fd);
    }
}

/* ---- 读取外部皮肤文件（最大 128 KB） ---- */
static char *v23_read_theme_file(const char *path, size_t *len_out) {
    if (len_out) *len_out = 0;
    struct stat st;
    if (stat(path, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > V23_MAX_THEME_BYTES) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = (char *)malloc((size_t)st.st_size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    buf[n] = '\0';
    if (len_out) *len_out = n;
    return buf;
}

/* ---- 从 dash.c + navbox.c 刷新 60fps 车辆与导航总线 ---- */
static void v23_update_telemetry(v23_bus_t *bus, float dt, int map_live, int auto_night, int fps, int map_fps, double sim_t) {
    static uint64_t last_poll_ms = 0;
    static dash_vals_t raw_v;
    static int raw_inited = 0;
    static double rpm_peak = 0.0, rpm_hold = 0.0;
    static double sm_rpm = 0, sm_spd = 0, sm_rspd = 0, sm_pwr = 0, sm_tq = 0, sm_ped = 0, sm_brk = 0, sm_str = 0, sm_yaw = 0;
    static double sm_g = 0, sm_glong = 0, sm_glat = 0, sm_pitch = 0, sm_roll = 0, sm_oil = 0, sm_gb = 0, sm_alt = 0;

    uint64_t now_ms = v23_now_us() / 1000ULL;
    if (!raw_inited || now_ms - last_poll_ms >= 33ULL) {
        last_poll_ms = now_ms;
        raw_inited = 1;
        FILE *sf = fopen(DASH_STATE, "r");
        if (sf) {
            char l[2048];
            if (fgets(l, sizeof(l), sf) && strchr(l, '\n')) dash_parse(l, &raw_v);
            fclose(sf);
        }
        /* 解析导航快照供 bus.nav.* 使用 */
        FILE *nf = fopen(NAV_STATE_PATH, "r");
        if (nf) {
            char nb[8192];
            size_t nn = fread(nb, 1, sizeof(nb) - 1, nf);
            fclose(nf);
            nb[nn] = '\0';
            if (nn > 0 && strstr(nb, "current_slot:")) {
                nav_state_t ns;
                nav_parse_state(nb, &ns);
                bus->nav_live = ns.live ? 1 : 0;
                bus->nav_dist_m = ns.dist_to_maneuver >= 0 ? ns.dist_to_maneuver : 0;
                bus->nav_rem_dist_km = ns.dist_remaining >= 0 ? (double)ns.dist_remaining / 1000.0 : 0.0;
                bus->nav_rem_min = v23_calc_rem_min(ns.time_remaining);
                snprintf(bus->nav_road, sizeof(bus->nav_road), "%s", ns.s0_road[0] ? ns.s0_road : ns.cur_road);
                snprintf(bus->nav_next_road, sizeof(bus->nav_next_road), "%s", ns.s1_road);
                snprintf(bus->nav_action, sizeof(bus->nav_action), "%s", ns.s0_desc);
                snprintf(bus->nav_turn_type, sizeof(bus->nav_turn_type), "%s",
                         v23_map_turn_type(ns.s0_type, ns.s0_junction, ns.s0_driving_side));
                v23_format_eta(ns.eta, ns.time_remaining, g_nav_conf.tz_offset,
                               time(NULL), bus->nav_eta, sizeof(bus->nav_eta));
            }
        }
    }

    int dummy = 0, p_has = 0, tq_has = 0, w_has = 0;
    double r_rpm  = v23_sanitize_num((double)raw_v.rpm, &dummy);
    double r_spd  = v23_sanitize_num((double)raw_v.spd, &dummy);
    double r_rspd = v23_sanitize_num((double)raw_v.rspd, &dummy);
    if (DASH_NAN(raw_v.rspd)) r_rspd = r_spd;
    double r_pwr  = v23_sanitize_num((double)raw_v.p, &p_has);
    double r_tq   = v23_sanitize_num((double)raw_v.tq, &tq_has);
    bus->ready_power  = (p_has  && raw_v.pok  != 0) ? 1 : 0;
    bus->ready_torque = (tq_has && raw_v.tqok != 0) ? 1 : 0;
    if (!bus->ready_power)  r_pwr = 0.0;
    if (!bus->ready_torque) r_tq  = 0.0;
    double r_ped = v23_sanitize_num((double)raw_v.ped, &dummy);
    double r_brk = v23_sanitize_num((double)raw_v.brk, &dummy);
    double r_str = v23_sanitize_num((double)raw_v.str, &dummy);
    double r_yaw = v23_sanitize_num((double)raw_v.yaw, &dummy);
    double r_gx  = v23_sanitize_num((double)raw_v.gx, &dummy);
    double r_gy  = v23_sanitize_num(g_dash_conf.lat_flip ? -(double)raw_v.gy : (double)raw_v.gy, &dummy);
    double r_g   = nav_sqrt(r_gx * r_gx + r_gy * r_gy);
    if (r_gx < -0.02) r_g = -r_g;
    double r_oil = v23_sanitize_num((double)raw_v.oil, &bus->ready_oil);
    double r_gb  = v23_sanitize_num((double)raw_v.gbt, &bus->ready_gearbox);
    double r_alt = v23_sanitize_num((double)raw_v.alt, &bus->ready_alt);
    double r_cab = v23_sanitize_num((double)raw_v.cab, &bus->ready_cabin);
    double r_tpfl = v23_sanitize_num((double)raw_v.tpfl, &dummy);
    double r_tpfr = v23_sanitize_num((double)raw_v.tpfr, &dummy);
    double r_tprl = v23_sanitize_num((double)raw_v.tprl, &dummy);
    double r_tprr = v23_sanitize_num((double)raw_v.tprr, &dummy);
    int w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    double r_wfl = v23_sanitize_num((double)raw_v.wfl, &w0);
    double r_wfr = v23_sanitize_num((double)raw_v.wfr, &w1);
    double r_wrl = v23_sanitize_num((double)raw_v.wrl, &w2);
    double r_wrr = v23_sanitize_num((double)raw_v.wrr, &w3);
    w_has = (w0 || w1 || w2 || w3) ? 1 : 0;
    double r_pit = v23_sanitize_num((double)raw_v.pitch, &dummy);
    double r_rol = v23_sanitize_num((double)raw_v.roll, &dummy);

    double a = 1.0 - (double)v23_exp_negf(15.0f * dt);
    sm_rpm  += (r_rpm  - sm_rpm)  * a;
    sm_spd  += (r_spd  - sm_spd)  * a;
    sm_rspd += (r_rspd - sm_rspd) * a;
    sm_pwr  += (r_pwr  - sm_pwr)  * a;
    sm_tq   += (r_tq   - sm_tq)   * a;
    sm_ped  += (r_ped  - sm_ped)  * a;
    sm_brk  += (r_brk  - sm_brk)  * a;
    sm_str  += (r_str  - sm_str)  * a;
    sm_yaw  += (r_yaw  - sm_yaw)  * a;
    sm_g    += (r_g    - sm_g)    * a;
    sm_glong += (r_gx - sm_glong) * a;
    sm_glat  += (r_gy - sm_glat)  * a;
    sm_pitch += (r_pit - sm_pitch) * a;
    sm_roll  += (r_rol - sm_roll)  * a;
    sm_oil  += (r_oil  - sm_oil)  * a;
    sm_gb   += (r_gb   - sm_gb)   * a;
    sm_alt  += (r_alt  - sm_alt)  * a;

    if (sm_rpm >= rpm_peak) { rpm_peak = sm_rpm; rpm_hold = 0.45; }
    else if (rpm_hold > 0.0) { rpm_hold -= dt; }
    else { rpm_peak += (sm_rpm - rpm_peak) * (dt * 4.5 < 1.0 ? dt * 4.5 : 1.0); }

    bus->rpm = sm_rpm; bus->rpm_raw = r_rpm; bus->rpm_peak = rpm_peak;
    bus->speed = sm_spd; bus->speed_raw = r_spd;
    bus->real_speed = sm_rspd; bus->real_speed_raw = r_rspd;
    bus->power = sm_pwr; bus->torque = sm_tq; bus->pedal = sm_ped; bus->brake = sm_brk; bus->steer = sm_str; bus->yaw = sm_yaw;
    bus->g = sm_g; bus->glong = sm_glong; bus->glat = sm_glat;
    bus->oil = sm_oil; bus->gearbox = sm_gb; bus->alt = sm_alt; bus->cabin = r_cab;
    bus->coolant = v23_sanitize_num((double)raw_v.cool, &dummy);
    bus->outside = v23_sanitize_num((double)raw_v.out, &dummy);
    bus->cons    = v23_sanitize_num((double)raw_v.cv, &dummy);
    bus->fuel    = v23_calc_fuel(raw_v.tank, g_dash_conf.tank_l, &dummy);
    bus->range   = v23_sanitize_num((double)raw_v.rng, &dummy);
    bus->pitch = sm_pitch; bus->roll = sm_roll;
    bus->tpfl = r_tpfl; bus->tpfr = r_tpfr; bus->tprl = r_tprl; bus->tprr = r_tprr;
    bus->wfl = r_wfl; bus->wfr = r_wfr; bus->wrl = r_wrl; bus->wrr = r_wrr;
    bus->ready_wheels = w_has;
    bus->cond_flags = (raw_v.cf >= 0) ? raw_v.cf : 0;
    v23_calc_tires((double)raw_v.tpfl, (double)raw_v.tpfr, (double)raw_v.tprl, (double)raw_v.tprr,
                   &bus->ready_tires, bus->tires, sizeof(bus->tires));
    bus->t100 = v23_sanitize_num((double)raw_v.t1, &bus->ready_t100);
    bus->t100_running = (!DASH_NAN(raw_v.t1r) && raw_v.t1r > 0.0f) ? 1 : 0;
    bus->pmax = bus->ready_power ? v23_sanitize_num((double)raw_v.pmx, &dummy) : 0.0;
    bus->tqmax = bus->ready_torque ? v23_sanitize_num((double)raw_v.tqx, &dummy) : 0.0;
    bus->gmax = v23_sanitize_num((double)raw_v.gmx, &dummy);
    v23_fill_board_computer(bus, raw_v.avg, raw_v.dist, raw_v.bspd, raw_v.btime,
                            raw_v.odo, g_dash_conf.trip_base);
    bus->gear_num = (raw_v.gc >= 1 && raw_v.gc <= 10) ? raw_v.gc : 0;
    v23_format_gear(raw_v.gm, bus->gear_num, bus->gear, sizeof(bus->gear));

    bus->sys_map_live = map_live;
    bus->sys_night = auto_night;
    bus->sys_acc = (raw_v.acc == 1) ? 1 : 0;
    bus->sys_time = sim_t;
    bus->sys_dt = dt;
    bus->sys_fps = fps;
    bus->sys_map_fps = map_fps;

    bus->conf_show = g_box[0].conf.enabled ? 1 : 0;
    bus->conf_r_show = g_box[1].conf.enabled ? 1 : 0;
    bus->conf_bars = g_dash_conf.bars;
    bus->conf_alpha = g_dash_conf.alpha;
    bus->conf_nav_box = g_nav_conf.enabled;
    bus->conf_tank_l = g_dash_conf.tank_l;
    snprintf(bus->conf_nav_side, sizeof(bus->conf_nav_side), "%s", g_nav_conf.side == 0 ? "left" : "right");
    snprintf(bus->conf_accent, sizeof(bus->conf_accent), "%s", g_dash_conf.accent[0] ? g_dash_conf.accent : "#22d3ee");
}

/* ---- 统一 60fps GLES2 + QuickJS 主循环（v23.4：with_carplay_map=1 与 =0 一律自建独立 own_ctx + 4 缓冲 own_win） ---- */
static int v23_run_gl_loop(int with_carplay_map) {
    screen_context_t own_ctx = NULL;
    screen_window_t  own_win = NULL;
    screen_window_t  target_win = NULL;
    int ww = V23_CANVAS_W, wh = V23_CANVAS_H;
    int alloc_win_bufs = 0;

    if (with_carplay_map) {
        /* 屏蔽 GL 子线程的 SIGUSR1/SIGUSR2，确保 clusterd 信号仅由解码主线程接收，绝不打断 libscreen/libEGL 阻塞调用 */
        sigset_t gl_sig_mask;
        sigemptyset(&gl_sig_mask);
        sigaddset(&gl_sig_mask, SIGUSR1);
        sigaddset(&gl_sig_mask, SIGUSR2);
        pthread_sigmask(SIG_BLOCK, &gl_sig_mask, NULL);
        ww = (g_post_w > 0) ? g_post_w : V23_CANVAS_W;
        wh = (g_post_h > 0) ? g_post_h : V23_CANVAS_H;
    } else {
        v23_diag_reset();
        signal(SIGTERM, v23_on_sig);
        signal(SIGINT,  v23_on_sig);
        ww = DISP_W;
        wh = DISP_H;
    }

    /* v23.4 核心隔离：无论是否带 CarPlay 地图流，GL 渲染线程一律创建专属 screen_context_t 与独立窗口，
     * 彻底杜绝 eglSwapBuffers 与解码线程 screen_blit 并发重入同一个 g_dec.scr_ctx 命令队列。 */
    if (screen_create_context(&own_ctx, SCREEN_APPLICATION_CONTEXT) != 0) {
        v23_diag("v23: screen_create_context(own_ctx) failed errno=%d", errno);
        return 10;
    }
    {
        typedef int (*fn_cwt_t)(screen_window_t *, screen_context_t, int);
        fn_cwt_t cwt = (fn_cwt_t)dlsym(RTLD_DEFAULT, "screen_create_window_type");
        int wrc = cwt ? cwt(&own_win, own_ctx, 0) : screen_create_window(&own_win, own_ctx);
        if (wrc != 0) {
            v23_diag("v23: screen_create_window(own_win) failed rc=%d errno=%d", wrc, errno);
            screen_destroy_context(own_ctx);
            return 11;
        }
    }
    {
        const char *did = cl_disp_id();
        int sz[2] = { ww, wh };
        int fmt = SCREEN_FORMAT_RGBX8888;
        int usage = SCREEN_USAGE_OPENGL_ES2;
        int transp = SCREEN_TRANSPARENCY_NONE;
        int vis = 1;
        int swap_iv = 1;
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_SIZE, sz);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_BUFFER_SIZE, sz);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_FORMAT, &fmt);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_USAGE, &usage);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_TRANSPARENCY, &transp);
        screen_set_window_property_cv(own_win, SCREEN_PROPERTY_ID_STRING, (int)strlen(did), did);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_VISIBLE, &vis);
        screen_set_window_property_iv(own_win, SCREEN_PROPERTY_SWAP_INTERVAL, &swap_iv);
        screen_flush_context(own_ctx, 0);

        /* 优先分配 4 个窗口渲染缓冲（V23_WIN_BUFFERS=4），避免双缓冲下 displaymanager 异步扫描 LVDS2 时被 glClear 追尾产生白横条纹 */
        for (int want_nb = V23_WIN_BUFFERS; want_nb >= 2; want_nb--) {
            int brc;
            do { brc = screen_create_window_buffers(own_win, want_nb); } while (brc < 0 && errno == EINTR);
            if (brc == 0) { alloc_win_bufs = want_nb; break; }
        }
        if (alloc_win_bufs < 2) {
            v23_diag("v23: screen_create_window_buffers failed errno=%d", errno);
            screen_destroy_window(own_win);
            screen_destroy_context(own_ctx);
            return 12;
        }
    }
    target_win = own_win;

    v23_diag("[1] eglInit start own_ctx=%p own_win=%p %dx%d win_bufs=%d map=%d",
             (void *)own_ctx, (void *)target_win, ww, wh, alloc_win_bufs, with_carplay_map);
    EGLDisplay edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (edpy == EGL_NO_DISPLAY || !eglInitialize(edpy, NULL, NULL)) {
        v23_diag("[1] eglInitialize failed err=0x%x", eglGetError());
        screen_destroy_window(own_win); screen_destroy_context(own_ctx);
        return 13;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig ecfg; EGLint ncfg = 0;
    if (!eglChooseConfig(edpy, cfg_attr, &ecfg, 1, &ncfg) || ncfg < 1) {
        v23_diag("[2] eglChooseConfig failed ncfg=%d err=0x%x", (int)ncfg, eglGetError());
        eglTerminate(edpy); screen_destroy_window(own_win); screen_destroy_context(own_ctx);
        return 14;
    }
    v23_diag("[2] eglChooseConfig ok ncfg=%d", (int)ncfg);
    EGLSurface esurf = eglCreateWindowSurface(edpy, ecfg, (EGLNativeWindowType)target_win, NULL);
    if (esurf == EGL_NO_SURFACE) {
        v23_diag("[3] eglCreateWindowSurface failed err=0x%x", eglGetError());
        eglTerminate(edpy); screen_destroy_window(own_win); screen_destroy_context(own_ctx);
        return 15;
    }
    v23_diag("[3] eglCreateWindowSurface ok");
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ectx = eglCreateContext(edpy, ecfg, EGL_NO_CONTEXT, ctx_attr);
    if (ectx == EGL_NO_CONTEXT || !eglMakeCurrent(edpy, esurf, esurf, ectx)) {
        v23_diag("[4] eglMakeCurrent failed ctx=%p err=0x%x", (void *)ectx, eglGetError());
        if (ectx != EGL_NO_CONTEXT) eglDestroyContext(edpy, ectx);
        eglDestroySurface(edpy, esurf); eglTerminate(edpy);
        screen_destroy_window(own_win); screen_destroy_context(own_ctx);
        return 16;
    }
    eglSwapInterval(edpy, 1); /* 锁 60fps 垂直同步 */
    while (eglGetError() != EGL_SUCCESS) {}
    v23_diag("[4] eglMakeCurrent + swapInterval=1 ok");

    load_gl();
    if (v23_load_extra_gl() != 0) {
        v23_diag("[5] missing GLES2 symbols (CreateShader=%p DrawArrays=%p U1f=%p U2f=%p U4f=%p)",
                 (void *)gl_CreateShader, (void *)gl_DrawArrays,
                 (void *)v23gl.Uniform1f, (void *)v23gl.Uniform2f, (void *)v23gl.Uniform4f);
        return 18;
    }
    v23_diag("[5] load_gl + extra_gl ok");

    /* 堆分配渲染上下文与双 QuickJS 沙盒（合计 ~150 KB），严禁压在 QNX 子线程栈上 */
    v23_gl_ctx_t  *gc      = (v23_gl_ctx_t *)calloc(1, sizeof(v23_gl_ctx_t));
    v23_sandbox_t *fb_sb   = (v23_sandbox_t *)calloc(1, sizeof(v23_sandbox_t));
    v23_sandbox_t *user_sb = (v23_sandbox_t *)calloc(1, sizeof(v23_sandbox_t));
    if (!gc || !fb_sb || !user_sb) {
        v23_diag("[5] heap alloc failed");
        free(user_sb); free(fb_sb); free(gc);
        return 17;
    }
    gc->edpy = edpy;
    gc->win_w = ww;
    gc->win_h = wh;
    gc->win_bufs = alloc_win_bufs;
    gc->map_has_stream = with_carplay_map;
    gc->map_w = with_carplay_map ? g_v23_map.width : ww;
    gc->map_h = with_carplay_map ? g_v23_map.height : wh;
    /* 初始绑定在显槽 front_idx (2)，绝不默认落在解码线程首帧正在写入的 free_idx (0) */
    gc->map_active_slot = g_v23_map.front_idx;
    gc->map_ever_flipped = 0;

    char sh_err[256];
    gc->prog_solid  = v23_compile_prog(v23_vs_solid, v23_fs_solid, sh_err, sizeof(sh_err));
    gc->prog_rrect  = v23_compile_prog(v23_vs_quad,  v23_fs_rrect, sh_err, sizeof(sh_err));
    gc->prog_circle = v23_compile_prog(v23_vs_quad,  v23_fs_circle, sh_err, sizeof(sh_err));
    gc->prog_map    = v23_compile_prog(v23_vs_quad,  v23_fs_map, sh_err, sizeof(sh_err));
    gc->prog_bgra   = v23_compile_prog(v23_vs_quad,  v23_fs_bgra, sh_err, sizeof(sh_err));
    gc->prog_text   = v23_compile_prog(v23_vs_quad,  v23_fs_alpha_text, sh_err, sizeof(sh_err));
    if (!gc->prog_solid || !gc->prog_rrect || !gc->prog_map || !gc->prog_bgra || !gc->prog_text) {
        v23_diag("[6] core shader compile failed: %s", sh_err);
        free(user_sb); free(fb_sb); free(gc);
        return 19;
    }
    v23_cache_core_locs(gc);
    v23_cache_user_locs(gc);
    v23_diag("[6] core shaders ok (solid=%u rrect=%u circle=%u map=%u bgra=%u text=%u)",
             gc->prog_solid, gc->prog_rrect, gc->prog_circle, gc->prog_map, gc->prog_bgra, gc->prog_text);

    /* 初始化共享 TTF 字形图集 */
    v23_atlas_init(&gc->atlas);
    v23_diag("[7] atlas ok tex=%u", gc->atlas.tex);

    /* 初始化 CarPlay 地图四槽纹理：默认走零驱动冲突 glTexSubImage2D 独立 GPU 纹理路径（满血 60fps），建 v23_force_zerocopy 可实验启用 EGLImage */
    if (with_carplay_map && g_v23_map.initialized) {
        int zc_ok = 0;
        int want_zc = (access("/mnt/app/eso/bin/apps/cluster/v23_force_zerocopy", F_OK) == 0 &&
                       access("/mnt/app/eso/bin/apps/cluster/v23_no_zerocopy", F_OK) != 0);
        if (want_zc && v23gl.EglCreateImageKHR && v23gl.GlEGLImageTargetTex2D) {
            zc_ok = 1;
            gl_GenTextures(V23_MAP_SLOTS, gc->map_tex);
            for (int i = 0; i < V23_MAP_SLOTS; i++) {
                gc->map_egl_img[i] = v23gl.EglCreateImageKHR(edpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR,
                                                             (EGLClientBuffer)g_v23_map.pix[i], NULL);
                if (!gc->map_egl_img[i]) { zc_ok = 0; break; }
                gl_BindTexture(GL_TEXTURE_2D, gc->map_tex[i]);
                gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                v23gl.GlEGLImageTargetTex2D(GL_TEXTURE_2D, gc->map_egl_img[i]);
            }
            gc->cur_tex0 = 0;
        }
        if (zc_ok) {
            gc->map_zero_copy = 1;
            v23_diag("[8] map_tex EGL_KHR_image_pixmap ZERO-COPY active (slots=%d)", V23_MAP_SLOTS);
        } else {
            gc->map_zero_copy = 0;
            for (int i = 0; i < V23_MAP_SLOTS; i++) {
                if (gc->map_egl_img[i] && v23gl.EglDestroyImageKHR) {
                    v23gl.EglDestroyImageKHR(edpy, gc->map_egl_img[i]);
                    gc->map_egl_img[i] = NULL;
                }
            }
            gl_GenTextures(1, &gc->map_tex_fb);
            gl_BindTexture(GL_TEXTURE_2D, gc->map_tex_fb);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            /* 安全零初始化，绝不传 NULL */
            uint8_t *zbuf = (uint8_t *)calloc((size_t)gc->map_w * (size_t)gc->map_h, 4);
            if (zbuf) {
                gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gc->map_w, gc->map_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, zbuf);
                free(zbuf);
            }
            if (g_v23_map.stride[0] != gc->map_w * 4) {
                gc->map_tight_buf = (uint8_t *)malloc((size_t)gc->map_w * (size_t)gc->map_h * 4u);
            }
            v23_diag("[8] map_tex glTexSubImage2D ready (%dx%d stride=%d tight=%d)",
                     gc->map_w, gc->map_h, g_v23_map.stride[0], gc->map_tight_buf ? 1 : 0);
        }
    }

    /* 读取 dash.conf 并初始化左右信息框与导航卡 GL 纹理（复用 dash.c + navbox.c） */
    {
        static const char *cl[] = { "/tmp/cl.conf", "/mnt/app/eso/bin/apps/cluster/cl.conf" };
        static const char *dc[] = { "/tmp/dash.conf", "/mnt/app/eso/bin/apps/cluster/dash.conf" };
        dash_read_conf_file(cl);
        dash_read_conf_file(dc);
        dash_layout_all();
    }
    gl_GenTextures(2, gc->dash_tex);
    for (int b = 0; b < 2; b++) {
        dash_use(b);
        if (g_dash_conf.enabled && g_nrows > 0 && g_dash_w > 0 && g_dash_h > 0) {
            gc->dash_en[b] = 1; gc->dash_w[b] = g_dash_w; gc->dash_h[b] = g_dash_h;
            gl_BindTexture(GL_TEXTURE_2D, gc->dash_tex[b]);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_dash_w, g_dash_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_dash_pxbuf[b]);
        }
    }
    dash_use(0);

    gl_GenTextures(1, &gc->nav_tex);
    gc->nav_w = g_nav_w > 0 ? g_nav_w : 295;
    gc->nav_h = g_nav_h > 0 ? g_nav_h : 308;
    gl_BindTexture(GL_TEXTURE_2D, gc->nav_tex);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gc->nav_w, gc->nav_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_nav_px);
    v23_diag("[9] dash/nav tex ok (dash0=%dx%d nav=%dx%d)", gc->dash_w[0], gc->dash_h[0], gc->nav_w, gc->nav_h);

    /* 初始化双沙盒：fb_sb 永久持有内置默认皮肤保底；user_sb 运行用户/热重载皮肤 */
    v23_draw_ops_t ops = {
        gc, v23_op_clear, v23_op_draw_map, v23_op_draw_shader, v23_op_rect, v23_op_circle,
        v23_op_line, v23_op_poly, v23_op_arc, v23_op_ticks, v23_op_needle, v23_op_seg_bar,
        v23_op_bi_bar, v23_op_range_bar, v23_op_text, v23_op_dash_box, v23_op_nav_card, v23_op_turn_arrow
    };
    if (v23_sandbox_init(fb_sb, &ops) != 0 ||
        v23_sandbox_load_code(fb_sb, "default_classic.js", V23_DEFAULT_CLASSIC_JS, strlen(V23_DEFAULT_CLASSIC_JS)) != 0) {
        v23_diag("[10] fallback sandbox init failed: %s", fb_sb->last_err);
        v23_sandbox_destroy(fb_sb);
        free(gc->map_tight_buf); free(user_sb); free(fb_sb); free(gc);
        return 20;
    }
    v23_diag("[10] fb_sb default_classic ok");

    int using_user = 0;
    int fallback_active = 0;
    int slow_frames = 0;
    int timeout_streak = 0;
    int clean_boot_frames = 0;
    int boot_sentinel_active = 0;
    int ready_written = 0;
    char cur_source[32] = "builtin";
    char cur_err[512] = "";
    time_t last_live_mt = 0, last_saved_mt = 0, last_sel_mt = 0;
    char last_sel_txt[64] = "";
    int had_live = 0, had_saved = 0;

    /* 冷启动死循环哨兵检测：若上次加载固化皮肤直接进程崩溃，本次跳过固化皮肤直接保底进默认经典版 */
    int skip_persisted_on_boot = (access("/tmp/v23_theme_loading", F_OK) == 0);
    if (skip_persisted_on_boot) {
        unlink("/tmp/v23_theme_loading");
        snprintf(cur_err, sizeof(cur_err), "检测到上次启动用户皮肤异常崩溃，已自动降级为内置默认经典版");
        v23_set_banner(gc, cur_err, 2, 8);
        fallback_active = 1;
    }

    v23_write_status_json(fb_sb, gc, cur_source, fallback_active, cur_err, 60, with_carplay_map ? 20 : 0);

    uint64_t t_start = v23_now_us(), t_prev = t_start;
    uint64_t last_hot_ms = 0, last_dash_ms = 0, last_fps_us = t_start, last_stat_ms = 0;
    int fps_frames = 0, fps_map_frames = 0, meas_fps = 60, meas_map_fps = with_carplay_map ? 20 : 0;
    uint32_t swaps_since_flip = 0;
    uint64_t last_flip_us = 0;
    uint64_t next_frame_us = t_start + 16667ULL;

    while (!g_v23_stop) {
        if (access("/tmp/vwcn_user_native", F_OK) == 0) {
            usleep(200000);
            t_prev = v23_now_us();
            continue;
        }
        uint64_t t_now = v23_now_us();
        float dt = (float)(t_now - t_prev) * 1e-6f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        t_prev = t_now;
        uint64_t now_ms = t_now / 1000ULL;

        /* 1. 每 250ms 检查内存热重载 (/tmp/theme_live.js) / 固化皮肤 / 皮肤切换索引 */
        if (now_ms - last_hot_ms >= 250ULL) {
            last_hot_ms = now_ms;
            struct stat st_live, st_saved, st_sel;
            int has_live = (stat("/tmp/theme_live.js", &st_live) == 0 && st_live.st_size > 0);
            int has_saved = (!skip_persisted_on_boot &&
                             stat("/mnt/app/eso/bin/apps/cluster/themes_user/my_theme.js", &st_saved) == 0 &&
                             st_saved.st_size > 0);

            int need_reload = 0;
            const char *load_path = NULL;
            const char *load_src = "builtin";
            if (has_live && (!had_live || st_live.st_mtime != last_live_mt)) {
                need_reload = 1; load_path = "/tmp/theme_live.js"; load_src = "live_ram";
                last_live_mt = st_live.st_mtime; had_live = 1;
            } else if (!has_live && had_live) {
                had_live = 0; need_reload = 1;
                if (has_saved) { load_path = "/mnt/app/eso/bin/apps/cluster/themes_user/my_theme.js"; load_src = "saved_user"; last_saved_mt = st_saved.st_mtime; had_saved = 1; }
            } else if (!has_live && has_saved && (!had_saved || st_saved.st_mtime != last_saved_mt)) {
                need_reload = 1; load_path = "/mnt/app/eso/bin/apps/cluster/themes_user/my_theme.js"; load_src = "saved_user";
                last_saved_mt = st_saved.st_mtime; had_saved = 1;
            } else if (!has_live && !has_saved && had_saved) {
                had_saved = 0; need_reload = 1;
            }

            if (need_reload) {
                if (using_user) { v23_sandbox_destroy(user_sb); using_user = 0; }
                fallback_active = 0; slow_frames = 0; timeout_streak = 0; cur_err[0] = '\0';
                snprintf(cur_source, sizeof(cur_source), "%s", load_src);
                if (load_path) {
                    /* 写冷启动哨兵（10 帧稳定后清除） */
                    int sfd = open("/tmp/v23_theme_loading", O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (sfd >= 0) close(sfd);
                    boot_sentinel_active = 1; clean_boot_frames = 0;

                    size_t flen = 0;
                    char *fcode = v23_read_theme_file(load_path, &flen);
                    if (!fcode) {
                        fallback_active = 1;
                        snprintf(cur_err, sizeof(cur_err), "读取皮肤失败或超过 128KB 上限");
                        v23_set_banner(gc, "皮肤读取失败或超 128KB，已自动回退内置默认经典版", 2, 7);
                        v23_diag("theme load failed: read error (%s)", load_path);
                    } else if (v23_sandbox_init(user_sb, &ops) != 0 ||
                               v23_sandbox_load_code(user_sb, load_path, fcode, flen) != 0) {
                        fallback_active = 1;
                        snprintf(cur_err, sizeof(cur_err), "%s", user_sb->last_err[0] ? user_sb->last_err : "皮肤加载失败");
                        v23_sandbox_destroy(user_sb);
                        char bm[320];
                        snprintf(bm, sizeof(bm), "AI 皮肤加载报错（%.180s）· 已自动回退内置默认皮肤", cur_err);
                        v23_set_banner(gc, bm, 2, 8);
                        v23_diag("theme load error: %s", cur_err);
                    } else {
                        using_user = 1;
                        v23_compile_user_shader(gc, &user_sb->metas[0]);
                        char bm[200];
                        snprintf(bm, sizeof(bm), "已热加载 60fps 皮肤：%s（共 %d 套）", user_sb->metas[0].name, user_sb->entry_count);
                        v23_set_banner(gc, bm, 0, 3);
                        v23_diag("theme loaded ok: %s (%d entries)", user_sb->metas[0].name, user_sb->entry_count);
                    }
                    if (fcode) free(fcode);
                } else {
                    v23_compile_user_shader(gc, NULL);
                    v23_set_banner(gc, "已切回内置默认 60fps 经典版", 0, 3);
                }
                v23_write_status_json(using_user ? user_sb : fb_sb, gc, cur_source, fallback_active, cur_err, meas_fps, meas_map_fps);
            }

            /* 检查多皮肤切换索引 /tmp/v23_theme_select（支持整数 0..7 / -1 及 "next #seq" / "prev #seq"，
             * 同时比对 mtime 与内容，彻底消除 QNX 1 秒 mtime 粒度导致的连续切换漏检） */
            if (using_user && stat("/tmp/v23_theme_select", &st_sel) == 0) {
                FILE *sf = fopen("/tmp/v23_theme_select", "r");
                if (sf) {
                    char sb_txt[64];
                    sb_txt[0] = '\0';
                    if (fgets(sb_txt, sizeof(sb_txt), sf) &&
                        (st_sel.st_mtime != last_sel_mt || strcmp(sb_txt, last_sel_txt) != 0)) {
                        last_sel_mt = st_sel.st_mtime;
                        snprintf(last_sel_txt, sizeof(last_sel_txt), "%s", sb_txt);
                        int want = 0;
                        if (strncmp(sb_txt, "next", 4) == 0) {
                            if (fallback_active) {
                                want = 0;
                            } else if (user_sb->entry_count > 1) {
                                want = (user_sb->active_idx + 1) % user_sb->entry_count;
                            } else {
                                want = -1; /* 单皮肤包时在自定义皮肤 ↔ 内置默认经典间往返 */
                            }
                        } else if (strncmp(sb_txt, "prev", 4) == 0) {
                            if (fallback_active) {
                                want = user_sb->entry_count - 1;
                            } else if (user_sb->entry_count > 1) {
                                want = (user_sb->active_idx + user_sb->entry_count - 1) % user_sb->entry_count;
                            } else {
                                want = -1;
                            }
                        } else {
                            want = atoi(sb_txt);
                        }
                        if (want < 0) {
                            /* -1 表示暂时切回默认经典 */
                            fallback_active = 1;
                            v23_compile_user_shader(gc, NULL);
                            v23_set_banner(gc, "已切回内置默认 60fps 经典版", 0, 3);
                        } else if (want < user_sb->entry_count) {
                            fallback_active = 0; slow_frames = 0; timeout_streak = 0; cur_err[0] = '\0';
                            user_sb->active_idx = want;
                            v23_compile_user_shader(gc, &user_sb->metas[want]);
                            char bm[200];
                            snprintf(bm, sizeof(bm), "已切换风格：%s", user_sb->metas[want].name);
                            v23_set_banner(gc, bm, 0, 3);
                        }
                        v23_write_status_json(user_sb, gc, cur_source, fallback_active, cur_err, meas_fps, meas_map_fps);
                    }
                    fclose(sf);
                }
            }
        }

        /* 2. 四槽单向退役轮转 CarPlay 地图纹理（retired <- front <- ready <- old_retired）：
         *    仅当 ready_idx 有新帧且距离上一次纹理切换已至少完成 1 次完整 eglSwapBuffers 且满 15.5ms 时才轮转，
         *    确保刚退进 retired_idx 的旧纹理绝不会在 Tegra 3D GPU 异步光栅化尚未排空时被解码线程提前覆写。 */
        int cur_night = 1;
        if (with_carplay_map && g_v23_map.initialized) {
            pthread_mutex_lock(&g_v23_map.mx);
            cur_night = g_v23_map.auto_night;
            if (g_v23_map.has_fresh &&
                (!gc->map_ever_flipped ||
                 (swaps_since_flip >= 1u && (!gc->map_zero_copy || (t_now - last_flip_us) >= 15500ULL)))) {
                uint32_t seq = g_v23_map.seq;
                int old_retired = g_v23_map.retired_idx;
                g_v23_map.retired_idx = g_v23_map.front_idx;
                g_v23_map.front_idx   = g_v23_map.ready_idx;
                g_v23_map.ready_idx   = old_retired;
                g_v23_map.has_fresh   = 0;
                gc->map_active_slot   = g_v23_map.front_idx;
                gc->map_last_seq      = seq;
                gc->map_ever_flipped  = 1;
                swaps_since_flip      = 0;
                last_flip_us          = t_now;
                fps_map_frames++;
                pthread_mutex_unlock(&g_v23_map.mx);

                int sl = gc->map_active_slot;
                if (gc->map_zero_copy) {
                    /* 通知 Tegra GLES2 驱动外部 EGLImage 已被 VIC 2D 引擎更新，强制失效 GPU L2 纹理缓存 */
                    if (gc->map_tex[sl] && gc->map_egl_img[sl] && v23gl.GlEGLImageTargetTex2D) {
                        gl_ActiveTexture(GL_TEXTURE0);
                        gl_BindTexture(GL_TEXTURE_2D, gc->map_tex[sl]);
                        gc->cur_tex0 = gc->map_tex[sl];
                        v23gl.GlEGLImageTargetTex2D(GL_TEXTURE_2D, gc->map_egl_img[sl]);
                    }
                } else if (g_v23_map.ptr[sl]) {
                    const uint8_t *src_px = g_v23_map.ptr[sl];
                    int st = g_v23_map.stride[sl];
                    int row_bytes = gc->map_w * 4;
                    if (st != row_bytes && gc->map_tight_buf && st > row_bytes) {
                        for (int ry = 0; ry < gc->map_h; ry++) {
                            memcpy(gc->map_tight_buf + (size_t)ry * (size_t)row_bytes,
                                   src_px + (size_t)ry * (size_t)st, (size_t)row_bytes);
                        }
                        src_px = gc->map_tight_buf;
                    }
                    gl_ActiveTexture(GL_TEXTURE0);
                    gl_BindTexture(GL_TEXTURE_2D, gc->map_tex_fb);
                    gc->cur_tex0 = gc->map_tex_fb;
                    if (v23gl.PixelStorei) v23gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gc->map_w, gc->map_h,
                                     GL_RGBA, GL_UNSIGNED_BYTE, src_px);
                }
            } else {
                pthread_mutex_unlock(&g_v23_map.mx);
            }
        }

        /* 3. 每 33ms 轮询 dash.c / navbox.c 并在脏更新时上传纹理 */
        if (now_ms - last_dash_ms >= 33ULL) {
            last_dash_ms = now_ms;
            if (dash_poll(now_ms) && g_dash_have) {
                for (int b = 0; b < 2; b++) {
                    if (gc->dash_en[b]) {
                        gl_BindTexture(GL_TEXTURE_2D, gc->dash_tex[b]);
                        gc->cur_tex0 = 0;
                        gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gc->dash_w[b], gc->dash_h[b],
                                         GL_RGBA, GL_UNSIGNED_BYTE, g_dash_pxbuf[b]);
                    }
                }
            }
            if (nav_poll() && g_nav_active && g_nav_dirty) {
                gl_BindTexture(GL_TEXTURE_2D, gc->nav_tex);
                gc->cur_tex0 = 0;
                if (g_nav_w != gc->nav_w || g_nav_h != gc->nav_h) {
                    gc->nav_w = g_nav_w; gc->nav_h = g_nav_h;
                    gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gc->nav_w, gc->nav_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_nav_px);
                } else {
                    gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gc->nav_w, gc->nav_h, GL_RGBA, GL_UNSIGNED_BYTE, g_nav_px);
                }
            }
        }

        /* 4. 更新 60fps 平滑总线 */
        v23_update_telemetry(&gc->cur_bus, dt, with_carplay_map, cur_night, meas_fps, meas_map_fps,
                             (double)(t_now - t_start) * 1e-6);

        /* 5. 执行 60fps 皮肤渲染（带 35ms 硬中断 / 连续超时容错 / 掉帧看门狗 / 同帧零黑屏补画保底） */
        gl_Viewport(0, 0, ww, wh);
        if (v23gl.Enable) v23gl.Enable(GL_BLEND);
        if (v23gl.BlendFunc) v23gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (using_user && !fallback_active) {
            uint64_t js_us = 0;
            int rrc = v23_sandbox_render_frame(user_sb, &gc->cur_bus, &js_us, NULL);
            if (rrc != 0) {
                if (user_sb->guard.timed_out && ++timeout_streak < 3) {
                    /* 偶发 1~2 帧超 35ms（如 QNX 调度/GC/字形首次光栅化毛刺）：当帧补画默认版防黑屏，不永久踢掉用户皮肤 */
                    v23_diag("theme timeout spike (%d/3, %.1fms), keeping user skin", timeout_streak, (double)js_us / 1000.0);
                    v23_sandbox_render_frame(fb_sb, &gc->cur_bus, NULL, NULL);
                } else {
                    fallback_active = 1;
                    snprintf(cur_err, sizeof(cur_err), "%s", user_sb->last_err);
                    char bm[320];
                    snprintf(bm, sizeof(bm), "AI 皮肤运行异常（%.180s）· 已自动回退内置默认皮肤", cur_err);
                    v23_set_banner(gc, bm, 2, 8);
                    v23_write_status_json(user_sb, gc, cur_source, 1, cur_err, meas_fps, meas_map_fps);
                    v23_diag("theme runtime error: %s", cur_err);
                    /* 同帧立即用内置默认经典版补画，0 黑帧！ */
                    v23_sandbox_render_frame(fb_sb, &gc->cur_bus, NULL, NULL);
                }
            } else {
                timeout_streak = 0;
                if (js_us > 25000ULL) {
                    if (++slow_frames >= 60) {
                        fallback_active = 1;
                        snprintf(cur_err, sizeof(cur_err), "连续 60 帧 JS 耗时超 25ms，掉帧看门狗已熔断回退默认皮肤");
                        v23_set_banner(gc, cur_err, 2, 8);
                        v23_write_status_json(user_sb, gc, cur_source, 1, cur_err, meas_fps, meas_map_fps);
                        v23_diag("theme watchdog fallback (>25ms x 60)");
                    }
                } else {
                    slow_frames = 0;
                }
                if (boot_sentinel_active && ++clean_boot_frames >= 10) {
                    unlink("/tmp/v23_theme_loading");
                    boot_sentinel_active = 0;
                }
            }
        } else {
            v23_sandbox_render_frame(fb_sb, &gc->cur_bus, NULL, NULL);
        }

        /* 6. 叠画顶部安全提示横幅并按 16667us 单调绝对网格精准锁 60fps 送显
         *    （4 缓冲下 libEGL 不阻塞 VSync，扣除 1000us QNX 1ms 时钟向上取整余量避免掉帧至 53fps，慢帧自动重对齐不飙帧） */
        v23_draw_banner(gc, t_now);
        eglSwapBuffers(edpy, esurf);
        if (swaps_since_flip < 1000000u) swaps_since_flip++;
        {
            uint64_t t_after = v23_now_us();
            if (t_after + 1100ULL < next_frame_us) {
                usleep((useconds_t)(next_frame_us - t_after - 1000ULL));
            }
            next_frame_us += 16667ULL;
            t_after = v23_now_us();
            if (next_frame_us < t_after || next_frame_us > t_after + 33334ULL) {
                next_frame_us = t_after + 16667ULL;
            }
        }

        if (!ready_written) {
            ready_written = 1;
            int rfd = open("/tmp/cl_render_ready", O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (rfd >= 0) {
                char rb[24];
                int rl = snprintf(rb, sizeof(rb), "%d\n", (int)getpid());
                (void)write(rfd, rb, (size_t)rl);
                close(rfd);
            }
            /* 首帧就绪后关闭常规帧流水日志（仅保留 rdbg_err 异常配额），将稳态 /tmp RAMFS 写入降为 0 */
            g_rdbg = 0;
            v23_diag("[11] first eglSwapBuffers ok -> cl_render_ready written (%dx%d win_bufs=%d slots=%d map=%d zc=%d)",
                     ww, wh, alloc_win_bufs, V23_MAP_SLOTS, with_carplay_map, gc->map_zero_copy);
        }

        fps_frames++;
        if (t_now - last_fps_us >= 1000000ULL) {
            double sec = (double)(t_now - last_fps_us) * 1e-6;
            meas_fps = (int)((double)fps_frames / sec + 0.5);
            meas_map_fps = (int)((double)fps_map_frames / sec + 0.5);
            fps_frames = 0; fps_map_frames = 0; last_fps_us = t_now;
        }
        if (now_ms - last_stat_ms >= 2000ULL) {
            last_stat_ms = now_ms;
            v23_write_status_json(using_user ? user_sb : fb_sb, gc,
                                  cur_source, fallback_active, cur_err, meas_fps, meas_map_fps);
        }
    }

    if (using_user) v23_sandbox_destroy(user_sb);
    v23_sandbox_destroy(fb_sb);
    if (boot_sentinel_active) unlink("/tmp/v23_theme_loading");
    for (int i = 0; i < V23_MAP_SLOTS; i++) {
        if (gc->map_egl_img[i] && v23gl.EglDestroyImageKHR) v23gl.EglDestroyImageKHR(edpy, gc->map_egl_img[i]);
    }
    free(gc->map_tight_buf);
    free(user_sb);
    free(fb_sb);
    free(gc);
    eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(edpy, ectx);
    eglDestroySurface(edpy, esurf);
    eglTerminate(edpy);
    if (own_win) screen_destroy_window(own_win);
    if (own_ctx) screen_destroy_context(own_ctx);
    v23_diag("v23: 60fps JS engine exited cleanly");
    return 0;
}

static void *v23_render_thread_entry(void *arg) {
    int with_map = (int)(intptr_t)arg;
    int rc = v23_run_gl_loop(with_map);
    if (rc != 0 && !g_v23_stop) {
        v23_diag("v23: render thread failed rc=%d -> exiting to trigger Layer-2 fallback", rc);
        _exit(rc);
    }
    return NULL;
}

static int run_v23_idle_mode(int argc, char **argv) {
    (void)argc; (void)argv;
    return v23_run_gl_loop(0);
}

#endif /* USE_DASH */
