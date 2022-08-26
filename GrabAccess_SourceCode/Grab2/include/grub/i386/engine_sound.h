/* engine_sound.h - Interface to engine of sound.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright 2015,2017 Ruyi Boy - All Rights Reserved
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

#ifndef GRUB_MENU_SOUND_HEADER
#define GRUB_MENU_SOUND_HEADER 1

#include <grub/types.h>
#include <grub/menu.h>
#include <grub/speaker.h>

#define ENGINE_SOUND_STOP 0
#define ENGINE_SOUND_PLAY 1

typedef struct engine_sound_class *sound_class_t;

struct engine_sound_class
{
  grub_uint16_t *start_buf;
  grub_uint16_t *select_buf;
  int start_len;
  int select_len;
  int cur_index;
  int selected;
  int play_mark;
};

struct engine_sound_player
{
  struct engine_sound_player *next;
  void *data;
  void (*refresh_player_state) (int is_selected, int cur_sound, void *data);
  void (*fini) (void *data);
};

sound_class_t engine_sound_new (void);

void
engine_sound_destroy (sound_class_t sound);
void
engine_register_player (struct engine_sound_player *player);
void
engine_player_refresh (int is_selected, int cur_sound, void *data);

extern grub_err_t (*engine_need_sound) (void);

#endif /* GRUB_MENU_SOUND_HEADER */
