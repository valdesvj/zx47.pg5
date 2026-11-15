/*
 * ZX47 PC simulator (from GB47's, TERM47's and Chess 47's): links the program's real C files against a fake DMCP5
 * layer and renders the 400x240 LCD. Writes straight into the LCD line buffer (lcd_line_addr) are kept, as the
 * emulator draws that way. Time is real: the emulator runs at 50 frames a second here too.
 *
 * usage: sim KEY KEY ... SNAP:name ...
 *   keys: F1..F6 0..9 ENTER EXIT BSP UP DOWN SHIFT SIGMA ... (as dmcp.h without KEY_), RS, DOT, DIV, MUL, ADD
 *   K1+K2       two keys held together (DOT+COS: SYMBOL SHIFT + K)
 *   WAIT:ms     real time passes
 *   SNAP:name   writes name.pbm (framebuffer) and name.txt (the DMCP-font text: the menu bar), for render.py
 * read_key (the emulator's keys) holds each scripted key for 6 calls (frames), then 6 calls up; key_pop (the menu)
 * takes them as presses. ZX47_FILE=/ZX47/NAME.TAP: the file the file selection screen picks (if its type matches).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "dmcp.h"

sys_sdb_t sim_sdb;
static disp_stat_t ds20, ds24, dsReg;
static line_font_t f20 = { "t20", 9, 19, 15, 32, 96, 1, 1, 0, 0 };
static line_font_t f24 = { "t24", 11, 23, 18, 32, 96, 1, 1, 0, 0 };

static uint8_t fb[LCD_Y][LCD_X];
typedef struct { int font, x, y, inv; char s[64]; } Txt;
static Txt txt[600];
static int ntxt;

static char **script;
static int nscript, spos;

/* ---------------- LCD ---------------- */
void sim_sync(void);
static uint8_t touched[LCD_Y];
void lcd_clear_buf(void) { memset(fb, 0, sizeof fb); memset(touched, 0, sizeof touched); ntxt = 0; }
static void sim_lcd_out(int forced) { (void)forced; }
void lcd_refresh(void) { sim_sync(); sim_lcd_out(0); }
void lcd_refresh_wait(void) {}
void lcd_refresh_lines(int ln, int cnt) { (void)ln; (void)cnt; sim_sync(); sim_lcd_out(0); }
void lcd_forced_refresh(void) { sim_sync(); sim_lcd_out(1); }
void lcd_set_buf_cleared(int v) { (void)v; }
int  lcd_get_buf_cleared(void) { return 0; }
void LCD_power_on(void) {}
void LCD_power_off(int c) { (void)c; }
void draw_power_off_image(int a) { (void)a; }

static void kill_text_in(int x, int y, int w, int h) {
  for (int i = 0; i < ntxt; i++) {
    Txt *t = &txt[i];
    int th = t->font == 24 ? 23 : 19;
    if (t->x >= x && t->y >= y && t->x < x + w && t->y + th <= y + h + 2) t->s[0] = 0;
  }
}

void lcd_fill_rect(uint32_t x, uint32_t y, uint32_t dx, uint32_t dy, int val) {
  sim_sync();
  for (uint32_t j = y; j < y + dy && j < LCD_Y; j++)
    for (uint32_t i = x; i < x + dx && i < LCD_X; i++) fb[j][i] = (uint8_t)(val ? 1 : 0);
  if (dx > 8 && dy > 8) kill_text_in((int)x, (int)y, (int)dx, (int)dy);
}

static int fw(disp_stat_t *ds) { return ds->f->width; }
int lcd_lineHeight(disp_stat_t *ds) { return ds->f->height; }
int lcd_textWidth(disp_stat_t *ds, const char *t) { return (int)strlen(t) * fw(ds); }
void lcd_setXY(disp_stat_t *ds, int x, int y) { ds->x = (int16_t)x; ds->y = (int16_t)y; }
void lcd_writeText(disp_stat_t *ds, const char *s) {
  if (ntxt < 600) {
    Txt *t = &txt[ntxt++];
    t->font = ds->f == &f24 ? 24 : 20;
    t->x = ds->x; t->y = ds->y; t->inv = ds->inv;
    snprintf(t->s, sizeof t->s, "%s", s);
  }
  ds->x = (int16_t)(ds->x + lcd_textWidth(ds, s));
}
void lcd_draw_menu_keys(const char *keys[]) {
  int y0 = LCD_Y - LCD_MENU_LINES + 2, w = LCD_X / 6;
  for (int i = 0; i < ntxt; i++) if (txt[i].y >= LCD_Y - LCD_MENU_LINES) txt[i].s[0] = 0;
  lcd_fill_rect(0, LCD_Y - LCD_MENU_LINES, LCD_X, LCD_MENU_LINES, 0);
  for (int i = 0; i < 6; i++) {
    int x0 = i * w + 2;
    lcd_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)(w - 4), LCD_MENU_LINES - 4, 1);
    if (keys[i] && keys[i][0] && ntxt < 600) {
      Txt *t = &txt[ntxt++];
      t->font = 20; t->inv = 2; t->y = y0 + 5;
      t->x = x0 + (w - 4 - (int)strlen(keys[i]) * 9) / 2;
      snprintf(t->s, sizeof t->s, "%s", keys[i]);
    }
  }
}

/* ---------------- time / power ---------------- */
uint32_t sys_current_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
uint32_t get_rtc_ticks(void) { return 12345; }
void sys_delay(uint32_t ms) { (void)ms; }
static uint32_t timer_ms;
void sys_timer_start(int ix, uint32_t ms) { (void)ix; timer_ms = ms; }
void start_buzzer_freq(uint32_t f) { (void)f; }
void stop_buzzer(void) {}
int  switch_usb_powered_freq(void) { return 0; }
int  usb_powered(void) { return 1; }
/* the DMCP line buffer with its own oddities (offset, mirrored, LSB first, inverted), as Chess 47's simulator.
   A program may write into it (FPS47 does): a line handed out stays the truth until the next drawing call,
   refresh or snapshot reads it back into fb (sim_sync). */
static uint8_t lb[LCD_Y][54];
void sim_sync(void) {
  for (int y = 0; y < LCD_Y; y++) {
    if (!touched[y]) continue;
    touched[y] = 0;
    for (int x = 0; x < LCD_X; x++) { int X = LCD_X - 1 - x; fb[y][x] = !(lb[y][2 + X / 8] >> (X % 8) & 1); }
  }
}
uint8_t *lcd_line_addr(int y) {
  if (!touched[y]) {
    memset(lb[y], 0xFF, 54);
    for (int x = 0; x < LCD_X; x++)
      if (fb[y][x]) { int X = LCD_X - 1 - x; lb[y][2 + X / 8] &= (uint8_t)~(1u << (X % 8)); }
    touched[y] = 1;
  }
  return lb[y];
}
uint32_t read_power_voltage(void) { return 3000; }
void reset_auto_off(void) {}
void rtc_wakeup_delay(void) {}
int  sys_free_mem(void) { return 600 * 1024; }
int  sys_largest_free_mem(void) { return 560 * 1024; }
void sys_reset(void) { exit(3); }
int  handle_menu(const smenu_t *m, int a, int l) { (void)m; (void)a; (void)l; return 0; }
void wait_for_key_release(int t) { (void)t; }

/* ---------------- files -> ./simdisk ---------------- */
static void path(const char *p, char *o) { snprintf(o, 256, "simdisk%s", p); }
int sys_disk_ok(void) { return 1; }
int sys_disk_write_enable(int v) { (void)v; return 1; }
int check_create_dir(const char *d) { char p[256], c[300]; path(d, p); snprintf(c, sizeof c, "mkdir -p %s", p); return system(c); }
int file_exists(const char *f) { char p[256]; path(f, p); FILE *x = fopen(p, "rb"); if (x) fclose(x); return x != NULL; }
FRESULT f_open(FIL *fp, const TCHAR *p, BYTE mode) {
  char q[256]; path(p, q);
  FILE *f = fopen(q, (mode & FA_WRITE) ? "wb" : "rb");
  fp->obj.fs = (FATFS *)f;
  if (f && !(mode & FA_WRITE)) { fseek(f, 0, SEEK_END); fp->obj.objsize = (FSIZE_t)ftell(f); fseek(f, 0, SEEK_SET); }
  return f ? FR_OK : FR_NO_FILE;
}
FRESULT f_close(FIL *fp) { fclose((FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_read(FIL *fp, void *b, UINT n, UINT *br) { *br = (UINT)fread(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }
FRESULT f_write(FIL *fp, const void *b, UINT n, UINT *bw) { *bw = (UINT)fwrite(b, 1, n, (FILE *)fp->obj.fs); return FR_OK; }
int file_selection_screen(const char *title, const char *base_dir, const char *ext, file_sel_fn_t sel_fn,
                          int disp_new, int overwrite_check, void *data) {
  (void)title; (void)base_dir; (void)disp_new; (void)overwrite_check;
  const char *f = getenv("ZX47_FILE");
  if (!f) return 0;
  size_t lf = strlen(f), le = strlen(ext);
  if (lf < le || strcasecmp(f + lf - le, ext)) return 0;
  const char *n = strrchr(f, '/');
  return sel_fn(f, n ? n + 1 : f, data);
}

/* ---------------- keys from the script ---------------- */
static void snap(const char *name) {
  sim_sync();
  char fn[256];
  snprintf(fn, sizeof fn, "%s.pbm", name);
  FILE *f = fopen(fn, "w");
  fprintf(f, "P1\n%d %d\n", LCD_X, LCD_Y);
  for (int y = 0; y < LCD_Y; y++) { for (int x = 0; x < LCD_X; x++) fputc(fb[y][x] ? '1' : '0', f); fputc('\n', f); }
  fclose(f);
  snprintf(fn, sizeof fn, "%s.txt", name);
  f = fopen(fn, "w");
  for (int i = 0; i < ntxt; i++) if (txt[i].s[0]) fprintf(f, "%d\t%d\t%d\t%d\t%s\n", txt[i].font, txt[i].x, txt[i].y, txt[i].inv, txt[i].s);
  fclose(f);
  fprintf(stderr, "snap %s\n", name);
}

static int parse_key(const char *s) {
  static const struct { const char *n; int k; } map[] = {
    {"F1",KEY_F1},{"F2",KEY_F2},{"F3",KEY_F3},{"F4",KEY_F4},{"F5",KEY_F5},{"F6",KEY_F6},
    {"0",KEY_0},{"1",KEY_1},{"2",KEY_2},{"3",KEY_3},{"4",KEY_4},{"5",KEY_5},{"6",KEY_6},{"7",KEY_7},{"8",KEY_8},{"9",KEY_9},
    {"ENTER",KEY_ENTER},{"EXIT",KEY_EXIT},{"BSP",KEY_BSP},{"UP",KEY_UP},{"DOWN",KEY_DOWN},{"SHIFT",KEY_SHIFT},
    {"SIGMA",KEY_SIGMA},{"ADD",KEY_ADD},{"SUB",KEY_SUB},{"MUL",KEY_MUL},{"DIV",KEY_DIV},{"DOT",KEY_DOT},{"RS",KEY_RUN},
    {"INV",KEY_INV},{"SQRT",KEY_SQRT},{"LOG",KEY_LOG},{"LN",KEY_LN},{"XEQ",KEY_XEQ},{"STO",KEY_STO},{"RCL",KEY_RCL},
    {"RDN",KEY_RDN},{"SIN",KEY_SIN},{"COS",KEY_COS},{"TAN",KEY_TAN},{"SWAP",KEY_SWAP},{"CHS",KEY_CHS},{"E",KEY_E},
  };
  for (unsigned i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(map[i].n, s)) return map[i].k;
  fprintf(stderr, "sim: unknown key %s\n", s);
  return -1;
}

static int queue[4096], queue2[4096], nq, qpos;
static void push2(int k, int k2) { if (nq < 4096) { queue[nq] = k; queue2[nq] = k2; nq++; } }
static void push(int k) { push2(k, 0); }

static uint32_t wait_until;
/* advance the script until a key is available (handles SNAP/WAIT/TYPE) */
static void fill(void) {
  while (qpos >= nq && spos < nscript && sys_current_ms() >= wait_until) {
    const char *t = script[spos++];
    nq = qpos = 0;
    if (!strncmp(t, "SNAP:", 5)) snap(t + 5);
    else if (!strncmp(t, "WAIT:", 5)) { wait_until = sys_current_ms() + (uint32_t)atoi(t + 5); return; }
    else if (strchr(t, '+') && strcmp(t, "+")) {                       /* K1+K2 held together */
      char a[32]; snprintf(a, sizeof a, "%s", t);
      char *p = strchr(a, '+'); *p = 0;
      int k1 = parse_key(a), k2 = parse_key(p + 1);
      if (k1 > 0 && k2 > 0) push2(k1, k2);
    }
    else { int k = parse_key(t); if (k > 0) push(k); }
  }
}
static int extra = -1;                     /* a key from the serial console (77: the PC sent L) */
void sim_push_key(int k) { extra = k; }
static int done(void) { return qpos >= nq && spos >= nscript && sys_current_ms() >= wait_until; }
int key_empty(void) { if (extra > 0) return 0; fill(); return qpos >= nq; }
int key_pop(void) { if (extra > 0) { int k = extra; extra = -1; return k; } fill(); return qpos < nq ? queue[qpos++] : -1; }
void key_pop_all(void) {}
int create_screenshot(int report_error) {            /* the LCD as it is now, as SCREENS/shotN.pbm in the sim's disk */
  (void)report_error;
  static int n;
  char name[64];
  system("mkdir -p simdisk/SCREENS");
  snprintf(name, sizeof name, "simdisk/SCREENS/shot%d", n++);
  snap(name);
  return 0;
}

/* read_key: the scripted keys held for 6 calls, then 6 calls up */
static int held1, held2, hold, gap;
int __sysfn_read_key(int *k1, int *k2) {
  if (hold > 0) { hold--; if (!hold) gap = 6; *k1 = held1; *k2 = held2; return (held1 > 0) + (held2 > 0); }
  *k1 = *k2 = 0;
  if (gap > 0) { gap--; return 0; }
  fill();
  if (qpos < nq) { held1 = queue[qpos]; held2 = queue2[qpos]; qpos++; hold = 6; *k1 = held1; *k2 = held2; return 1 + (held2 > 0); }
  if (done()) { fprintf(stderr, "script done\n"); exit(0); }
  return 0;
}
void wait_for_key_press(void) { if (key_empty()) exit(0); key_pop(); }
void sys_sleep(void) {
  if (done()) { fprintf(stderr, "script done\n"); exit(0); }
  uint32_t t = timer_ms ? timer_ms : 20;
  timer_ms = 0;
  usleep(t * 1000);
}

void program_main(void);
int main(int argc, char **argv) {
  ds20.f = &f20; ds24.f = &f24; dsReg.f = &f24;
  ds20.lnfill = ds24.lnfill = 1; ds20.newln = ds24.newln = 1;
  sim_sdb.pds_t20 = &ds20; sim_sdb.pds_t24 = &ds24; sim_sdb.pds_fReg = &dsReg;
  script = argv + 1;
  nscript = argc - 1;
  program_main();
  return 0;
}
