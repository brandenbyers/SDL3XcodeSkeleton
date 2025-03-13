/*
 * level.h - Level system declarations
 *
 * This header defines structures and functions for managing game levels.
 */

#ifndef LEVEL_H
#define LEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Maximum number of levels in the game */
#define MAX_LEVELS 10

/* Level structure to hold level-specific data */
typedef struct {
    const char* name;           /* Level name/title */
    uint8_t num_items;          /* Number of items to collect */
    bool items_required;        /* Whether all items are required to win */
} LevelInfo;

/* Initialize the level system */
void init_level_system(GameState* game);

/* Load a specific level */
void load_level(GameState* game, int level_num);

/* Advance to the next level (with wraparound) */
void next_level(GameState* game);

/* Track an item collection */
void track_item_collected(GameState* game);

/* Check if the level is completed */
bool is_level_completed(const GameState* game);

/* Get current level number */
int get_current_level(const GameState* game);

/* Get total number of levels */
int get_total_levels(void);

/* Get info about the current level */
const LevelInfo* get_current_level_info(const GameState* game);

#endif /* LEVEL_H */
