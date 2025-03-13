/*
 * collision.c - Collision system implementation
 *
 * This file contains the grid-based collision detection and response system.
 * Uses bit manipulation for maximum efficiency.
 */

#include <string.h>
#include "collision.h"
#include "game.h"
#include "entity.h"
#include "physics.h"  /* Include physics.h for MovementState definition */

/* Initialize the collision system */
void init_collision_system(CollisionSystem* collision) {
    /* Clear collision and pivot maps */
    memset(collision->map, CMAP_EMPTY, GRID_SIZE);
    memset(collision->pivot_dirs, 0, GRID_SIZE);
}

/* Update the collision map based on entity positions */
void update_collision_map(GameState* game) {
    CollisionSystem* collision = game->collision;
    
    /* Clear entity and player positions from collision map */
    for (int i = 0; i < GRID_SIZE; i++) {
        collision->map[i] &= ~(CMAP_ENTITY | CMAP_PLAYER);
    }
    
    /* Add player position to collision map */
    int player_idx = (game->player->pos_y << GRID_WIDTH_SHIFT) | game->player->pos_x;
    collision->map[player_idx] |= CMAP_PLAYER;
    
    /* Add entity positions to collision map */
    for (int i = 0; i < game->entities->count; i++) {
        if (game->entities->is_active[i]) {
            int entity_idx = (game->entities->pos_y[i] << GRID_WIDTH_SHIFT) | game->entities->pos_x[i];
            collision->map[entity_idx] |= CMAP_ENTITY;
        }
    }
}

/* Check if a move is valid */
bool is_move_valid(const GameState* game, int x, int y, Direction dir) {
    /* Handle invalid direction */
    if (dir == DIR_NONE) return false;
    
    /* Calculate target position with bit manipulation for wrapping */
    int target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    int target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
    
    /* Calculate target index */
    int target_idx = (target_y << GRID_WIDTH_SHIFT) | target_x;
    
    /* Check if target cell has a wall */
    if (game->collision->map[target_idx] & CMAP_WALL) {
        return false;
    }
    
    return true;
}

/* Get possible directions at a position */
uint8_t get_pivot_directions(const GameState* game, int x, int y) {
    /* Calculate index */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* If not a pivot point, return 0 */
    if (!(game->collision->map[idx] & CMAP_PIVOT)) {
        return 0;
    }
    
    /* Return pivot directions */
    return game->collision->pivot_dirs[idx];
}

/* Determine new direction based on pivot directions and entity rules */
Direction get_new_direction(uint8_t pivot_directions, uint8_t entity_rule, Direction current) {
    /* Get bit for current direction */
    uint8_t current_bit = 1 << current;
    
    /* Get opposite direction (XOR with 2 flips direction in our encoding) */
    Direction opposite = (current ^ 0x02) & 0x03;
    uint8_t opposite_bit = 1 << opposite;
    
    /* Check if current direction is valid */
    if (pivot_directions & current_bit) {
        return current;  /* Can continue in same direction */
    }
    
    /* Apply entity rules to choose direction */
    if (entity_rule & RULE_REVERSE) {
        /* Try to reverse direction */
        if (pivot_directions & opposite_bit) {
            return opposite;
        }
    }
    
    if (entity_rule & RULE_CLOCKWISE) {
        /* Try to turn clockwise */
        Direction clockwise = (current + 3) & 0x03;  /* +3 is -1 in mod 4 (right turn) */
        uint8_t clockwise_bit = 1 << clockwise;
        
        if (pivot_directions & clockwise_bit) {
            return clockwise;
        }
    }
    
    if (entity_rule & RULE_COUNTER_CW) {
        /* Try to turn counter-clockwise */
        Direction counter_cw = (current + 1) & 0x03;  /* +1 is left turn */
        uint8_t counter_cw_bit = 1 << counter_cw;
        
        if (pivot_directions & counter_cw_bit) {
            return counter_cw;
        }
    }
    
    /* No preferred direction available, try any valid direction */
    for (int i = 0; i < 4; i++) {
        if (pivot_directions & (1 << i)) {
            return i;
        }
    }
    
    /* No valid direction, return opposite as fallback */
    return opposite;
}

/* Set pivot point with specified directions */
void set_pivot_point(GameState* game, int x, int y, uint8_t directions) {
    /* Calculate index with bit masking for wrapping */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Mark as pivot point in collision map */
    game->collision->map[idx] |= CMAP_PIVOT;
    
    /* Set pivot directions */
    game->collision->pivot_dirs[idx] = directions;
}

/* Get what's at a specific position in the collision map */
uint8_t get_collision_cell(const GameState* game, int x, int y) {
    /* Calculate index with bit masking for wrapping */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    return game->collision->map[idx];
}

/* Set a specific type in the collision map */
void set_collision_cell(GameState* game, int x, int y, uint8_t type, bool value) {
    /* Calculate index with bit masking for wrapping */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    if (value) {
        game->collision->map[idx] |= type;
    } else {
        game->collision->map[idx] &= ~type;
    }
}

/* Check for collisions between player and entities */
bool check_player_entity_collision(const GameState* game) {
    /* Get player position */
    int player_idx = (game->player->pos_y << GRID_WIDTH_SHIFT) | game->player->pos_x;
    
    /* Check if player position has an entity */
    if (game->collision->map[player_idx] & CMAP_ENTITY) {
        return true;
    }
    
    return false;
}
