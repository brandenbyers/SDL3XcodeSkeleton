/*
 * collision.c - Collision and movement system implementation
 *
 * This file contains the grid-based collision detection and movement system.
 * Uses bit manipulation for maximum efficiency.
 */

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#include "collision.h"
#include "game.h"
#include "entity.h"
#include "input.h"
#include "level.h"

/*
 * Collision System Functions
 */

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

/*
 * Movement Functions (Merged from physics.c)
 */

/* Get current direction */
Direction get_direction(const MovementState* movement) {
    return (Direction)movement->direction;
}

/* Set current direction */
void set_direction(MovementState* movement, Direction dir) {
    movement->direction = dir;
}

/* Check if directions are opposite */
bool are_directions_opposite(Direction dir1, Direction dir2) {
    /* If either direction is NONE, they're not opposite */
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) return false;
    
    /* Directions are opposite if they differ by 2 (when 2-bit values) */
    return ((dir1 ^ dir2) == 2);  /* XOR with 2 checks if bits flipped */
}

/* Start Movement */
bool start_movement(GameState* game, Direction dir) {
    MovementState* movement = game->player;
    
    /* Check if direction is valid */
    if (dir == DIR_NONE) return false;
    
    /* Calculate target position with bit masking for wrapping */
    int target_x = (movement->pos_x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    int target_y = (movement->pos_y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
    
    /* Check if the move is valid using collision map */
    if (!is_move_valid(game, movement->pos_x, movement->pos_y, dir)) {
        return false;
    }
    
    /* Update movement state */
    movement->target_x = target_x;
    movement->target_y = target_y;
    set_direction(movement, dir);
    movement->is_moving = true;
    movement->just_started = true;
    movement->move_frame = 0;
    
    return true;
}

/* Calculate visual position based on movement state */
void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y) {
    if (!movement->is_moving || movement->move_frame >= FRAMES_PER_TILE) {
        /* Not moving or movement complete - use exact grid position */
        *visual_x = (float)movement->pos_x;
        *visual_y = (float)movement->pos_y;
        return;
    }
    
    /* Calculate progress (0.0 to 1.0) */
    float progress = (float)movement->move_frame / FRAMES_PER_TILE;
    
    /* Get start and target positions */
    float start_x = (float)movement->pos_x;
    float start_y = (float)movement->pos_y;
    float target_x = (float)movement->target_x;
    float target_y = (float)movement->target_y;
    
    /* Check if wrapping horizontally */
    int dx = abs((int)movement->target_x - (int)movement->pos_x);
    if (dx > GRID_WIDTH/2) {
        /* We're wrapping around the edge */
        if (movement->target_x < movement->pos_x) {
            /* Moving right to left across the edge */
            target_x += GRID_WIDTH;
        } else {
            /* Moving left to right across the edge */
            start_x += GRID_WIDTH;
        }
    }
    
    /* Check if wrapping vertically */
    int dy = abs((int)movement->target_y - (int)movement->pos_y);
    if (dy > GRID_HEIGHT/2) {
        /* We're wrapping around the edge */
        if (movement->target_y < movement->pos_y) {
            /* Moving bottom to top across the edge */
            target_y += GRID_HEIGHT;
        } else {
            /* Moving top to bottom across the edge */
            start_y += GRID_HEIGHT;
        }
    }
    
    /* Linear interpolation between start and target */
    *visual_x = start_x + (target_x - start_x) * progress;
    *visual_y = start_y + (target_y - start_y) * progress;
    
    /* Normalize coordinates to grid range */
    *visual_x = fmodf(*visual_x, GRID_WIDTH);
    *visual_y = fmodf(*visual_y, GRID_HEIGHT);
    
    /* Handle negative coordinates from wrapping */
    if (*visual_x < 0) *visual_x += GRID_WIDTH;
    if (*visual_y < 0) *visual_y += GRID_HEIGHT;
}

/* Handle movement completion and start next movement if needed */
void complete_movement(GameState* game) {
    MovementState* movement = game->player;
    InputState* input = game->input;
    
    /* Update position to target */
    movement->pos_x = movement->target_x;
    movement->pos_y = movement->target_y;
    movement->move_frame = 0;
    
    /* Check if target has an item */
    if (get_collision_cell(game, movement->pos_x, movement->pos_y) & CMAP_ITEM) {
        /* Collect the item */
        set_cell(game, movement->pos_x, movement->pos_y, CELL_EMPTY);
        set_collision_cell(game, movement->pos_x, movement->pos_y, CMAP_ITEM, false);
        
        /* Track item collection for level completion */
        track_item_collected(game);
    }
    
    /* Check if we should continue moving */
    Direction next_dir = DIR_NONE;
    
    /* Priority 1: Use buffered direction if valid */
    if (has_buffered_dir(input) &&
        is_move_valid(game, movement->pos_x, movement->pos_y, input->buffered_dir)) {
        next_dir = input->buffered_dir;
        set_has_buffered(input, false);
    }
    /* Priority 2: Continue in same direction if key still held */
    else {
        Direction current_dir = get_direction(movement);
        if (is_key_pressed(input, current_dir) &&
            is_move_valid(game, movement->pos_x, movement->pos_y, current_dir)) {
            next_dir = current_dir;
        }
        /* Priority 3: Check for any held direction key */
        else {
            /* Use standard bit check instead of bit scan for compatibility */
            uint8_t keys = input->key_states & 0x0F; /* Get just direction bits */
            for (int i = 0; i < DIR_COUNT; i++) {
                if ((keys & (1 << i)) && is_move_valid(game, movement->pos_x, movement->pos_y, i)) {
                    next_dir = i;
                    break;
                }
            }
        }
    }
    
    /* Start next movement or stop */
    if (next_dir != DIR_NONE) {
        start_movement(game, next_dir);
    } else {
        movement->is_moving = false;
        set_direction(movement, DIR_NONE);
    }
}
