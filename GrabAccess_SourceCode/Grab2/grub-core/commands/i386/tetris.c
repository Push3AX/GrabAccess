/*
 * bare-metal-tetris
 * https://github.com/programble/bare-metal-tetris
 * Copyright (C) 2013–2014, Curtis McEnroe programble@gmail.com
 *
 * Permission to use, copy, modify, and/or distribute this software
 * for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission notice
 * appear in all copies.
 */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2019  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>
#include <grub/term.h>
#include <grub/cpu/io.h>

#ifdef GRUB_MACHINE_EFI
#include <grub/efi/api.h>
#include <grub/efi/efi.h>
#include <grub/efi/graphics_output.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

/* Tetris well dimensions */
#define WELL_WIDTH  (10)
#define WELL_HEIGHT (22)
/* Initial interval in milliseconds at which to apply gravity */
#define INITIAL_SPEED (1000)
/* Delay in milliseconds before rows are cleared */
#define CLEAR_DELAY (100)
/* Scoring: score is increased by the product of the current level and a factor
 * corresponding to the number of rows cleared. */
#define SCORE_FACTOR_1 (100)
#define SCORE_FACTOR_2 (300)
#define SCORE_FACTOR_3 (500)
#define SCORE_FACTOR_4 (800)
/* Amount to increase the score for a soft drop */
#define SOFT_DROP_SCORE (1)
/* Factor by which to multiply the number of rows dropped to increase the score
 * for a hard drop */
#define HARD_DROP_SCORE_FACTOR (2)
/* Number of rows that need to be cleared to increase level */
#define ROWS_PER_LEVEL (10)

typedef enum bool
{
  false,
  true
} bool;

/* Timing */

/* Return the number of CPU ticks since boot. */
static inline grub_uint64_t
rdtsc (void)
{
  grub_uint32_t hi, lo;
  asm ("rdtsc" : "=a" (lo), "=d" (hi));
  return ((grub_uint64_t) lo) | (((grub_uint64_t) hi) << 32);
}

/* Return the current second field of the real-time-clock (RTC). Note that the
 * value may or may not be represented in such a way that it should be
 * formatted in hex to display the current second (i.e. 0x30 for the 30th
 * second). */

static grub_uint8_t
rtcs (void)
{
  grub_uint8_t last = 0, sec;
  do
  {
    /* until value is the same twice in a row */
    /* wait for update not in progress */
    do
    {
      grub_outb (0x0A, 0x70);
    }
    while (grub_inb (0x71) & 0x80);
    grub_outb (0x00, 0x70);
    sec = grub_inb (0x71);
  }
  while (sec != last && (last = sec));
  return sec;
}

/* The number of CPU ticks per millisecond */
grub_uint64_t tpms;

/* Set tpms to the number of CPU ticks per millisecond based on the number of
 * ticks in the last second, if the RTC second has changed since the last call.
 * This gets called on every iteration of the main loop in order to provide
 * accurate timing. */
static void
tps (void)
{
  static grub_uint64_t ti = 0;
  static grub_uint8_t last_sec = 0xFF;
  grub_uint8_t sec = rtcs();
  if (sec != last_sec)
  {
    last_sec = sec;
    grub_uint64_t tf = rdtsc();
    tpms = (grub_uint32_t) ((tf - ti) >> 3) / 125; /* Less chance of truncation */
    ti = tf;
  }
}

/* IDs used to keep separate timing operations separate */
enum timer
{
  TIMER_UPDATE,
  TIMER_CLEAR,
  TIMER__LENGTH
};

grub_uint64_t timers[TIMER__LENGTH] = {0};

/* Return true if at least ms milliseconds have elapsed since the last call
 * that returned true for this timer. When called on each iteration of the main
 * loop, has the effect of returning true once every ms milliseconds. */
static bool
interval (enum timer timer, grub_uint32_t ms)
{
  grub_uint64_t tf = rdtsc();
  if (tf - timers[timer] >= tpms * ms)
  {
    timers[timer] = tf;
    return true;
  }
  else 
    return false;
}

/* Return true if at least ms milliseconds have elapsed since the first call
 * for this timer and reset the timer. */
static bool
wait (enum timer timer, grub_uint32_t ms)
{
  if (timers[timer])
  {
    if (rdtsc() - timers[timer] >= tpms * ms)
    {
      timers[timer] = 0;
      return true;
    }
    else
      return false;
  }
  else
  {
    timers[timer] = rdtsc();
    return false;
  }
}

/* Video Output */
#define COLS (80)
#define ROWS (25)

enum color
{
  BLACK,
  BLUE,
  GREEN,
  CYAN,
  RED,
  MAGENTA,
  BROWN,
  GRAY,
  BRIGHT
};

#ifdef GRUB_MACHINE_EFI
grub_efi_uintn_t color_fg[9] = 
{
  GRUB_EFI_BLACK,
  GRUB_EFI_BLUE,
  GRUB_EFI_GREEN,
  GRUB_EFI_CYAN,
  GRUB_EFI_RED,
  GRUB_EFI_MAGENTA,
  GRUB_EFI_BROWN,
  GRUB_EFI_LIGHTGRAY,
  GRUB_EFI_WHITE
};

grub_efi_uintn_t color_bg[9] = 
{
  GRUB_EFI_BACKGROUND_BLACK,
  GRUB_EFI_BACKGROUND_BLUE,
  GRUB_EFI_BACKGROUND_GREEN,
  GRUB_EFI_BACKGROUND_CYAN,
  GRUB_EFI_BACKGROUND_RED,
  GRUB_EFI_BACKGROUND_MAGENTA,
  GRUB_EFI_BACKGROUND_BROWN,
  GRUB_EFI_BACKGROUND_LIGHTGRAY,
  GRUB_EFI_BACKGROUND_LIGHTGRAY
};
#elif defined (GRUB_MACHINE_PCBIOS)
grub_uint16_t* const video = (grub_uint16_t *) 0xB8000;
#endif
/* Display a character at x, y in fg foreground color and bg background color.
 */

static void
putc (grub_uint8_t x, grub_uint8_t y, enum color fg, enum color bg, char c)
{
#ifdef GRUB_MACHINE_EFI
  grub_efi_char16_t str[2];
  str[0] = c;
  str[1] = 0;
  efi_call_3 (grub_efi_system_table->con_out->set_cursor_position,
        grub_efi_system_table->con_out, x, y);
  efi_call_2 (grub_efi_system_table->con_out->set_attributes,
        grub_efi_system_table->con_out,
        GRUB_EFI_TEXT_ATTR(color_fg[fg], color_bg[bg]));
  efi_call_2 (grub_efi_system_table->con_out->output_string,
        grub_efi_system_table->con_out, str);
  efi_call_3 (grub_efi_system_table->con_out->set_cursor_position,
        grub_efi_system_table->con_out, 0, 0);
  efi_call_2 (grub_efi_system_table->con_out->set_attributes,
        grub_efi_system_table->con_out,
        GRUB_EFI_TEXT_ATTR(color_fg[8], color_bg[0]));
#elif defined (GRUB_MACHINE_PCBIOS)
  grub_uint16_t z = (bg << 12) | (fg << 8) | c;
  video[y * COLS + x] = z;
#endif
}

/* Display a string starting at x, y in fg foreground color and bg background
 * color. Characters in the string are not interpreted (e.g \n, \b, \t, etc.).
 * */
static void
puts (grub_uint8_t x, grub_uint8_t y, enum color fg, enum color bg, const char *s)
{
  for (; *s; s++, x++)
    putc(x, y, fg, bg, *s);
}

/* Clear the screen to bg backround color. */
static void
clear (enum color bg)
{
  grub_uint8_t x, y;
  for (y = 0; y < ROWS; y++)
    for (x = 0; x < COLS; x++)
      putc(x, y, bg, bg, ' ');
}

/* Keyboard Input */

#define KEY_D   'd'
#define KEY_H   'h'
#define KEY_P   'p'
#define KEY_R   'r'
#define KEY_S   's'
#define KEY_UP  GRUB_TERM_KEY_UP
#define KEY_DOWN  GRUB_TERM_KEY_DOWN
#define KEY_LEFT  GRUB_TERM_KEY_LEFT
#define KEY_RIGHT GRUB_TERM_KEY_RIGHT
#define KEY_ENTER 0x0d
#define KEY_SPACE ' '
#define KEY_ESC   GRUB_TERM_ESC

/* Return the scancode of the current up or down key if it has changed since
 * the last call, otherwise returns 0. When called on every iteration of the
 * main loop, returns non-zero on a key event. */
static int
scan (void)
{
  static int key = 0;
  int scan = grub_getkey_noblock ();
  if (scan != key)
    return key = scan;
  else return 0;
}

/* Formatting */

/* Format n in radix r (2-16) as a w length string. */
static char*
itoa (grub_uint32_t n, grub_uint8_t r, grub_uint8_t w)
{
  static const char d[16] = "0123456789ABCDEF";
  static char s[34];
  s[33] = 0;
  grub_uint8_t i = 33;
  do
  {
    i--;
    s[i] = d[n % r];
    n /= r;
  }
  while (i > 33 - w);
  return (char *) (s + i);
}

/* Random */

/* Generate a random number from 0 inclusive to range exclusive from the number
 * of CPU ticks since boot. */
static grub_uint32_t
rand (grub_uint32_t range)
{
  return (grub_uint32_t) rdtsc() % range;
}

/* Shuffle an array of bytes arr of length len in-place using Fisher-Yates. */
static void
shuffle (grub_uint8_t arr[], grub_uint32_t len)
{
  grub_uint32_t i, j;
  grub_uint8_t t;
  for (i = len - 1; i > 0; i--)
  {
    j = rand(i + 1);
    t = arr[i];
    arr[i] = arr[j];
    arr[j] = t;
  }
}

/* Tetris */

/* The seven tetriminos in each rotation. Each tetrimino is represented as an
 * array of 4 rotations, each represented by a 4x4 array of color values. */
grub_uint8_t TETRIS[7][4][4][4] =
{
  { /* I */
    {{0,0,0,0},
     {4,4,4,4},
     {0,0,0,0},
     {0,0,0,0}},
    {{0,4,0,0},
     {0,4,0,0},
     {0,4,0,0},
     {0,4,0,0}},
    {{0,0,0,0},
     {4,4,4,4},
     {0,0,0,0},
     {0,0,0,0}},
    {{0,4,0,0},
     {0,4,0,0},
     {0,4,0,0},
     {0,4,0,0}}
  },
  { /* J */
    {{7,0,0,0},
     {7,7,7,0},
     {0,0,0,0},
     {0,0,0,0}},
    {{0,7,7,0},
     {0,7,0,0},
     {0,7,0,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {7,7,7,0},
     {0,0,7,0},
     {0,0,0,0}},
    {{0,7,0,0},
     {0,7,0,0},
     {7,7,0,0},
     {0,0,0,0}}
  },
  { /* L */
    {{0,0,5,0},
     {5,5,5,0},
     {0,0,0,0},
     {0,0,0,0}},
    {{0,5,0,0},
     {0,5,0,0},
     {0,5,5,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {5,5,5,0},
     {5,0,0,0},
     {0,0,0,0}},
    {{5,5,0,0},
     {0,5,0,0},
     {0,5,0,0},
     {0,0,0,0}}
  },
  { /* O */
    {{0,0,0,0},
     {0,1,1,0},
     {0,1,1,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {0,1,1,0},
     {0,1,1,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {0,1,1,0},
     {0,1,1,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {0,1,1,0},
     {0,1,1,0},
     {0,0,0,0}}
  },
  { /* S */
    {{0,0,0,0},
     {0,2,2,0},
     {2,2,0,0},
     {0,0,0,0}},
    {{0,2,0,0},
     {0,2,2,0},
     {0,0,2,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {0,2,2,0},
     {2,2,0,0},
     {0,0,0,0}},
    {{0,2,0,0},
     {0,2,2,0},
     {0,0,2,0},
     {0,0,0,0}}
  },
  { /* T */
    {{0,6,0,0},
     {6,6,6,0},
     {0,0,0,0},
     {0,0,0,0}},
    {{0,6,0,0},
     {0,6,6,0},
     {0,6,0,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {6,6,6,0},
     {0,6,0,0},
     {0,0,0,0}},
    {{0,6,0,0},
     {6,6,0,0},
     {0,6,0,0},
     {0,0,0,0}}
  },
  { /* Z */
    {{0,0,0,0},
     {3,3,0,0},
     {0,3,3,0},
     {0,0,0,0}},
    {{0,0,3,0},
     {0,3,3,0},
     {0,3,0,0},
     {0,0,0,0}},
    {{0,0,0,0},
     {3,3,0,0},
     {0,3,3,0},
     {0,0,0,0}},
    {{0,0,3,0},
     {0,3,3,0},
     {0,3,0,0},
     {0,0,0,0}}
  }
};

/* Two-dimensional array of color values */
grub_uint8_t well[WELL_HEIGHT][WELL_WIDTH];

struct
{
  grub_uint8_t i, r; /* Index and rotation into the TETRIS array */
  grub_uint8_t p;  /* Index into bag of preview tetrimino */
  grub_int8_t x, y; /* Coordinates */
  grub_int8_t g;  /* Y-coordinate of ghost */
} current;

/* Shuffled bag of next tetrimino indices */
#define BAG_SIZE (7)
grub_uint8_t bag[BAG_SIZE] = {0, 1, 2, 3, 4, 5, 6};

grub_uint32_t score = 0, level = 1, speed = INITIAL_SPEED, max_score = 0;

bool paused = false, game_over = false;

/* Return true if the tetrimino i in rotation r will collide when placed at x,
 * y. */
static bool
collide (grub_uint8_t i, grub_uint8_t r, grub_int8_t x, grub_int8_t y)
{
  grub_uint8_t xx, yy;
  for (yy = 0; yy < 4; yy++)
    for (xx = 0; xx < 4; xx++)
      if (TETRIS[i][r][yy][xx])
        if (x + xx < 0 || x + xx >= WELL_WIDTH ||
          y + yy < 0 || y + yy >= WELL_HEIGHT ||
          well[y + yy][x + xx])
            return true;
  return false;
}

grub_uint32_t stats[7];

/* Set the current tetrimino to the preview tetrimino in the default rotation
 * and place it in the top center. Increase the stats count for the spawned
 * tetrimino. Set the preview tetrimino to the next one in the shuffled bag. If
 * the spawned tetrimino was the last in the bag, re-shuffle the bag and set
 * the preview to the first in the bag. */
static void
spawn (void)
{
  current.i = bag[current.p];
  stats[current.i]++;
  current.r = 0;
  current.x = WELL_WIDTH / 2 - 2;
  current.y = 0;
  current.p++;
  if (current.p == BAG_SIZE)
  {
    current.p = 0;
    shuffle(bag, BAG_SIZE);
  }
}

/* Set the ghost y-coordinate by moving the current tetrimino down until it
 * collides. */
static void
ghost (void)
{
  grub_int8_t y;
  for (y = current.y; y < WELL_HEIGHT; y++)
    if (collide(current.i, current.r, current.x, y))
      break;
  current.g = y - 1;
}

/* Try to move the current tetrimino by dx, dy and return true if successful.
 */
static bool
move (grub_int8_t dx, grub_int8_t dy)
{
  if (game_over)
    return false;

  if (collide(current.i, current.r, current.x + dx, current.y + dy))
    return false;
  current.x += dx;
  current.y += dy;
  return true;
}

/* Try to rotate the current tetrimino clockwise and return true if successful.
 */
static bool
rotate (void)
{
  if (game_over)
    return false;

  grub_uint8_t r = (current.r + 1) % 4;
  if (collide(current.i, r, current.x, current.y))
    return false;
  current.r = r;
  return true;
}

/* Try to move the current tetrimino down one and increase the score if
 * successful. */
static void
soft_drop (void)
{
  if (move(0, 1))
    score += SOFT_DROP_SCORE;
}

/* Lock the current tetrimino into the well. This is done by copying the color
 * values from the 4x4 array of the tetrimino into the well array. */
static void lock(void)
{
  grub_uint8_t x, y;
  for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
      if (TETRIS[current.i][current.r][y][x])
        well[current.y + y][current.x + x] =
          TETRIS[current.i][current.r][y][x];
}

/* The y-coordinates of the rows cleared in the last update, top down */
grub_int8_t cleared_rows[4];

/* Update the game state. Called at an interval relative to the current level.
 */
static void
update (void)
{
  /* Gravity: move the current tetrimino down by one. If it cannot be moved
   * and it is still in the top row, set game over state. If it cannot be
   * moved down but is not in the top row, lock it in place and spawn a new
   * tetrimino. */
  if (!move(0, 1))
  {
    if (current.y == 0)
    {
      game_over = true;
      return;
    }
    lock();
    spawn();
  }

  /* Row clearing: check if any rows are full across and add them to the
   * cleared_rows array. */
  static grub_uint8_t level_rows = 0; /* Rows cleared in the current level */

  grub_uint8_t x, y, a, i = 0, rows = 0;
  for (y = 0; y < WELL_HEIGHT; y++)
  {
    for (a = 0, x = 0; x < WELL_WIDTH; x++)
      if (well[y][x])
        a++;
    if (a != WELL_WIDTH)
      continue;

    rows++;
    cleared_rows[i++] = y;
  }

  /* Scoring */
  switch (rows)
  {
    case 1: score += SCORE_FACTOR_1 * level; break;
    case 2: score += SCORE_FACTOR_2 * level; break;
    case 3: score += SCORE_FACTOR_3 * level; break;
    case 4: score += SCORE_FACTOR_4 * level; break;
  }
  if (score > max_score)
    max_score = score;
  /* Leveling: increase the level for every 10 rows cleared, increase game
   * speed. */
  level_rows += rows;
  if (level_rows >= ROWS_PER_LEVEL)
  {
    level++;
    level_rows -= ROWS_PER_LEVEL;

    speed = 10 + 990 / level;
  }
}

/* Clear the rows in the rows_cleared array and move all rows above them down.
 */
static void
clear_rows (void)
{
  grub_int8_t i, y, x;
  for (i = 0; i < 4; i++)
  {
    if (!cleared_rows[i])
      break;
    for (y = cleared_rows[i]; y > 0; y--)
      for (x = 0; x < WELL_WIDTH; x++)
        well[y][x] = well[y - 1][x];
    cleared_rows[i] = 0;
  }
}

/* Move the current tetrimino to the position of its ghost, increase the score
 * and trigger an update (to cause locking and clearing). */
static void
drop (void)
{
  if (game_over)
    return;

  score += HARD_DROP_SCORE_FACTOR * (current.g - current.y);
  current.y = current.g;
  update();
}

#define TITLE_X (COLS / 2 - 9)
#define TITLE_Y (ROWS / 2 - 1)

/* Draw about information in the centre. Shown on boot and pause. */
static void draw_about(void) {
  puts(TITLE_X,    TITLE_Y,   BLACK,  RED,   "   ");
  puts(TITLE_X + 3,  TITLE_Y,   BLACK,  MAGENTA, "   ");
  puts(TITLE_X + 6,  TITLE_Y,   BLACK,  BLUE,  "   ");
  puts(TITLE_X + 9,  TITLE_Y,   BLACK,  GREEN,   "   ");
  puts(TITLE_X + 12, TITLE_Y,   BLACK,  BROWN,  "   ");
  puts(TITLE_X + 15, TITLE_Y,   BLACK,  CYAN,  "   ");
  puts(TITLE_X,    TITLE_Y + 1, GRAY,   RED,   " T ");
  puts(TITLE_X + 3,  TITLE_Y + 1, GRAY,   MAGENTA, " E ");
  puts(TITLE_X + 6,  TITLE_Y + 1, GRAY,   BLUE,  " T ");
  puts(TITLE_X + 9,  TITLE_Y + 1, GRAY,   GREEN,   " R ");
  puts(TITLE_X + 12, TITLE_Y + 1, GRAY,   BROWN,  " I ");
  puts(TITLE_X + 15, TITLE_Y + 1, GRAY,   CYAN,  " S ");
  puts(TITLE_X,    TITLE_Y + 2, BLACK,  RED,   "   ");
  puts(TITLE_X + 3,  TITLE_Y + 2, BLACK,  MAGENTA, "   ");
  puts(TITLE_X + 6,  TITLE_Y + 2, BLACK,  BLUE,  "   ");
  puts(TITLE_X + 9,  TITLE_Y + 2, BLACK,  GREEN,   "   ");
  puts(TITLE_X + 12, TITLE_Y + 2, BLACK,  BROWN,  "   ");
  puts(TITLE_X + 15, TITLE_Y + 2, BLACK,  CYAN,  "   ");

  puts(0, ROWS - 1, GRAY,  BLACK,
     "TETRIS for GRUB");
}

#define WELL_X (COLS / 2 - WELL_WIDTH)

#define PREVIEW_X (COLS * 3/4 + 1)
#define PREVIEW_Y (2)

#define STATUS_X (COLS * 3/4)
#define STATUS_Y (ROWS / 2 - 4)

#define MAX_SCORE_X STATUS_X
#define MAX_SCORE_Y (ROWS / 2 - 1)

#define SCORE_X MAX_SCORE_X
#define SCORE_Y (MAX_SCORE_Y + 4)

#define LEVEL_X SCORE_X
#define LEVEL_Y (SCORE_Y + 4)

/* Draw the well, current tetrimino, its ghost, the preview tetrimino, the
 * status, score and level indicators. Each well/tetrimino cell is drawn one
 * screen-row high and two screen-columns wide. The top two rows of the well
 * are hidden. Rows in the cleared_rows array are drawn as white rather than
 * their actual colors. */
static void
draw (void)
{
  grub_uint8_t x, y;

  if (paused)
  {
    draw_about();
    goto status;
  }

  /* Border */
  for (y = 2; y < WELL_HEIGHT; y++) {
    putc(WELL_X - 1, y, BLACK, GRAY, ' ');
    putc(COLS / 2 + WELL_WIDTH, y, BLACK, GRAY, ' ');
  }
  for (x = 0; x < WELL_WIDTH * 2 + 2; x++)
    putc(WELL_X + x - 1, WELL_HEIGHT, BLACK, GRAY, ' ');

  /* Well */
  for (y = 0; y < 2; y++)
    for (x = 0; x < WELL_WIDTH; x++)
      puts(WELL_X + x * 2, y, BLACK, BLACK, "  ");
  for (y = 2; y < WELL_HEIGHT; y++)
    for (x = 0; x < WELL_WIDTH; x++)
      if (well[y][x])
        if (cleared_rows[0] == y || cleared_rows[1] == y ||
          cleared_rows[2] == y || cleared_rows[3] == y)
          puts(WELL_X + x * 2, y, BLACK, BRIGHT, "  ");
        else
          puts(WELL_X + x * 2, y, BLACK, well[y][x], "  ");
      else
        puts(WELL_X + x * 2, y, BROWN, BLACK, "  "); /* FIXME */

  /* Ghost */
  if (!game_over)
    for (y = 0; y < 4; y++)
      for (x = 0; x < 4; x++)
        if (TETRIS[current.i][current.r][y][x])
          puts(WELL_X + current.x * 2 + x * 2, current.g + y,
            TETRIS[current.i][current.r][y][x], BLACK, "::");

  /* Current */
  for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
      if (TETRIS[current.i][current.r][y][x])
        puts(WELL_X + current.x * 2 + x * 2, current.y + y, BLACK,
           TETRIS[current.i][current.r][y][x], "  ");

  /* Preview */
  for (y = 0; y < 4; y++)
    for (x = 0; x < 4; x++)
      if (TETRIS[bag[current.p]][0][y][x])
        puts(PREVIEW_X + x * 2, PREVIEW_Y + y, BLACK,
           TETRIS[bag[current.p]][0][y][x], "  ");
      else
        puts(PREVIEW_X + x * 2, PREVIEW_Y + y, BLACK, BLACK, "  ");

status:
  if (paused)
    puts(STATUS_X + 2, STATUS_Y, BRIGHT, BLACK, "PAUSED");
  if (game_over)
    puts(STATUS_X, STATUS_Y, BRIGHT, BLACK, "GAME OVER");
  
  /* Highest score */
  puts(MAX_SCORE_X - 2, MAX_SCORE_Y, BLUE, BLACK, "HIGHEST SCORE");
  puts(MAX_SCORE_X, MAX_SCORE_Y + 2, BRIGHT, BLACK, itoa(max_score, 10, 10));

  /* Score */
  puts(SCORE_X + 2, SCORE_Y, BLUE, BLACK, "SCORE");
  puts(SCORE_X, SCORE_Y + 2, BRIGHT, BLACK, itoa(score, 10, 10));

  /* Level */
  puts(LEVEL_X + 2, LEVEL_Y, BLUE, BLACK, "LEVEL");
  puts(LEVEL_X, LEVEL_Y + 2, BRIGHT, BLACK, itoa(level, 10, 10));
}

static grub_err_t
grub_cmd_tetris (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		int argc __attribute__ ((unused)), char **args __attribute__ ((unused)))
{
  grub_memset (well, 0, sizeof (well));
  score = 0;
  level = 1;
  speed = INITIAL_SPEED;
  paused = false;
  game_over = false;
#ifdef GRUB_MACHINE_EFI
  grub_efi_simple_text_output_mode_t saved_console_mode;
  /* Save the current console cursor position and attributes */
  grub_memcpy(&saved_console_mode,
        grub_efi_system_table->con_out->mode,
        sizeof(grub_efi_simple_text_output_mode_t));
  efi_call_2 (grub_efi_system_table->con_out->enable_cursor,
        grub_efi_system_table->con_out, 0);
#endif

  clear(BLACK);
  draw_about();

  /* Wait a full second to calibrate timing. */
  grub_uint32_t itpms;
  tps();
  itpms = tpms; while (tpms == itpms) tps();
  itpms = tpms; while (tpms == itpms) tps();

  /* Initialize game state. Shuffle bag of tetriminos until first tetrimino
   * is not S or Z. */
  do
  {
    shuffle(bag, BAG_SIZE);    
  }
  while (bag[0] == 4 || bag[0] == 6);
  spawn();
  ghost();
  clear(BLACK);
  draw();

  bool debug = false, help = true, statistics = false;
  int last_key;
loop:
  tps();
  if (!debug && !statistics)
    help = true;

  if (debug)
  {
    grub_uint32_t i;
    puts(0,  0, GRAY,   BLACK, "RTC sec:");
    puts(10, 0, GREEN,  BLACK, itoa(rtcs(), 16, 2));
    puts(0,  1, GRAY,   BLACK, "ticks/ms:");
    puts(10, 1, GREEN,  BLACK, itoa(tpms, 10, 10));
    puts(0,  2, GRAY,   BLACK, "key:");
    puts(10, 2, GREEN,  BLACK, itoa(last_key, 16, 2));
    puts(0,  3, GRAY,   BLACK, "i,r,p:");
    puts(10, 3, GREEN,  BLACK, itoa(current.i, 10, 1));
    putc(11, 3, GREEN,  BLACK, ',');
    puts(12, 3, GREEN,  BLACK, itoa(current.r, 10, 1));
    putc(13, 3, GREEN,  BLACK, ',');
    puts(14, 3, GREEN,  BLACK, itoa(current.p, 10, 1));
    puts(0,  4, GRAY,   BLACK, "x,y,g:");
    puts(10, 4, GREEN,  BLACK, itoa(current.x, 10, 3));
    putc(13, 4, GREEN,  BLACK, ',');
    puts(14, 4, GREEN,  BLACK, itoa(current.y, 10, 3));
    putc(17, 4, GREEN,  BLACK, ',');
    puts(18, 4, GREEN,  BLACK, itoa(current.g, 10, 3));
    puts(0,  5, GRAY,   BLACK, "bag:");
    for (i = 0; i < 7; i++)
      puts(10 + i * 2, 5, GREEN, BLACK, itoa(bag[i], 10, 1));
    puts(0,  6, GRAY,   BLACK, "speed:");
    puts(10, 6, GREEN,  BLACK, itoa(speed, 10, 10));
    for (i = 0; i < TIMER__LENGTH; i++)
    {
      puts(0,  7 + i, GRAY,   BLACK, "timer:");
      puts(10, 7 + i, GREEN,  BLACK, itoa(timers[i], 10, 10));
    }
  }

  if (help)
  {
    puts(1, 12, GRAY,   BLACK, "LEFT");
    puts(7, 12, BLUE,   BLACK, "- Move left");
    puts(1, 13, GRAY,   BLACK, "RIGHT");
    puts(7, 13, BLUE,   BLACK, "- Move right");
    puts(1, 14, GRAY,   BLACK, "UP");
    puts(7, 14, BLUE,   BLACK, "- Rotate clockwise");
    puts(1, 15, GRAY,   BLACK, "DOWN");
    puts(7, 15, BLUE,   BLACK, "- Soft drop");
    puts(1, 16, GRAY,   BLACK, "ENTER");
    puts(7, 16, BLUE,   BLACK, "- Hard drop");
    puts(1, 17, GRAY,   BLACK, "P");
    puts(7, 17, BLUE,   BLACK, "- Pause");
    puts(1, 18, GRAY,   BLACK, "R");
    puts(7, 18, BLUE,   BLACK, "- Hard reset");
    puts(1, 19, GRAY,   BLACK, "S");
    puts(7, 19, BLUE,   BLACK, "- Toggle statistics");
    puts(1, 20, GRAY,   BLACK, "D");
    puts(7, 20, BLUE,   BLACK, "- Toggle debug info");
    puts(1, 21, GRAY,   BLACK, "H");
    puts(7, 21, BLUE,   BLACK, "- Toggle help");
  }

  if (statistics)
  {
    grub_uint8_t i, x, y;
    for (i = 0; i < 7; i++)
    {
      for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
          if (TETRIS[i][0][y][x])
            puts(5 + x * 2, 1 + i * 3 + y, BLACK,
               TETRIS[i][0][y][x], "  ");
      puts(14, 2 + i * 3, BLUE, BLACK, itoa(stats[i], 10, 10));
    }
  }

  bool updated = false;

  int key;
  if ((key = scan()))
  {
    last_key = key;
    switch(key)
    {
      case KEY_D:
        debug = !debug;
        if (debug)
          help = statistics = false;
        clear(BLACK);
        break;
      case KEY_H:
        help = !help;
        if (help)
          debug = statistics = false;
        clear(BLACK);
        break;
      case KEY_S:
        statistics = !statistics;
        if (statistics)
          debug = help = false;
        clear(BLACK);
        break;
      case KEY_R:
      case KEY_ESC:
        goto fail;
      case KEY_LEFT:
        move(-1, 0);
        break;
      case KEY_RIGHT:
        move(1, 0);
        break;
      case KEY_DOWN:
        soft_drop();
        break;
      case KEY_UP:
      case KEY_SPACE:
        rotate();
        break;
      case KEY_ENTER:
        drop();
        break;
      case KEY_P:
        if (game_over)
          break;
        clear(BLACK);
        paused = !paused;
        break;
    }
    updated = true;
  }

  if (!paused && !game_over && interval(TIMER_UPDATE, speed))
  {
    update();
    updated = true;
  }

  if (cleared_rows[0] && wait(TIMER_CLEAR, CLEAR_DELAY))
  {
    clear_rows();
    updated = true;
  }

  if (updated)
  {
    ghost();
    draw();
  }

  goto loop;
fail:
  if (score > max_score)
    max_score = score;
#ifdef GRUB_MACHINE_EFI
  efi_call_2 (grub_efi_system_table->con_out->enable_cursor,
        grub_efi_system_table->con_out, saved_console_mode.cursor_visible);
  efi_call_3 (grub_efi_system_table->con_out->set_cursor_position,
        grub_efi_system_table->con_out,
        saved_console_mode.cursor_column,
        saved_console_mode.cursor_row);
  efi_call_2 (grub_efi_system_table->con_out->set_attributes,
        grub_efi_system_table->con_out,
        saved_console_mode.attribute);
#endif
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(tetris)
{
  cmd = grub_register_extcmd ("tetris", grub_cmd_tetris, 0, 0,
			    N_("Tetris game."), 0);
}

GRUB_MOD_FINI(tetris)
{
  grub_unregister_extcmd (cmd);
}
