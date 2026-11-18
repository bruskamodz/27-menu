/* 27 Menu - Rainbow Six Siege (CUSA01788 3.71) - single file build */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <orbis/Pad.h>
#include <orbis/Gnm.h>
#include <orbis/VideoOut.h>
#include <orbis/Sysutil.h>
#include <orbis/Kernel.h>
#include <orbis/libkernel.h>

#include <Common.h>
#include <GoldHEN_Hook.h>

#define R27_PATH  "/data/27Menu"
#define R27_CFG   R27_PATH "/config.ini"
#define R27_LOG   R27_PATH "/27menu.log"

#define attr_public __attribute__((visibility("default")))
#define attr_hidden __attribute__((weak)) __attribute__((visibility("hidden")))

attr_public const char *g_pluginName = "27Menu";
attr_public const char *g_pluginDesc = "27 Menu - Rainbow Six Siege";
attr_public const char *g_pluginAuth = "venice";
attr_public uint32_t g_pluginVersion = 0x00000100;

/* ================= LOG / NOTIFY ================= */
static void r27_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    klog("[27Menu] %s", buf);
    FILE *f = fopen(R27_LOG, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

static void r27_notify(const char *fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    sceSysutilSendSystemNotificationWithText(0xFE, buf);
}

/* ================= MATH ================= */
typedef struct { float x, y, z; } vec3;
typedef struct { float x, y; } vec2;

#define RAD2DEG(r) ((r) * 57.29577951308232f)
#define DEG2RAD(d) ((d) * 0.017453292519943295f)

static vec3 v3_sub(vec3 a, vec3 b) { vec3 r = {a.x-b.x, a.y-b.y, a.z-b.z}; return r; }
static float v3_len(vec3 v) { return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z); }

static int r27_w2s(const float *m, vec3 p, float sw, float sh, vec2 *out)
{
    float cx = m[0]*p.x + m[4]*p.y + m[8]*p.z  + m[12];
    float cy = m[1]*p.x + m[5]*p.y + m[9]*p.z  + m[13];
    float cw = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
    if (cw < 0.001f) return 0;
    out->x = ((cx / cw) * 0.5f + 0.5f) * sw;
    out->y = (1.0f - ((cy / cw) * 0.5f + 0.5f)) * sh;
    return 1;
}

static void r27_angles(vec3 dir, float *pitch, float *yaw)
{
    float len = v3_len(dir);
    if (len < 0.0001f) return;
    *pitch = RAD2DEG(asinf(dir.z / len));
    *yaw   = RAD2DEG(atan2f(dir.y, dir.x)) + 90.0f;
    if (*yaw > 180.0f)  *yaw -= 360.0f;
    if (*yaw < -180.0f) *yaw += 360.0f;
}

/* ================= CONFIG ================= */
typedef struct {
    int   aimbot;
    float aimbot_fov;
    float aimbot_smooth;
    int   silent;
    float silent_fov;
    int   esp;
    int   esp_skeleton;
    int   esp_box;
    float esp_max_dist;
    int   norecoil;
    int   menu_open;
} r27_cfg;

static r27_cfg g_cfg;

static void r27_cfg_defaults(void)
{
    g_cfg.aimbot = 0;  g_cfg.aimbot_fov = 35.0f; g_cfg.aimbot_smooth = 4.0f;
    g_cfg.silent  = 0; g_cfg.silent_fov  = 12.0f;
    g_cfg.esp = 0; g_cfg.esp_skeleton = 1; g_cfg.esp_box = 0; g_cfg.esp_max_dist = 120.0f;
    g_cfg.norecoil = 0; g_cfg.menu_open = 0;
}

static void r27_cfg_save(void)
{
    FILE *f = fopen(R27_CFG, "w");
    if (!f) return;
    fprintf(f, "aimbot=%.0f\naimbot_fov=%.1f\naimbot_smooth=%.1f\n",
            (float)g_cfg.aimbot, g_cfg.aimbot_fov, g_cfg.aimbot_smooth);
    fprintf(f, "silent=%.0f\nsilent_fov=%.1f\n", (float)g_cfg.silent, g_cfg.silent_fov);
    fprintf(f, "esp=%.0f\nesp_skeleton=%.0f\nesp_box=%.0f\nesp_max_dist=%.1f\n",
            (float)g_cfg.esp, (float)g_cfg.esp_skeleton, (float)g_cfg.esp_box, g_cfg.esp_max_dist);
    fprintf(f, "norecoil=%.0f\n", (float)g_cfg.norecoil);
    fclose(f);
}

static void r27_cfg_load(void)
{
    r27_cfg_defaults();
    FILE *f = fopen(R27_CFG, "r");
    if (!f) { r27_cfg_save(); return; }
    char k[32]; float v;
    while (fscanf(f, "%31[^=]=%f\n", k, &v) == 2) {
        if      (!strcmp(k, "aimbot"))        g_cfg.aimbot = (int)v;
        else if (!strcmp(k, "aimbot_fov"))    g_cfg.aimbot_fov = v;
        else if (!strcmp(k, "aimbot_smooth")) g_cfg.aimbot_smooth = v;
        else if (!strcmp(k, "silent"))        g_cfg.silent = (int)v;
        else if (!strcmp(k, "silent_fov"))    g_cfg.silent_fov = v;
        else if (!strcmp(k, "esp"))           g_cfg.esp = (int)v;
        else if (!strcmp(k, "esp_skeleton"))  g_cfg.esp_skeleton = (int)v;
        else if (!strcmp(k, "esp_box"))       g_cfg.esp_box = (int)v;
        else if (!strcmp(k, "esp_max_dist"))  g_cfg.esp_max_dist = v;
        else if (!strcmp(k, "norecoil"))      g_cfg.norecoil = (int)v;
    }
    fclose(f);
}

/* ================= PLATFORM ================= */
static uintptr_t r27_base(void) { return (uintptr_t)procInfo.base_address; }

static int r27_read(uintptr_t a, void *dst, size_t n)
{
    if (!a) return 0;
    return kernel_copyin((void *)a, dst, n) == 0;
}

static int r27_write(uintptr_t a, const void *src, size_t n)
{
    if (!a) return 0;
    kernel_mprotect(a, n, PROT_READ | PROT_WRITE | PROT_EXEC);
    return kernel_copyout((void *)a, (void *)src, n) == 0;
}

static void r27_sleep_ms(uint32_t ms) { sceKernelUsleep(ms * 1000); }

static uintptr_t r27_sym(const char *name) { return (uintptr_t)dlsym(RTLD_DEFAULT, name); }

/* ================= SIGSCAN ================= */
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } r27_phdr;
typedef struct { uint8_t id[16]; uint16_t e_type, e_machine; uint32_t e_version;
                 uint64_t e_entry, e_phoff, e_shoff; uint32_t e_flags;
                 uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; } r27_ehdr;

static uintptr_t r27_scan(const char *pattern)
{
    uintptr_t base = r27_base();
    if (!base) return 0;

    r27_ehdr eh;
    if (!r27_read(base, &eh, sizeof(eh)) || !eh.e_phnum) return 0;

    uintptr_t start = 0; size_t size = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        r27_phdr ph;
        if (!r27_read(base + eh.e_phoff + (uintptr_t)i * sizeof(ph), &ph, sizeof(ph))) continue;
        if (ph.p_type == 1 && (ph.p_flags & 1) && ph.p_memsz) {
            start = base + ph.p_vaddr; size = ph.p_memsz; break;
        }
    }
    if (!start || !size) return 0;

    uint8_t pat[64], msk[64]; size_t plen = 0;
    for (const char *p = pattern; *p && plen < 64; ) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { pat[plen] = 0; msk[plen] = 0; plen++; p++; continue; }
        char h[3] = { p[0], p[1], 0 };
        pat[plen] = (uint8_t)strtoul(h, NULL, 16);
        msk[plen] = 0xFF; plen++; p += 2;
    }
    if (!plen) return 0;

    uint8_t *buf = malloc(size);
    if (!buf) return 0;
    if (!r27_read(start, buf, size)) { free(buf); return 0; }

    uintptr_t found = 0;
    for (size_t i = 0; i + plen < size && !found; i++) {
        size_t j = 0;
        for (; j < plen; j++) if (msk[j] && buf[i + j] != pat[j]) break;
        if (j == plen) found = start + i;
    }
    free(buf);
    return found;
}

/* ================= GAME ================= */
#define R27_MAX_BONES 12

/* ATTENZIONE: offset e pattern sono segnaposto, da confermare sul dump 3.71 */
#define OFF_LOCAL_FROM_WORLD 0x00
#define OFF_ENT_LIST         0x00
#define OFF_ENT_COUNT        0x00
#define OFF_ENT_STRIDE       0x00
#define OFF_ENT_POS          0x00
#define OFF_ENT_BONES        0x00
#define OFF_CAM_POS          0x00
#define OFF_CAM_MATRIX       0x00
#define OFF_CAM_PITCH        0x00
#define OFF_CAM_YAW          0x00

typedef struct {
    uintptr_t world, set_angles, recoil_mul;
} r27_game;

static r27_game g_game;
static int s_resolved;

static const int g_bones[R27_MAX_BONES] = { 0,1,2,3,4,5,6,7,8,9,10,11 };

static int r27_resolve(void)
{
    uintptr_t w = r27_scan("48 8B 05 ?? ?? ?? ?? 48 8B 88 ?? ?? ?? ?? 48 85 C9 74");
    if (!w) { r27_log("pattern world non trovato"); return 0; }

    int32_t rel = 0;
    r27_read(w + 3, &rel, 4);
    g_game.world = w + 7 + rel;

    g_game.set_angles = r27_scan("F3 0F 11 ?? F3 0F 11 ?? C3");

    uintptr_t rec = r27_scan("F3 0F 10 05 ?? ?? ?? ?? F3 0F 11");
    if (rec) { r27_read(rec + 4, &rel, 4); g_game.recoil_mul = rec + 8 + rel; }

    s_resolved = g_game.world != 0;
    r27_log("resolve world=%p angoli=%p recoil=%p", (void *)g_game.world,
            (void *)g_game.set_angles, (void *)g_game.recoil_mul);
    return s_resolved;
}

static uintptr_t r27_local(void)
{
    uintptr_t p = 0;
    if (!g_game.world) return 0;
    r27_read(g_game.world + OFF_LOCAL_FROM_WORLD, &p, 8);
    return p;
}

static int r27_cam_pos(vec3 *out)
{
    uintptr_t l = r27_local();
    return l && r27_read(l + OFF_CAM_POS, out, sizeof(vec3));
}

static int r27_matrix(float *m)
{
    uintptr_t l = r27_local();
    return l && r27_read(l + OFF_CAM_MATRIX, m, sizeof(float) * 16);
}

static int r27_count(void)
{
    int32_t n = 0;
    if (!g_game.world) return 0;
    r27_read(g_game.world + OFF_ENT_COUNT, &n, 4);
    return (n > 0 && n < 64) ? n : 0;
}

static uintptr_t r27_ent(int i)
{
    uintptr_t list = 0, e = 0;
    int32_t stride = 8;
    if (!g_game.world) return 0;
    r27_read(g_game.world + OFF_ENT_LIST, &list, 8);
    r27_read(g_game.world + OFF_ENT_STRIDE, &stride, 4);
    if (!list || stride <= 0 || stride > 0x400) return 0;
    r27_read(list + (uintptr_t)i * stride, &e, 8);
    return e;
}

static int r27_ent_pos(int i, vec3 *out)
{
    uintptr_t e = r27_ent(i);
    return e && r27_read(e + OFF_ENT_POS, out, sizeof(vec3)) && v3_len(*out) > 1.0f;
}

static int r27_ent_bones(int i, vec3 *out)
{
    uintptr_t e = r27_ent(i);
    int n = 0;
    if (!e) return 0;
    for (int b = 0; b < R27_MAX_BONES; b++) {
        if (!r27_read(e + OFF_ENT_BONES + (uintptr_t)g_bones[b] * 0x20, &out[n], sizeof(vec3))) break;
        n++;
    }
    return n;
}

static void r27_set_angles(float pitch, float yaw)
{
    uintptr_t l = r27_local();
    if (!l) return;
    if (g_game.set_angles) { ((void (*)(float, float))g_game.set_angles)(pitch, yaw); return; }
    r27_write(l + OFF_CAM_PITCH, &pitch, 4);
    r27_write(l + OFF_CAM_YAW, &yaw, 4);
}

static void r27_get_angles(float *pitch, float *yaw)
{
    uintptr_t l = r27_local();
    if (!l) { *pitch = *yaw = 0; return; }
    r27_read(l + OFF_CAM_PITCH, pitch, 4);
    r27_read(l + OFF_CAM_YAW, yaw, 4);
}

/* ================= RENDER ================= */
#define COL(r,g,b,a) ((uint32_t)(((a)<<24)|((b)<<16)|((g)<<8)|(r)))
#define C_BG   COL(10,10,12,220)
#define C_BAR  COL(230,40,40,255)
#define C_TXT  COL(235,235,235,255)
#define C_DIM  COL(150,150,155,255)
#define C_ON   COL(90,220,120,255)
#define C_OFF  COL(120,120,125,255)
#define C_BONE COL(255,255,255,200)
#define C_BOX  COL(230,40,40,200)

#define MAX_QUADS 4096

typedef struct { float x, y, z, w, u, v; uint32_t c; } r27_vert;

static r27_vert s_vb[MAX_QUADS * 6];
static int      s_n;
static int      s_rinit;
static void   (*s_flip)(int32_t, int32_t, int32_t, int32_t, uint64_t);

static void r27_quad(float x, float y, float w, float h, uint32_t c)
{
    if (s_n + 6 > MAX_QUADS * 6) return;
    float x2 = x + w, y2 = y + h;
    r27_vert *v = &s_vb[s_n];
    v[0] = (r27_vert){x,  y,  0, 1, 0, 0, c};
    v[1] = (r27_vert){x2, y,  0, 1, 1, 0, c};
    v[2] = (r27_vert){x2, y2, 0, 1, 1, 1, c};
    v[3] = (r27_vert){x,  y,  0, 1, 0, 0, c};
    v[4] = (r27_vert){x2, y2, 0, 1, 1, 1, c};
    v[5] = (r27_vert){x,  y2, 0, 1, 0, 1, c};
    s_n += 6;
}

static void r27_rect(float x, float y, float w, float h, uint32_t c) { r27_quad(x, y, w, h, c); }

static void r27_line(float x1, float y1, float x2, float y2, uint32_t c)
{
    float dx = x2 - x1, dy = y2 - y1;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float t = 1.2f / len;
    r27_quad(x1 - dy * t, y1 + dx * t, len + 2.0f, 2.4f, c);
}

/* font 5x7 */
static int r27_glyph(char ch, uint8_t out[5])
{
    uint8_t g[5] = {0,0,0,0,0};
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    switch (ch) {
    case ' ': break;
    case '!': { uint8_t t[5]={0x00,0x00,0x5F,0x00,0x00}; memcpy(g,t,5); } break;
    case '-': { uint8_t t[5]={0x08,0x08,0x08,0x08,0x08}; memcpy(g,t,5); } break;
    case '.': { uint8_t t[5]={0x00,0x60,0x60,0x00,0x00}; memcpy(g,t,5); } break;
    case ':': { uint8_t t[5]={0x00,0x36,0x36,0x00,0x00}; memcpy(g,t,5); } break;
    case '/': { uint8_t t[5]={0x20,0x10,0x08,0x04,0x02}; memcpy(g,t,5); } break;
    case '|': { uint8_t t[5]={0x00,0x00,0x7F,0x00,0x00}; memcpy(g,t,5); } break;
    case '+': { uint8_t t[5]={0x08,0x08,0x3E,0x08,0x08}; memcpy(g,t,5); } break;
    case '0': { uint8_t t[5]={0x3E,0x51,0x49,0x45,0x3E}; memcpy(g,t,5); } break;
    case '1': { uint8_t t[5]={0x00,0x42,0x7F,0x40,0x00}; memcpy(g,t,5); } break;
    case '2': { uint8_t t[5]={0x42,0x61,0x51,0x49,0x46}; memcpy(g,t,5); } break;
    case '3': { uint8_t t[5]={0x21,0x41,0x45,0x4B,0x31}; memcpy(g,t,5); } break;
    case '4': { uint8_t t[5]={0x18,0x14,0x12,0x7F,0x10}; memcpy(g,t,5); } break;
    case '5': { uint8_t t[5]={0x27,0x45,0x45,0x45,0x39}; memcpy(g,t,5); } break;
    case '6': { uint8_t t[5]={0x3C,0x4A,0x49,0x49,0x30}; memcpy(g,t,5); } break;
    case '7': { uint8_t t[5]={0x01,0x71,0x09,0x05,0x03}; memcpy(g,t,5); } break;
    case '8': { uint8_t t[5]={0x36,0x49,0x49,0x49,0x36}; memcpy(g,t,5); } break;
    case '9': { uint8_t t[5]={0x06,0x49,0x49,0x29,0x1E}; memcpy(g,t,5); } break;
    case 'A': { uint8_t t[5]={0x7E,0x11,0x11,0x11,0x7E}; memcpy(g,t,5); } break;
    case 'B': { uint8_t t[5]={0x7F,0x49,0x49,0x49,0x36}; memcpy(g,t,5); } break;
    case 'C': { uint8_t t[5]={0x3E,0x41,0x41,0x41,0x22}; memcpy(g,t,5); } break;
    case 'D': { uint8_t t[5]={0x7F,0x41,0x41,0x22,0x1C}; memcpy(g,t,5); } break;
    case 'E': { uint8_t t[5]={0x7F,0x49,0x49,0x49,0x41}; memcpy(g,t,5); } break;
    case 'F': { uint8_t t[5]={0x7F,0x09,0x09,0x09,0x01}; memcpy(g,t,5); } break;
    case 'G': { uint8_t t[5]={0x3E,0x41,0x49,0x49,0x7A}; memcpy(g,t,5); } break;
    case 'H': { uint8_t t[5]={0x7F,0x08,0x08,0x08,0x7F}; memcpy(g,t,5); } break;
    case 'I': { uint8_t t[5]={0x00,0x41,0x7F,0x41,0x00}; memcpy(g,t,5); } break;
    case 'J': { uint8_t t[5]={0x20,0x40,0x41,0x3F,0x01}; memcpy(g,t,5); } break;
    case 'K': { uint8_t t[5]={0x7F,0x08,0x14,0x22,0x41}; memcpy(g,t,5); } break;
    case 'L': { uint8_t t[5]={0x7F,0x40,0x40,0x40,0x40}; memcpy(g,t,5); } break;
    case 'M': { uint8_t t[5]={0x7F,0x02,0x0C,0x02,0x7F}; memcpy(g,t,5); } break;
    case 'N': { uint8_t t[5]={0x7F,0x04,0x08,0x10,0x7F}; memcpy(g,t,5); } break;
    case 'O': { uint8_t t[5]={0x3E,0x41,0x41,0x41,0x3E}; memcpy(g,t,5); } break;
    case 'P': { uint8_t t[5]={0x7F,0x09,0x09,0x09,0x06}; memcpy(g,t,5); } break;
    case 'Q': { uint8_t t[5]={0x3E,0x41,0x51,0x21,0x5E}; memcpy(g,t,5); } break;
    case 'R': { uint8_t t[5]={0x7F,0x09,0x19,0x29,0x46}; memcpy(g,t,5); } break;
    case 'S': { uint8_t t[5]={0x46,0x49,0x49,0x49,0x31}; memcpy(g,t,5); } break;
    case 'T': { uint8_t t[5]={0x01,0x01,0x7F,0x01,0x01}; memcpy(g,t,5); } break;
    case 'U': { uint8_t t[5]={0x3F,0x40,0x40,0x40,0x3F}; memcpy(g,t,5); } break;
    case 'V': { uint8_t t[5]={0x1F,0x20,0x40,0x20,0x1F}; memcpy(g,t,5); } break;
    case 'W': { uint8_t t[5]={0x3F,0x40,0x38,0x40,0x3F}; memcpy(g,t,5); } break;
    case 'X': { uint8_t t[5]={0x63,0x14,0x08,0x14,0x63}; memcpy(g,t,5); } break;
    case 'Y': { uint8_t t[5]={0x07,0x08,0x70,0x08,0x07}; memcpy(g,t,5); } break;
    case 'Z': { uint8_t t[5]={0x61,0x51,0x49,0x45,0x43}; memcpy(g,t,5); } break;
    default: break;
    }
    memcpy(out, g, 5);
    return 1;
}

static void r27_text(float x, float y, float s, uint32_t c, const char *str)
{
    for (; *str; str++, x += 6.0f * s) {
        uint8_t g[5];
        r27_glyph(*str, g);
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if (g[col] & (1 << row))
                    r27_rect(x + col * s, y + row * s, s, s, c);
    }
}

static void r27_flush(void)
{
    if (!s_n) return;
    SceGnmDrawContext *dc = sceGnmGetGfxContext();
    SceGnmRenderTarget rt;
    if (!dc || sceGnmGetRenderTarget(dc, &rt) != 0) { s_n = 0; return; }

    sceGnmDrawInitDefaultHardwareState(dc, 0x40000);
    sceGnmSetRenderTarget(dc, &rt);
    sceGnmSetViewport(dc, 0, 0, 1920.0f, 1080.0f, 0.0f, 1.0f);
    sceGnmSetScissor(dc, 0, 0, 1920, 1080);

    SceGnmPrimitiveSetup ps; memset(&ps, 0, sizeof(ps));
    ps.prim_type = SCE_GNM_PRIM_TRIANGLE_LIST;
    sceGnmSetPrimitiveSetup(dc, &ps);

    sceGnmSetEmbeddedVsShader(dc, SCE_GNM_EMBEDDED_VS_SHADER_COPY, 0);
    sceGnmSetEmbeddedPsShader(dc, SCE_GNM_EMBEDDED_PS_SHADER_COPY);

    SceGnmVertexBuffer vb; memset(&vb, 0, sizeof(vb));
    vb.vb_size = sizeof(r27_vert);
    vb.vb_addr = (uint64_t)s_vb;
    sceGnmSetVbControl(dc, &vb, 1);

    sceGnmDrawIndexAuto(dc, s_n, 0);
    s_n = 0;
}

static int32_t r27_flip_hook(int32_t a, int32_t b, int32_t c, int32_t d, uint64_t e)
{
    r27_flush();
    return s_flip(a, b, c, d, e);
}

static void r27_render_init(void)
{
    if (s_rinit) return;
    uintptr_t f = r27_sym("sceVideoOutSubmitFlip");
    if (!f) { r27_log("flip non risolto"); return; }
    if (HOOK_INSTALL((void *)f, (void *)r27_flip_hook, (void **)&s_flip) != 0) {
        r27_log("hook flip fallito"); return;
    }
    s_rinit = 1;
    r27_log("renderer pronto");
}

/* ================= MENU ================= */
#define PAD_R2       0x0200
#define PAD_TOUCH    0x100000
#define PAD_CROSS    0x4000
#define PAD_UP       0x0010
#define PAD_DOWN     0x0040
#define PAD_LEFT     0x0080
#define PAD_RIGHT    0x0020

enum { IT_AIM, IT_AFOV, IT_ASM, IT_SIL, IT_SFOV, IT_ESP, IT_SKEL, IT_BOX, IT_DIST, IT_NR, IT_SAVE, IT_COUNT };

static const char *const s_names[IT_COUNT] = {
    "AIMBOT", "AIM FOV", "AIM SMOOTH", "SILENT AIM", "SILENT FOV",
    "SKELETON ESP", "BOX ESP", "MAX DIST", "NO RECOIL", "SALVA CONFIG"
};

static int s_sel;
static uint32_t s_prev;

static float *r27_slider(int it)
{
    switch (it) {
    case IT_AFOV: return &g_cfg.aimbot_fov;
    case IT_ASM:  return &g_cfg.aimbot_smooth;
    case IT_SFOV: return &g_cfg.silent_fov;
    case IT_DIST: return &g_cfg.esp_max_dist;
    }
    return NULL;
}

static void r27_menu_update(uint32_t btn)
{
    uint32_t pressed = btn & ~s_prev;
    s_prev = btn;

    if (pressed & PAD_UP)   s_sel = (s_sel + IT_COUNT - 1) % IT_COUNT;
    if (pressed & PAD_DOWN) s_sel = (s_sel + 1) % IT_COUNT;

    float *sl = r27_slider(s_sel);
    if (sl) {
        if (btn & PAD_LEFT)  *sl -= 0.5f;
        if (btn & PAD_RIGHT) *sl += 0.5f;
        if (*sl < 0.5f) *sl = 0.5f;
        return;
    }
    if (!(pressed & PAD_CROSS)) return;

    switch (s_sel) {
    case IT_AIM:  g_cfg.aimbot = !g_cfg.aimbot; break;
    case IT_SIL:  g_cfg.silent = !g_cfg.silent; break;
    case IT_ESP:  g_cfg.esp = !g_cfg.esp; break;
    case IT_SKEL: g_cfg.esp_skeleton = !g_cfg.esp_skeleton; break;
    case IT_BOX:  g_cfg.esp_box = !g_cfg.esp_box; break;
    case IT_NR:   g_cfg.norecoil = !g_cfg.norecoil; break;
    case IT_SAVE: r27_cfg_save(); r27_notify("27 Menu: config salvata"); break;
    }
}

static uint32_t r27_state(int it)
{
    switch (it) {
    case IT_AIM:  return g_cfg.aimbot ? C_ON : C_OFF;
    case IT_SIL:  return g_cfg.silent ? C_ON : C_OFF;
    case IT_ESP:  return g_cfg.esp ? C_ON : C_OFF;
    case IT_SKEL: return g_cfg.esp_skeleton ? C_ON : C_OFF;
    case IT_BOX:  return g_cfg.esp_box ? C_ON : C_OFF;
    case IT_NR:   return g_cfg.norecoil ? C_ON : C_OFF;
    }
    return C_DIM;
}

static void r27_menu_draw(void)
{
    float x = 40, y = 110, w = 440, h = 66 + IT_COUNT * 34;

    r27_rect(x, y, w, h, C_BG);
    r27_rect(x, y, 6, h, C_BAR);
    r27_text(x + 20, y + 16, 2.0f, C_TXT, "27 MENU");
    r27_text(x + 20, y + 44, 1.0f, C_DIM, "CUSA01788 3.71 V1.0");

    for (int i = 0; i < IT_COUNT; i++) {
        float iy = y + 72 + i * 34;
        float *sl = r27_slider(i);
        char buf[24];

        if (i == s_sel) r27_rect(x + 12, iy - 4, w - 24, 28, COL(40,40,46,255));
        r27_text(x + 20, iy, 1.0f, (i == s_sel) ? C_TXT : C_DIM, s_names[i]);

        if (sl) snprintf(buf, sizeof(buf), "%.1f", *sl);
        else    snprintf(buf, sizeof(buf), "%s", (r27_state(i) == C_ON) ? "ON" : "OFF");
        r27_text(x + w - 100, iy, 1.0f, sl ? C_TXT : r27_state(i), buf);
    }
    r27_text(x + 20, y + h - 24, 0.8f, C_DIM, "DPAD NAV - X TOGGLE - L/R ADJ - 2X TOUCH CHIUDI");
}

static void r27_crosshair(void)
{
    float cx = 960, cy = 540;
    r27_line(cx - 8, cy, cx + 8, cy, C_TXT);
    r27_line(cx, cy - 8, cx, cy + 8, C_TXT);

    if (!g_cfg.aimbot) return;
    float r = g_cfg.aimbot_fov * 3.0f;
    for (int a = 0; a < 360; a += 12) {
        float ra = DEG2RAD((float)a);
        r27_line(cx + cosf(ra) * r, cy + sinf(ra) * r,
                 cx + cosf(ra) * (r + 4), cy + sinf(ra) * (r + 4), C_BAR);
    }
}

/* ================= FEATURES ================= */
static const int s_links[][2] = { {0,1},{1,2},{2,3},{3,4},{4,5},
                                  {4,6},{6,7},{7,8},{4,9},{9,10},{10,11} };

static int r27_target(vec3 *out, float max_px)
{
    float m[16]; vec3 me;
    if (!r27_matrix(m) || !r27_cam_pos(&me)) return 0;

    float best = max_px;
    int found = 0;

    for (int i = 0; i < r27_count(); i++) {
        vec3 p; vec2 sp;
        if (!r27_ent_pos(i, &p)) continue;
        if (!r27_w2s(m, p, 1920.0f, 1080.0f, &sp)) continue;

        float dx = sp.x - 960.0f, dy = sp.y - 540.0f;
        float d = sqrtf(dx*dx + dy*dy);
        if (d < best) { best = d; *out = p; found = 1; }
    }
    return found;
}

static void r27_esp_tick(void)
{
    if (!g_cfg.esp) return;

    float m[16]; vec3 me;
    if (!r27_matrix(m) || !r27_cam_pos(&me)) return;

    for (int i = 0; i < r27_count(); i++) {
        vec3 p; vec2 sp;
        if (!r27_ent_pos(i, &p)) continue;
        if (v3_len(v3_sub(p, me)) > g_cfg.esp_max_dist) continue;
        if (!r27_w2s(m, p, 1920.0f, 1080.0f, &sp)) continue;

        if (g_cfg.esp_box) r27_rect(sp.x - 30, sp.y - 90, 60, 120, C_BOX);
        if (!g_cfg.esp_skeleton) continue;

        vec3 bones[R27_MAX_BONES]; vec2 pts[R27_MAX_BONES];
        int n = r27_ent_bones(i, bones);
        for (int b = 0; b < n; b++)
            if (!r27_w2s(m, bones[b], 1920.0f, 1080.0f, &pts[b])) pts[b].x = -1;

        for (size_t l = 0; l < sizeof(s_links)/sizeof(s_links[0]); l++) {
            int a = s_links[l][0], c = s_links[l][1];
            if (a >= n || c >= n || pts[a].x < 0 || pts[c].x < 0) continue;
            r27_line(pts[a].x, pts[a].y, pts[c].x, pts[c].y, C_BONE);
        }
    }
}

static void r27_aim_tick(int fire)
{
    if (!g_cfg.aimbot || !fire) return;

    vec3 t, me;
    if (!r27_target(&t, g_cfg.aimbot_fov * 3.0f) || !r27_cam_pos(&me)) return;

    float tp, ty, cp, cy;
    r27_angles(v3_sub(t, me), &tp, &ty);
    r27_get_angles(&cp, &cy);

    float s = g_cfg.aimbot_smooth < 1.0f ? 1.0f : g_cfg.aimbot_smooth;
    r27_set_angles(cp + (tp - cp) / s, cy + (ty - cy) / s);
}

static void r27_silent_tick(void)
{
    if (!g_cfg.silent) return;

    vec3 t, me;
    if (!r27_target(&t, g_cfg.silent_fov * 3.0f) || !r27_cam_pos(&me)) return;

    float tp, ty;
    r27_angles(v3_sub(t, me), &tp, &ty);
    r27_set_angles(tp, ty);
}

static uintptr_t s_recoil_addr;
static float s_recoil_orig;

static void r27_norecoil(int on)
{
    if (!on) {
        if (s_recoil_addr) { r27_write(s_recoil_addr, &s_recoil_orig, 4); s_recoil_addr = 0; }
        return;
    }
    if (s_recoil_addr || !g_game.recoil_mul) return;
    float zero = 0.0f;
    if (!r27_read(g_game.recoil_mul, &s_recoil_orig, 4)) return;
    if (!r27_write(g_game.recoil_mul, &zero, 4)) return;
    s_recoil_addr = g_game.recoil_mul;
}

/* ================= MAIN ================= */
static volatile int s_run;
static pthread_t    s_thr;
static uint64_t     s_tap;

static int r27_pad(uint32_t *btn)
{
    ScePadData d;
    int32_t h = scePadOpen(0xFE, 0, 0, NULL);
    if (h < 0) return 0;
    int ok = scePadReadState(h, &d) == 0;
    scePadClose(h);
    if (!ok) return 0;
    *btn = d.buttons;
    return 1;
}

static void r27_double_tap(uint32_t btn)
{
    static uint32_t prev;
    int down = (btn & PAD_TOUCH) != 0;
    int was  = (prev & PAD_TOUCH) != 0;
    prev = btn;
    if (!down || was) return;

    uint64_t now = sceKernelGetProcessTimeLow();
    if (s_tap && (now - s_tap) <= 350000ULL) {
        s_tap = 0;
        g_cfg.menu_open = !g_cfg.menu_open;
        if (!g_cfg.menu_open) r27_cfg_save();
        r27_notify(g_cfg.menu_open ? "27 Menu: APERTO" : "27 Menu: CHIUSO");
        return;
    }
    s_tap = now;
}

static void *r27_thread(void *arg)
{
    (void)arg;
    r27_sleep_ms(3000);
    r27_notify("27 Menu v1.0 attivo - doppio tap touchpad");

    int waited = 0;
    while (s_run && waited < 120) {
        if (r27_resolve()) break;
        r27_sleep_ms(500);
        waited++;
    }
    if (s_resolved) r27_notify("27 Menu: pronto in partita");
    else            r27_log("gioco non risolto");

    r27_render_init();

    while (s_run) {
        if (!s_resolved) { r27_sleep_ms(500); r27_resolve(); continue; }

        uint32_t btn = 0;
        if (!r27_pad(&btn)) { r27_sleep_ms(50); continue; }

        r27_double_tap(btn);
        if (g_cfg.menu_open) r27_menu_update(btn);

        r27_aim_tick((btn & PAD_R2) != 0);
        r27_silent_tick();
        r27_norecoil(g_cfg.norecoil);

        if (g_cfg.menu_open) r27_menu_draw();
        r27_esp_tick();
        r27_crosshair();

        r27_sleep_ms(8);
    }
    return NULL;
}

int32_t attr_public plugin_load(int32_t argc, const char *argv[])
{
    mkdir(R27_PATH, 0777);
    r27_log("avvio");
    r27_cfg_load();
    s_run = 1;
    if (pthread_create(&s_thr, NULL, r27_thread, NULL) != 0) { s_run = 0; return -1; }
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char *argv[])
{
    s_run = 0;
    r27_cfg_save();
    r27_notify("27 Menu scaricato");
    return 0;
}

int32_t attr_hidden module_start(int64_t argc, const void *args) { return 0; }
int32_t attr_hidden module_stop(int64_t argc, const void *args)  { return 0; }
