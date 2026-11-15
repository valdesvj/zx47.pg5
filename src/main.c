/*
 * ZX47 for the SwissMicros DM42n (DMCP5 native program, .pg5): a 48K ZX Spectrum.
 * (C) 2026 Victor Valdes (valdes.vj@gmail.com) - GPL-3.0-or-later
 *
 * The screen: the Spectrum's 256x192 picture 1:1 in the middle of the LCD, a status line above it (layer, speed,
 * the file), a key hint below. Only the lines that changed go to the LCD (screen.c).
 * The loop: read the two keys held (DMCP's read_key), run one 50 Hz frame (zx.c), draw, wait for the next 20 ms.
 * EXIT opens the menu: load a .Z80 / .SNA / .TAP from /ZX47, save a snapshot, reset, the picture and sound
 * settings, the joystick, off. + changes the key layer (ALPHA, NUM, JOY).
 * The ROM is Amstrad's 48K ROM (roms/README.md: distributed with their permission).
 */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zx.h"
#include "screen.h"
#include "keys.h"

#define DIR       "/ZX47"
#define CFG_FILE  DIR "/ZX47.CFG"
#define MAX_FILE  (320u * 1024u)                    /* the largest tape kept in memory */
#define FRAME_MS  20

typedef struct { int mode, invert, sound, joy, speed, layer; } Cfg;
static Cfg cfg = { MONO_COLOUR, 0, 1, JOY_KEMPSTON, 0, LAYER_ALPHA };

static Zx X;
static uint8_t *tape;                               /* the inserted .TAP (kept while it is in) */
static char loaded[40] = "";                        /* the last file's name, for the status line and saving */
static char msg[64] = "";                           /* the menu's message line */

/* ------------------------------------------------------------------ text */
static void text(int x, int y, const char *s, int inv) {
  t20->inv = inv;
  lcd_setXY(t20, x, y);
  lcd_writeText(t20, s);
  t20->inv = 0;
}

static void bar(const char *s) { lcd_fill_rect(0, 0, LCD_X, 22, 1); text(4, 1, s, 1); }

/* ------------------------------------------------------------------ settings */
static void cfg_save(void) {
  if (!sys_disk_ok()) return;
  char s[96];
  int n = sprintf(s, "mode=%d\ninvert=%d\nsound=%d\njoy=%d\nspeed=%d\nlayer=%d\n",
                  cfg.mode, cfg.invert, cfg.sound, cfg.joy, cfg.speed, cfg.layer);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw;
  if (f_open(&f, CFG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, s, (UINT)n, &bw); f_close(&f); }
  sys_disk_write_enable(0);
}

static void cfg_load(void) {
  FIL f; UINT br;
  char s[128];
  if (f_open(&f, CFG_FILE, FA_READ) != FR_OK) return;
  f_read(&f, s, sizeof s - 1, &br);
  f_close(&f);
  s[br] = 0;
  static const struct { const char *k; int *v, max; } T[] = {
    {"mode", &cfg.mode, 1}, {"invert", &cfg.invert, 1}, {"sound", &cfg.sound, 1}, {"joy", &cfg.joy, JOY_CNT - 1},
    {"speed", &cfg.speed, 1}, {"layer", &cfg.layer, LAYER_CNT - 1} };
  for (unsigned i = 0; i < sizeof T / sizeof *T; i++) {
    char key[16];
    sprintf(key, "%s=", T[i].k);
    char *p = strstr(s, key);
    if (p) { int v = atoi(p + strlen(key)); if (v >= 0 && v <= T[i].max) *T[i].v = v; }
  }
}

/* ------------------------------------------------------------------ the frame around the picture */
static char status_now[96];
static int emu_ms, scr_ms;                          /* the last second: ms a frame (x10) running, drawing */
static char note[40];                               /* a short message in the status line ... */
static uint32_t note_until;                         /* ... until this time */

static void status(int pct) {
  char s[96];
  if (note[0] && (int32_t)(sys_current_ms() - note_until) < 0) {
    snprintf(s, sizeof s, "ZX47  %s", note);
    if (strcmp(s, status_now)) {
      strcpy(status_now, s);
      lcd_refresh_wait();
      lcd_fill_rect(0, 0, LCD_X, SCR_Y - 1, 0);
      text(4, 2, s, 0);
      lcd_refresh_lines(0, SCR_Y - 1);
    }
    return;
  }
  note[0] = 0;
  if (cfg.speed) snprintf(s, sizeof s, "%s %d%% z%d.%d s%d.%d %.20s", LAYER_NAME[cfg.layer], pct, emu_ms / 10, emu_ms % 10,
                         scr_ms / 10, scr_ms % 10, loaded);
  else snprintf(s, sizeof s, "ZX47  %-5s  %.30s", LAYER_NAME[cfg.layer], loaded);
  if (cfg.layer == LAYER_JOY && !cfg.speed) sprintf(s + strlen(s), "  %s", JOY_NAME[cfg.joy]);
  if (!strcmp(s, status_now)) return;
  strcpy(status_now, s);
  lcd_refresh_wait();                               /* the picture's last lines may still be going out */
  lcd_fill_rect(0, 0, LCD_X, SCR_Y - 1, 0);
  text(4, 2, s, 0);
  lcd_refresh_lines(0, SCR_Y - 1);
}

static void frame_ui(void) {                        /* the whole LCD: blank margins, status, hint */
  lcd_refresh_wait();
  lcd_clear_buf();
  status_now[0] = 0;
  text(4, SCR_Y + SCR_H + 2, "EXIT menu   + ALPHA/NUM/JOY", 0);
  lcd_forced_refresh();
  lcd_refresh_wait();
  screen_invalidate();
  X.cpu.watched = 1;
}

/* ------------------------------------------------------------------ files */
static uint8_t *read_file(const char *path, uint32_t *n, uint32_t max) {
  FIL f;
  if (f_open(&f, path, FA_READ) != FR_OK) return NULL;
  uint32_t size = (uint32_t)f_size(&f);
  if (!size || size > max) { f_close(&f); *n = size; return NULL; }
  uint8_t *b = malloc(size);
  UINT br = 0;
  if (b) f_read(&f, b, size, &br);
  f_close(&f);
  if (b && br != size) { free(b); b = NULL; }
  *n = size;
  return b;
}

static void set_loaded(const char *fname) {
  strncpy(loaded, fname, sizeof loaded - 1);
  loaded[sizeof loaded - 1] = 0;
}

/* typing LOAD "" ENTER into a booted Spectrum: each step held 4 frames, then 4 frames up */
static const uint8_t LOAD_KEYS[][2] = { {ZK_J, ZK_NONE}, {ZK_SYM, ZK_P}, {ZK_SYM, ZK_P}, {ZK_ENTER, ZK_NONE} };
static int autotype = -1;                           /* the frame count into LOAD_KEYS, -1 = none */
static int load_ok;

static int load_cb(const char *fpath, const char *fname, void *data) {
  int kind = *(int *)data;                          /* 0 .Z80, 1 .SNA, 2 .TAP */
  uint32_t n;
  uint8_t *b = read_file(fpath, &n, kind == 2 ? MAX_FILE : 200000);
  if (!b) { sprintf(msg, n ? "%s: too big or no memory" : "%s: cannot read", fname); return MRET_EXIT; }
  int e = 0;
  if (kind == 2) {
    if (tape) free(tape);
    tape = b;
    zx_reset(&X);
    for (int i = 0; i < 150; i++) zx_frame(&X);     /* boot (3 s of Spectrum time, much less here) */
    zx_tape(&X, tape, n);
    autotype = 0;
    sprintf(msg, "%s in: LOAD \"\" typed", fname);
  } else {
    e = kind == 0 ? zx_load_z80(&X, b, n) : zx_load_sna(&X, b, n);
    free(b);
    if (e) sprintf(msg, "%s: %s", fname, zx_error(e));
    else sprintf(msg, "%s loaded", fname);
  }
  if (!e) set_loaded(fname);
  load_ok = !e;
  return MRET_EXIT;
}

static int load(int kind) {                         /* 1: a file was loaded */
  static const char *TITLE[3] = { "Load .Z80 snapshot", "Load .SNA snapshot", "Insert .TAP tape" };
  static const char *EXT[3] = { ".z80", ".sna", ".tap" };
  load_ok = 0;
  check_create_dir(DIR);
  file_selection_screen(TITLE[kind], DIR, EXT[kind], load_cb, 0, 0, &kind);
  wait_for_key_release(-1);
  return load_ok;
}

static void save(void) {
  char path[64], base[40];
  strcpy(base, loaded[0] ? loaded : "ZX47");
  char *dot = strrchr(base, '.');
  if (dot) *dot = 0;
  snprintf(path, sizeof path, DIR "/%s-SAVE.SNA", base);
  uint8_t *b = malloc(ZX_SNA_SIZE);
  if (!b) { strcpy(msg, "save: no memory"); return; }
  zx_save_sna(&X, b);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw = 0;
  if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, b, ZX_SNA_SIZE, &bw); f_close(&f); }
  sys_disk_write_enable(0);
  free(b);
  snprintf(msg, sizeof msg, bw == ZX_SNA_SIZE ? "saved %s" : "NOT saved %s", path + sizeof DIR);
}

/* ------------------------------------------------------------------ the menu */
static int read_keys(int *k1, int *k2) {
  *k1 = *k2 = 0;
  __sysfn_read_key(k1, k2);
  if (*k1 < 0) *k1 = 0;
  if (*k2 < 0) *k2 = 0;
  return (*k1 > 0) + (*k2 > 0);
}

static void wait_release(void) { int a, b; while (read_keys(&a, &b)) sys_delay(10); key_pop_all(); }

static void keytest(void) {
  lcd_clear_buf();
  bar("Key test: the keys held (EXIT twice ends)");
  lcd_refresh();
  int exits = 0, was = -1;
  for (;;) {
    int a, b;
    read_keys(&a, &b);
    int now = a * 100 + b;
    if (now != was) {
      char s[48];
      sprintf(s, "k1 = %2d   k2 = %2d", a, b);
      lcd_fill_rect(0, 60, LCD_X, 24, 0);
      text(20, 62, s, 0);
      lcd_refresh_lines(60, 24);
      lcd_refresh_wait();
      if (a == KEY_EXIT && was != now) exits++;
      was = now;
    }
    if (exits >= 2 && !a) break;
    sys_delay(20);
    reset_auto_off();
  }
  key_pop_all();
}

/* ------------------------------------------------------------------ About, and DMCP's setup menu (as Chess 47) */
static void about(void) {
  char s[64];
  lcd_clear_buf();
  bar("ZX47 v" PROGRAM_VERSION "   a 48K ZX Spectrum for the DM42n");
  int y = 26;
  static const char *L[] = {
    "(C) 2026 Victor Valdes - GPL-3.0-or-later",
    "Z80 core: superzazu/z80 (MIT)",
    "48K ROM (C) Amstrad, with their permission",
    "DMCP5 SDK (BSD-3, NOMAS)",
  };
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++) { text(4, y, L[i], 0); y += 21; }
  y += 4;
  sprintf(s, "Free heap: %d KB  (largest %d KB)", sys_free_mem() / 1024, sys_largest_free_mem() / 1024);
  text(4, y, s, 0); y += 21;
  sprintf(s, "CPU: %s   LCD: %s", usb_powered() ? "160 MHz (USB)" : "80 MHz (battery)",
          screen_fast() ? "fast" : "slow path");
  text(4, y, s, 0); y += 21;
  text(4, y + 4, "Written with the help of AI (see README)", 0);
  lcd_refresh();
  lcd_refresh_wait();
  wait_release();
  for (;;) {
    if (key_empty()) { sys_timer_start(0, 500); CLR_ST(STAT_RUNNING); sys_sleep(); }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) return;
    int k = key_pop();
    if (k == KEY_EXIT || k == KEY_ENTER) break;
  }
  wait_release();
}

#define MI_ZX47_ABOUT 1
static const uint8_t setup_items[] = {
  MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, MI_ZX47_ABOUT, 0 };
static const smenu_t SETUP_MENU = { "ZX47 Setup", setup_items, NULL, NULL };

static int run_menu_item(uint8_t id) {
  if (id == MI_ZX47_ABOUT) { about(); return 0; }
  return MRET_UNIMPL;
}

static const char *menu_line_str(uint8_t id, char *s, const int slen) {
  (void)s; (void)slen;
  return id == MI_ZX47_ABOUT ? "About ZX47 >" : NULL;
}

static void menu_draw(void) {
  char s[64];
  lcd_clear_buf();
  bar("ZX47 " PROGRAM_VERSION "  menu        EXIT: back");
  static const char *MODE[2] = { "colour", "ink" };
  const char *L[8];
  char l[6][48];
  sprintf(l[0], "1 picture: %s", MODE[cfg.mode]);
  sprintf(l[1], "2 invert: %s", cfg.invert ? "on" : "off");
  sprintf(l[2], "3 sound: %s", cfg.sound ? "on" : "off");
  sprintf(l[3], "4 joystick: %s", JOY_NAME[cfg.joy]);
  sprintf(l[4], "5 speed shown: %s", cfg.speed ? "on" : "off");
  sprintf(l[5], "6 key test  7 setup (USB disk)  8 about");
  for (int i = 0; i < 6; i++) L[i] = l[i];
  text(4, 26, "Files in " DIR ":  F1 .Z80  F2 .SNA  F3 .TAP", 0);
  for (int i = 0; i < 6; i++) text(4, 50 + i * 21, L[i], 0);
  sprintf(s, "%s", msg[0] ? msg : (loaded[0] ? loaded : "48K BASIC"));
  text(4, 50 + 6 * 21 + 2, s, 0);
  static const char *K[6] = { "Z80", "SNA", "TAP", "SAVE", "RESET", "OFF" };
  lcd_draw_menu_keys(K);
  lcd_refresh();
  lcd_refresh_wait();
}

/* returns 1 to go on, 0 to turn off */
static int menu(void) {
  lcd_refresh_wait();
  stop_buzzer();
  wait_release();
  msg[0] = 0;
  menu_draw();
  for (;;) {
    if (key_empty()) { sys_timer_start(0, 200); CLR_ST(STAT_RUNNING); sys_sleep(); }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) return 0;
    int k = key_pop();
    if (k <= 0 || k == KEY_DOUBLE_RELEASE) continue;
    switch (k) {
    case KEY_EXIT: case KEY_ENTER: cfg_save(); wait_release(); return 1;
    case KEY_F1: case KEY_F2: case KEY_F3:
      if (load(k - KEY_F1)) { cfg_save(); wait_release(); return 1; }   /* loaded: play */
      break;
    case KEY_F4: save(); break;
    case KEY_F5: zx_reset(&X); zx_tape(&X, tape, 0); loaded[0] = 0; strcpy(msg, "reset"); break;
    case KEY_F6: cfg_save(); return 0;
    case KEY_1: cfg.mode ^= 1; break;
    case KEY_2: cfg.invert ^= 1; break;
    case KEY_3: cfg.sound ^= 1; break;
    case KEY_4: cfg.joy = (cfg.joy + 1) % JOY_CNT; break;
    case KEY_5: cfg.speed ^= 1; break;
    case KEY_6: keytest(); break;
    case KEY_7: wait_release(); handle_menu(&SETUP_MENU, MENU_RESET, 0); wait_release(); break;
    case KEY_8: about(); break;
    default: continue;
    }
    menu_draw();
  }
}

/* ------------------------------------------------------------------ sound: the beeper's edges a frame -> a tone */
static uint32_t tone;

static void sound(void) {
  uint32_t f = (uint32_t)X.toggles * 25;            /* edges in 20 ms: 2 edges a period */
  if (!cfg.sound || ST(STAT_BEEP_MUTE) || X.toggles < 4 || f < 60 || f > 9000) {
    if (tone) { stop_buzzer(); tone = 0; }
    return;
  }
  uint32_t d = f > tone ? f - tone : tone - f;
  if (!tone || d * 16 > tone) { start_buzzer_freq(f * 1000); tone = f; }
}

/* ------------------------------------------------------------------ screenshot: the LCD as a .bmp in /SCREENS (DMCP) */
static void screenshot(void) {
  lcd_refresh_wait();
  status_now[0] = 0;                                /* the status line as it was in the picture, then the note */
  int r = create_screenshot(1);
  strcpy(note, r < 0 ? "screenshot NOT saved" : "screenshot saved in /SCREENS");
  note_until = sys_current_ms() + 2000;
}

/* ------------------------------------------------------------------ power, as the DMCP SDK's template */
static void power_off(void) {                       /* the Spectrum keeps its state while off */
  lcd_refresh_wait();
  stop_buzzer(); tone = 0;
  SET_ST(STAT_PGM_END);
  if (!ST(STAT_SUSPENDED)) {
    lcd_set_buf_cleared(0);
    draw_power_off_image(1);
    LCD_power_off(0);
    SET_ST(STAT_SUSPENDED);
    SET_ST(STAT_OFF);
  }
  while (ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) { CLR_ST(STAT_RUNNING); sys_sleep(); }   /* ON clears them */
  SET_ST(STAT_RUNNING);
  if (ST(STAT_OFF)) { LCD_power_on(); rtc_wakeup_delay(); CLR_ST(STAT_OFF); }
  key_pop_all();
  frame_ui();
}

/* ------------------------------------------------------------------ the main loop */
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  switch_usb_powered_freq();
  int e = zx_init(&X);
  int s = e ? -1 : screen_init();
  if (e || s == -1) {
    lcd_clear_buf();
    text(4, 40, "ZX47: not enough memory", 0);
    lcd_refresh();
    for (;;) wait_for_key_press();
  }
  run_menu_item_app = run_menu_item;
  menu_line_str_app = menu_line_str;
  cfg_load();
  frame_ui();
  if (s) { strcpy(msg, "LCD layout not learnt: slow drawing"); }

  uint32_t next = sys_current_ms(), last_auto = next, sec = next;
  int frames_sec = 0, pct = 100, skipped = 0, plus_was = 0;
  uint32_t ms_emu = 0, ms_scr = 0;
  for (;;) {
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) power_off();
    if (ST(STAT_POWER_CHANGE)) { switch_usb_powered_freq(); CLR_ST(STAT_POWER_CHANGE); }
    SET_ST(STAT_RUNNING);

    int k1, k2;
    read_keys(&k1, &k2);
    int shot = 0;                                   /* the keys go by read_key; DMCP's queue is emptied, */
    for (int q = key_pop(); q >= 0; q = key_pop())  /* but its screenshot key is kept */
      if (q == KEY_SCREENSHOT) shot = 1;
    if (k1 == KEY_EXIT || k2 == KEY_EXIT) {
      if (!menu()) power_off();
      else frame_ui();
      next = sys_current_ms();
      continue;
    }
    int plus = k1 == KEY_ADD || k2 == KEY_ADD;
    int shift = k1 == KEY_SHIFT || k2 == KEY_SHIFT;
    if (plus && !plus_was) {
      if (shift) shot = 1;                          /* SHIFT + +: a screenshot */
      else cfg.layer = (cfg.layer + 1) % LAYER_CNT;
    }
    if (shot) { screenshot(); next = sys_current_ms(); }
    if (plus && shift) k1 = k2 = 0;                 /* no CAPS SHIFT to the Spectrum for it */
    plus_was = plus;
    if (plus) { if (k1 == KEY_ADD) k1 = 0; if (k2 == KEY_ADD) k2 = 0; }

    if (autotype >= 0) {                            /* LOAD "" being typed */
      int step = autotype / 8, held = autotype % 8 < 4;
      zx_keys_clear(&X);
      if (held) { zx_key(&X, LOAD_KEYS[step][0], 1); zx_key(&X, LOAD_KEYS[step][1], 1); }
      if (++autotype >= (int)(sizeof LOAD_KEYS / sizeof *LOAD_KEYS) * 8) autotype = -1;
    } else keys_apply(&X, k1, k2, cfg.layer, cfg.joy);

    uint32_t e0 = sys_current_ms();
    zx_frame(&X);
    ms_emu += sys_current_ms() - e0;
    sound();

    uint32_t now = sys_current_ms();
    int late = (int32_t)(now - next) > FRAME_MS;    /* behind: draw only every other frame (up to 3 skipped) */
    int need = X.cpu.watched || (X.frames & 15) == 0;   /* the screen was written, or FLASH changes */
    if (need && (!late || skipped >= 3)) {
      uint32_t r0 = sys_current_ms();
      X.cpu.watched = 0;
      screen_render(&X, cfg.mode, cfg.invert);
      ms_scr += sys_current_ms() - r0;
      skipped = 0;
    } else if (need) skipped++;
    frames_sec++;

    now = sys_current_ms();
    if (now - sec >= 1000) {
      pct = (int)(frames_sec * 100000u / ((now - sec) * 50u));
      emu_ms = frames_sec ? (int)(ms_emu * 10 / (uint32_t)frames_sec) : 0;   /* tenths of a ms a frame */
      scr_ms = frames_sec ? (int)(ms_scr * 10 / (uint32_t)frames_sec) : 0;
      frames_sec = 0; sec = now; ms_emu = ms_scr = 0;
    }
    status(pct);
    if (now - last_auto > 10000) { reset_auto_off(); last_auto = now; }

    next += FRAME_MS;                               /* 50 frames a second, as the Spectrum */
    if ((int32_t)(now - next) > 200) next = now;    /* far behind: do not try to catch up */
    while ((int32_t)(next - sys_current_ms()) > 0) {}
  }
}
