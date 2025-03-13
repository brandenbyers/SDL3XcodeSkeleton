/*
 * level.c - Level system implementation
 *
 * This file contains functions for managing game levels, including
 * level loading, transitions, and win conditions.
 */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"
#include "game.h"
#include "entity.h"
#include "collision.h"
#include "viewport.h"
#include "input.h"

/* Level state tracking */
static int s_current_level = 0;
static int s_total_levels = 2;  /* Initial implementation has 2 levels */
static int s_items_collected = 0;

/* Level information */
static const LevelInfo s_level_info[MAX_LEVELS] = {
    { "Tutorial Level", 5, false },    /* Level 0 */
    { "Entity Madness", 10, true }     /* Level 1 */
};

/* Clear all level data */
static void clear_level_data(GameState* game) {
    /* Clear the visual grid */
    memset(game->grid, CELL_EMPTY, GRID_SIZE);
    
    /* Explicitly clear the collision map */
    memset(game->collision->map, CMAP_EMPTY, GRID_SIZE);
    memset(game->collision->pivot_dirs, 0, GRID_SIZE);
    
    /* Initialize systems */
    init_collision_system(game->collision);
    init_entity_system(game);
    
    /* Reset items collected counter */
    s_items_collected = 0;
    
    /* Mark grid as changed to force texture update */
    game->grid_state->cells_changed = true;
    game->grid_state->cache_valid = false;
}

/* Initialize the level system */
void init_level_system(GameState* game) {
    /* Start with level 0 */
    s_current_level = 0;
    load_level(game, s_current_level);
}

/* Build level 0 (tutorial level) */
static void build_level0(GameState* game) {
    /* Clear existing level data - crucial for clean level transitions */
    clear_level_data(game);
    
    /* Outer walls around the viewport */
    for (int i = 0; i < VIEWPORT_WIDTH; i++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Top wall */
        viewport_to_grid(game, i, 0, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Bottom wall */
        viewport_to_grid(game, i, VIEWPORT_HEIGHT - 1, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Side walls of the viewport */
    for (int j = 0; j < VIEWPORT_HEIGHT; j++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Left wall */
        viewport_to_grid(game, 0, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Right wall */
        viewport_to_grid(game, VIEWPORT_WIDTH - 1, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Add pivot points for entity movement */
    int left_pivot_x, left_pivot_y;
    int right_pivot_x, right_pivot_y;
    
    /* Left side pivot */
    viewport_to_grid(game, 10, VIEWPORT_HEIGHT/2, &left_pivot_x, &left_pivot_y);
    set_cell(game, left_pivot_x, left_pivot_y, CELL_PIVOT);
    
    /* Set pivot in collision map with directions */
    set_pivot_point(game, left_pivot_x, left_pivot_y, PIVOT_LEFT | PIVOT_RIGHT);
    
    /* Right side pivot */
    viewport_to_grid(game, VIEWPORT_WIDTH - 11, VIEWPORT_HEIGHT/2, &right_pivot_x, &right_pivot_y);
    set_cell(game, right_pivot_x, right_pivot_y, CELL_PIVOT);
    
    /* Set pivot in collision map with directions */
    set_pivot_point(game, right_pivot_x, right_pivot_y, PIVOT_LEFT | PIVOT_RIGHT);
    
    /* Add a patrol entity that moves between the pivots */
    add_entity(game, left_pivot_x, left_pivot_y, DIR_RIGHT, ENTITY_PATROL);
    
    /* Add items for collection - specific placement for tutorial level */
    int item_count = s_level_info[0].num_items;
    
    for (int i = 0; i < item_count; i++) {
        int viewport_x = 10 + i * 10;  /* Evenly spaced */
        int viewport_y = VIEWPORT_HEIGHT/4;
        
        if (viewport_x >= VIEWPORT_WIDTH - 5) {
            viewport_x = 10 + (i % 4) * 10;
            viewport_y = 3 * VIEWPORT_HEIGHT/4;
        }
        
        /* Convert to grid coordinates */
        int grid_x, grid_y;
        viewport_to_grid(game, viewport_x, viewport_y, &grid_x, &grid_y);
        
        /* Place item */
        set_cell(game, grid_x, grid_y, CELL_ITEM);
        set_collision_cell(game, grid_x, grid_y, CMAP_ITEM, true);
    }
    
    /* Initialize player in center of bottom half */
    int player_viewport_x = VIEWPORT_WIDTH / 2;
    int player_viewport_y = 3 * VIEWPORT_HEIGHT / 4;
    
    /* Convert to grid coordinates */
    int player_grid_x, player_grid_y;
    viewport_to_grid(game, player_viewport_x, player_viewport_y, &player_grid_x, &player_grid_y);
    
    /* Make sure player doesn't start on an item */
    if (get_cell(game, player_grid_x, player_grid_y) == CELL_ITEM) {
        player_grid_y = (player_grid_y + 1) & GRID_HEIGHT_MASK;
    }
    
    /* Set player position */
    game->player->pos_x = player_grid_x;
    game->player->pos_y = player_grid_y;
    game->player->target_x = player_grid_x;
    game->player->target_y = player_grid_y;
    game->player->direction = DIR_NONE;
    game->player->is_moving = false;
    game->player->just_started = false;
    game->player->move_frame = 0;
    
    /* Add player to collision map */
    set_collision_cell(game, player_grid_x, player_grid_y, CMAP_PLAYER, true);
}

/* Build level 1 (entity madness) - complex level with many entities */
static void build_level1(GameState* game) {
    /* Clear existing level data - crucial for clean level transitions */
    clear_level_data(game);
    
    /* Outer walls around the viewport */
    for (int i = 0; i < VIEWPORT_WIDTH; i++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Top wall */
        viewport_to_grid(game, i, 0, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Bottom wall */
        viewport_to_grid(game, i, VIEWPORT_HEIGHT - 1, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Side walls of the viewport */
    for (int j = 0; j < VIEWPORT_HEIGHT; j++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Left wall */
        viewport_to_grid(game, 0, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Right wall */
        viewport_to_grid(game, VIEWPORT_WIDTH - 1, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Add some inner walls for a more complex maze */
    int inner_wall_x, inner_wall_y;
    
    /* Horizontal divider with openings */
    for (int i = 10; i < VIEWPORT_WIDTH - 10; i += 3) {
        viewport_to_grid(game, i, VIEWPORT_HEIGHT/2, &inner_wall_x, &inner_wall_y);
        set_cell(game, inner_wall_x, inner_wall_y, CELL_WALL);
        set_collision_cell(game, inner_wall_x, inner_wall_y, CMAP_WALL, true);
    }
    
    /* Vertical dividers */
    for (int j = 10; j < VIEWPORT_HEIGHT - 10; j += 4) {
        viewport_to_grid(game, VIEWPORT_WIDTH/3, j, &inner_wall_x, &inner_wall_y);
        set_cell(game, inner_wall_x, inner_wall_y, CELL_WALL);
        set_collision_cell(game, inner_wall_x, inner_wall_y, CMAP_WALL, true);
        
        viewport_to_grid(game, 2*VIEWPORT_WIDTH/3, j, &inner_wall_x, &inner_wall_y);
        set_cell(game, inner_wall_x, inner_wall_y, CELL_WALL);
        set_collision_cell(game, inner_wall_x, inner_wall_y, CMAP_WALL, true);
    }
    
    /* Add many pivot points for complex entity movement */
    int pivot_x, pivot_y;
    
    /* Grid of pivot points */
    for (int vx = 10; vx < VIEWPORT_WIDTH - 10; vx += 10) {
        for (int vy = 8; vy < VIEWPORT_HEIGHT - 8; vy += 8) {
            viewport_to_grid(game, vx, vy, &pivot_x, &pivot_y);
            
            /* Skip if there's a wall here */
            if (get_cell(game, pivot_x, pivot_y) == CELL_WALL) {
                continue;
            }
            
            set_cell(game, pivot_x, pivot_y, CELL_PIVOT);
            
            /* Set random pivot directions - make sure at least one direction is allowed */
            uint8_t pivot_dirs = 0;
            while (pivot_dirs == 0) {
                pivot_dirs = (rand() % 16);  /* 0000 to 1111 binary */
            }
            set_pivot_point(game, pivot_x, pivot_y, pivot_dirs);
        }
    }
    
    /* Add 8 entities of different types */
    
    /* Four patrol entities in the corners */
    int corner_x, corner_y;
    
    /* Top-left corner */
    viewport_to_grid(game, 5, 5, &corner_x, &corner_y);
    add_entity(game, corner_x, corner_y, DIR_RIGHT, ENTITY_PATROL);
    
    /* Top-right corner */
    viewport_to_grid(game, VIEWPORT_WIDTH - 6, 5, &corner_x, &corner_y);
    add_entity(game, corner_x, corner_y, DIR_LEFT, ENTITY_PATROL);
    
    /* Bottom-left corner */
    viewport_to_grid(game, 5, VIEWPORT_HEIGHT - 6, &corner_x, &corner_y);
    add_entity(game, corner_x, corner_y, DIR_UP, ENTITY_PATROL);
    
    /* Bottom-right corner */
    viewport_to_grid(game, VIEWPORT_WIDTH - 6, VIEWPORT_HEIGHT - 6, &corner_x, &corner_y);
    add_entity(game, corner_x, corner_y, DIR_DOWN, ENTITY_PATROL);
    
    /* Four clockwise entities in the middle */
    int mid_x, mid_y;
    
    /* Near middle 1 */
    viewport_to_grid(game, VIEWPORT_WIDTH/3, VIEWPORT_HEIGHT/3, &mid_x, &mid_y);
    add_entity(game, mid_x, mid_y, DIR_RIGHT, ENTITY_CLOCKWISE);
    
    /* Near middle 2 */
    viewport_to_grid(game, 2*VIEWPORT_WIDTH/3, VIEWPORT_HEIGHT/3, &mid_x, &mid_y);
    add_entity(game, mid_x, mid_y, DIR_DOWN, ENTITY_CLOCKWISE);
    
    /* Near middle 3 */
    viewport_to_grid(game, VIEWPORT_WIDTH/3, 2*VIEWPORT_HEIGHT/3, &mid_x, &mid_y);
    add_entity(game, mid_x, mid_y, DIR_UP, ENTITY_CLOCKWISE);
    
    /* Near middle 4 */
    viewport_to_grid(game, 2*VIEWPORT_WIDTH/3, 2*VIEWPORT_HEIGHT/3, &mid_x, &mid_y);
    add_entity(game, mid_x, mid_y, DIR_LEFT, ENTITY_CLOCKWISE);
    
    /* Add items for collection - scattered around more randomly */
    int item_count = s_level_info[1].num_items;
    
    for (int i = 0; i < item_count; i++) {
        /* Generate random viewport coordinates (away from walls) */
        int viewport_x = 5 + (rand() % (VIEWPORT_WIDTH - 10));
        int viewport_y = 5 + (rand() % (VIEWPORT_HEIGHT - 10));
        
        /* Convert to grid coordinates */
        int grid_x, grid_y;
        viewport_to_grid(game, viewport_x, viewport_y, &grid_x, &grid_y);
        
        /* Only place items in empty spaces */
        if (get_cell(game, grid_x, grid_y) == CELL_EMPTY &&
            !is_entity_at_position(game, grid_x, grid_y)) {
            set_cell(game, grid_x, grid_y, CELL_ITEM);
            set_collision_cell(game, grid_x, grid_y, CMAP_ITEM, true);
        }
    }
    
    /* Initialize player in a safe corner */
    int player_viewport_x = 3;
    int player_viewport_y = 3;
    
    /* Convert to grid coordinates */
    int player_grid_x, player_grid_y;
    viewport_to_grid(game, player_viewport_x, player_viewport_y, &player_grid_x, &player_grid_y);
    
    /* Set player position */
    game->player->pos_x = player_grid_x;
    game->player->pos_y = player_grid_y;
    game->player->target_x = player_grid_x;
    game->player->target_y = player_grid_y;
    game->player->direction = DIR_NONE;
    game->player->is_moving = false;
    game->player->just_started = false;
    game->player->move_frame = 0;
    
    /* Add player to collision map */
    set_collision_cell(game, player_grid_x, player_grid_y, CMAP_PLAYER, true);
}

/* Load a specific level */
void load_level(GameState* game, int level_num) {
    /* Handle level number bounds checking */
    if (level_num < 0) {
        level_num = s_total_levels - 1;
    } else if (level_num >= s_total_levels) {
        level_num = 0;
    }
    
    /* Store the current level */
    s_current_level = level_num;
    
    /* Reset input state */
    game->input->key_states = 0;
    game->input->current_dir = DIR_NONE;
    game->input->buffered_dir = DIR_NONE;
    game->input->actions = 0;  /* Clear any pending actions */
    
    /* Build the selected level */
    switch (level_num) {
        case 0:
            build_level0(game);
            break;
        case 1:
            build_level1(game);
            break;
        default:
            /* Should never get here due to bounds checking above */
            build_level0(game);
            break;
    }
    
    /* Update viewport cache */
    update_viewport_cache(game);
    
    /* Mark grid as updated and force background texture rebuild */
    game->grid_state->cells_changed = true;
    game->grid_state->last_frame_updated = game->frame_count;
    
    /* Reset timing */
    game->last_tick_time = SDL_GetTicks();
    game->accumulated_time = 0;
}

/* Advance to the next level (with wraparound) */
void next_level(GameState* game) {
    load_level(game, s_current_level + 1);
}

/* Track an item collection */
void track_item_collected(GameState* game) {
    s_items_collected++;
}

/* Check if the level is completed */
bool is_level_completed(const GameState* game) {
    /* For now, we'll say a level is completed when all items are collected */
    const LevelInfo* info = &s_level_info[s_current_level];
    
    /* If items are required, check against the level's item count */
    if (info->items_required) {
        return s_items_collected >= info->num_items;
    }
    
    /* Otherwise, a minimum collection might be enough */
    return s_items_collected >= info->num_items / 2;
}

/* Get current level number */
int get_current_level(const GameState* game) {
    return s_current_level;
}

/* Get total number of levels */
int get_total_levels(void) {
    return s_total_levels;
}

/* Get info about the current level */
const LevelInfo* get_current_level_info(const GameState* game) {
    return &s_level_info[s_current_level];
}
