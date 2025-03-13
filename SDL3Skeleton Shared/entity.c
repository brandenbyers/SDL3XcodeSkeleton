/*
 * entity.c - Entity system implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the event-driven entity system that uses minimal CPU.
 * Entities only update when they reach pivot points or when they need to
 * check for collisions with the player.
 */

#include "main.h"

/* Entity colors */
const SDL_Color ENTITY_COLORS[ENTITY_MAX] = {
    { 255, 0, 0, 255 },    /* ENTITY_PATROL: red */
    { 255, 128, 0, 255 }   /* ENTITY_CLOCKWISE: orange */
};

/*
 * Initialize the entity system
 */
void init_entity_system(GameState* game) {
    /* Clear all entity data */
    memset(&game->entities, 0, sizeof(EntitySystem));
    
    /* Clear all pivot data */
    memset(&game->pivots, 0, sizeof(PivotSystem));
    
    /* No entities active at start */
    game->entities.count = 0;
    game->entities.next_event_time = 0;
    game->entities.needs_update = false;
}

/*
 * Add a new entity to the system
 */
void add_entity(GameState* game, uint8_t x, uint8_t y, Direction dir, EntityType type) {
    /* Ensure we don't exceed max entities */
    if (game->entities.count >= MAX_ENTITIES) {
        return;
    }
    
    /* Get next available entity index */
    uint8_t idx = game->entities.count;
    
    /* Initialize entity data */
    game->entities.pos_x[idx] = x;
    game->entities.pos_y[idx] = y;
    game->entities.target_x[idx] = x;
    game->entities.target_y[idx] = y;
    game->entities.direction[idx] = dir;
    game->entities.entity_type[idx] = type;
    game->entities.is_active[idx] = 1;
    game->entities.is_moving[idx] = 0;
    game->entities.move_frame[idx] = 0;
    
    /* Calculate time to first pivot point */
    calculate_next_pivot_collision(game, idx);
    
    /* Increment entity count */
    game->entities.count++;
    
    /* Mark system as needing an update */
    game->entities.needs_update = true;
}

/*
 * Calculate when entity will hit next pivot point
 */
void calculate_next_pivot_collision(GameState* game, int entity_idx) {
    /* Get current position and direction */
    uint8_t x = game->entities.pos_x[entity_idx];
    uint8_t y = game->entities.pos_y[entity_idx];
    Direction dir = game->entities.direction[entity_idx];
    
    /* Calculate steps to next pivot point */
    int steps = 0;
    int next_x = x;
    int next_y = y;
    
    /* Search up to GRID_SIZE steps (cannot be more in a wrapped grid) */
    for (int i = 0; i < GRID_SIZE; i++) {
        /* Get next position */
        get_target_position(next_x, next_y, dir, &next_x, &next_y);
        steps++;
        
        /* Check if we hit a wall */
        if (get_cell(game, next_x, next_y) == CELL_WALL) {
            /* We'll reverse direction at walls */
            steps--; /* Back up one step */
            next_x = (next_x - DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
            next_y = (next_y - DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
            
            /* Set the next update time based on steps */
            game->entities.next_update_time[entity_idx] = game->last_tick_time +
            (steps * FRAMES_PER_TILE * LOGIC_TICK_MS);
            
            /* Start moving */
            game->entities.is_moving[entity_idx] = 1;
            game->entities.target_x[entity_idx] = next_x;
            game->entities.target_y[entity_idx] = next_y;
            
            /* Update the next event time for the entity system */
            if (game->entities.next_event_time == 0 ||
                game->entities.next_update_time[entity_idx] < game->entities.next_event_time) {
                game->entities.next_event_time = game->entities.next_update_time[entity_idx];
            }
            
            return;
        }
        
        /* Check if we hit a pivot point */
        int idx = (next_y << GRID_WIDTH_SHIFT) | next_x;
        if (game->pivots.has_pivot[idx]) {
            /* Set the next update time based on steps */
            game->entities.next_update_time[entity_idx] = game->last_tick_time +
            (steps * FRAMES_PER_TILE * LOGIC_TICK_MS);
            
            /* Start moving */
            game->entities.is_moving[entity_idx] = 1;
            game->entities.target_x[entity_idx] = next_x;
            game->entities.target_y[entity_idx] = next_y;
            
            /* Update the next event time for the entity system */
            if (game->entities.next_event_time == 0 ||
                game->entities.next_update_time[entity_idx] < game->entities.next_event_time) {
                game->entities.next_event_time = game->entities.next_update_time[entity_idx];
            }
            
            return;
        }
    }
    
    /* If we get here, there are no pivot points in the current direction */
    /* This should not happen in a well-designed level, but handle it safely */
    game->entities.next_update_time[entity_idx] = game->last_tick_time + LOGIC_TICK_MS * 60;
    game->entities.is_moving[entity_idx] = 0;
}

/*
 * Determine new direction based on entity type and pivot behavior
 */
Direction determine_new_direction(uint8_t entity_type, Direction current_dir, uint8_t pivot_behavior) {
    /* Process pivot point behavior first */
    if (pivot_behavior & PIVOT_REVERSE) {
        /* Reverse direction (XOR with 2 flips direction) */
        return (Direction)((current_dir ^ 0x02) & 0x03);
    }
    
    if (pivot_behavior & PIVOT_TURN_RIGHT) {
        /* Turn 90 degrees right (subtract 1 mod 4) */
        return (Direction)((current_dir + 3) & 0x03);
    }
    
    if (pivot_behavior & PIVOT_TURN_LEFT) {
        /* Turn 90 degrees left (add 1 mod 4) */
        return (Direction)((current_dir + 1) & 0x03);
    }
    
    /* Process entity-specific behavior if conditional or no specific pivot behavior */
    if ((pivot_behavior & PIVOT_CONDITIONAL) || pivot_behavior == 0) {
        switch (entity_type) {
            case ENTITY_PATROL:
                /* Patrol entities reverse direction at pivot points */
                return (Direction)((current_dir ^ 0x02) & 0x03);
                
            case ENTITY_CLOCKWISE:
                /* Clockwise entities always turn right */
                return (Direction)((current_dir + 3) & 0x03);
                
            default:
                /* Default: continue in same direction */
                return current_dir;
        }
    }
    
    /* Default: continue in same direction */
    return current_dir;
}

/*
 * Process an entity collision with a pivot point
 */
void process_entity_collision(GameState* game, int entity_idx) {
    /* Get pivot behavior at entity position */
    uint8_t x = game->entities.target_x[entity_idx];
    uint8_t y = game->entities.target_y[entity_idx];
    uint8_t entity_type = game->entities.entity_type[entity_idx];
    Direction current_dir = (Direction)game->entities.direction[entity_idx];
    
    /* Get pivot behavior */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    uint8_t pivot_behavior = 0;
    
    if (game->pivots.has_pivot[idx]) {
        pivot_behavior = game->pivots.behavior[idx];
    } else {
        /* If we hit a wall, reverse direction */
        if (get_cell(game, x, y) == CELL_WALL) {
            pivot_behavior = PIVOT_REVERSE;
        }
    }
    
    /* Determine new direction */
    Direction new_dir = determine_new_direction(entity_type, current_dir, pivot_behavior);
    
    /* Update entity position and direction */
    game->entities.pos_x[entity_idx] = x;
    game->entities.pos_y[entity_idx] = y;
    game->entities.direction[entity_idx] = new_dir;
    game->entities.is_moving[entity_idx] = 0;
    game->entities.move_frame[entity_idx] = 0;
    
    /* Calculate next pivot collision */
    calculate_next_pivot_collision(game, entity_idx);
}

/*
 * Get current visual position of an entity (for rendering)
 */
void get_entity_visual_position(const GameState* game, int entity_idx, float* visual_x, float* visual_y) {
    if (!game->entities.is_moving[entity_idx] ||
        game->entities.move_frame[entity_idx] >= FRAMES_PER_TILE) {
        /* Not moving or movement complete - use exact grid position */
        *visual_x = (float)game->entities.pos_x[entity_idx];
        *visual_y = (float)game->entities.pos_y[entity_idx];
        return;
    }
    
    /* Calculate interpolation factor (0.0 to 1.0) */
    float t = (float)game->entities.move_frame[entity_idx] / FRAMES_PER_TILE;
    
    /* Start position */
    float start_x = (float)game->entities.pos_x[entity_idx];
    float start_y = (float)game->entities.pos_y[entity_idx];
    
    /* Target position */
    float target_x = (float)game->entities.target_x[entity_idx];
    float target_y = (float)game->entities.target_y[entity_idx];
    
    /* Check if wrapping horizontally */
    int dx = abs((int)game->entities.target_x[entity_idx] - (int)game->entities.pos_x[entity_idx]);
    if (dx > GRID_WIDTH/2) {
        /* We're wrapping around the edge */
        if (game->entities.target_x[entity_idx] < game->entities.pos_x[entity_idx]) {
            /* Moving right to left across the edge */
            target_x += GRID_WIDTH;
        } else {
            /* Moving left to right across the edge */
            start_x += GRID_WIDTH;
        }
    }
    
    /* Check if wrapping vertically */
    int dy = abs((int)game->entities.target_y[entity_idx] - (int)game->entities.pos_y[entity_idx]);
    if (dy > GRID_HEIGHT/2) {
        /* We're wrapping around the edge */
        if (game->entities.target_y[entity_idx] < game->entities.pos_y[entity_idx]) {
            /* Moving bottom to top across the edge */
            target_y += GRID_HEIGHT;
        } else {
            /* Moving top to bottom across the edge */
            start_y += GRID_HEIGHT;
        }
    }
    
    /* Linear interpolation between start and target */
    *visual_x = start_x + (target_x - start_x) * t;
    *visual_y = start_y + (target_y - start_y) * t;
    
    /* Normalize coordinates to grid range */
    *visual_x = fmodf(*visual_x, GRID_WIDTH);
    *visual_y = fmodf(*visual_y, GRID_HEIGHT);
    
    /* Handle negative coordinates from wrapping */
    if (*visual_x < 0) *visual_x += GRID_WIDTH;
    if (*visual_y < 0) *visual_y += GRID_HEIGHT;
}

/*
 * Set a pivot point with specific behavior
 */
void set_pivot_point(GameState* game, int x, int y, uint8_t behavior) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Set cell type to pivot */
    set_cell(game, x, y, CELL_PIVOT);
    
    /* Store pivot behavior */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    game->pivots.behavior[idx] = behavior;
    game->pivots.has_pivot[idx] = true;
}

/*
 * Get pivot behavior at a specific position
 */
uint8_t get_pivot_behavior(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Get pivot behavior */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    if (game->pivots.has_pivot[idx]) {
        return game->pivots.behavior[idx];
    }
    
    return 0; /* No pivot point */
}

/*
 * Check if an entity is at a specific position
 */
bool is_entity_at_position(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Check all active entities */
    for (int i = 0; i < game->entities.count; i++) {
        if (game->entities.is_active[i]) {
            if (game->entities.pos_x[i] == x && game->entities.pos_y[i] == y) {
                return true;
            }
            
            /* Also check target position if moving */
            if (game->entities.is_moving[i]) {
                if (game->entities.target_x[i] == x && game->entities.target_y[i] == y) {
                    /* Check if we're close to the target */
                    if (game->entities.move_frame[i] >= FRAMES_PER_TILE / 2) {
                        return true;
                    }
                }
            }
        }
    }
    
    return false;
}

/*
 * Check for collision between player and any entity
 */
bool check_player_entity_collision(GameState* game) {
    /* Get player position */
    int player_x = game->player.pos_x;
    int player_y = game->player.pos_y;
    
    /* Check all active entities */
    for (int i = 0; i < game->entities.count; i++) {
        if (game->entities.is_active[i]) {
            /* Get entity position */
            int entity_x = game->entities.pos_x[i];
            int entity_y = game->entities.pos_y[i];
            
            /* First, check exact position match */
            if (entity_x == player_x && entity_y == player_y) {
                return true;
            }
            
            /* Check if player and entity are moving toward each other */
            if (game->player.is_moving && game->entities.is_moving[i]) {
                /* Get player target position */
                int player_target_x = game->player.target_x;
                int player_target_y = game->player.target_y;
                
                /* Get entity target position */
                int entity_target_x = game->entities.target_x[i];
                int entity_target_y = game->entities.target_y[i];
                
                /* Check if player is moving to entity's current position */
                if (player_target_x == entity_x && player_target_y == entity_y) {
                    /* Check if entity is not near its target yet */
                    if (game->entities.move_frame[i] < FRAMES_PER_TILE / 2) {
                        return true;
                    }
                }
                
                /* Check if entity is moving to player's current position */
                if (entity_target_x == player_x && entity_target_y == player_y) {
                    /* Check if player is not near its target yet */
                    if (game->player.move_frame < FRAMES_PER_TILE / 2) {
                        return true;
                    }
                }
                
                /* Check if player and entity are moving to the same position */
                if (player_target_x == entity_target_x && player_target_y == entity_target_y) {
                    return true;
                }
                
                /* Check if they're passing through each other */
                if (player_target_x == entity_x && player_target_y == entity_y &&
                    entity_target_x == player_x && entity_target_y == player_y) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

/*
 * Update all entities
 */
void update_entity_system(GameState* game, uint64_t current_time) {
    bool any_update = false;
    uint64_t next_event = 0;
    
    /* First, check if any entity needs updating at this time */
    if (!game->entities.needs_update || current_time < game->entities.next_event_time) {
        /* No updates needed yet */
        
        /* Special case: if player is moving, we still need to check for collisions */
        if (game->player.is_moving) {
            if (check_player_entity_collision(game)) {
                /* Collision detected - reset the level */
                set_restart_requested(&game->input, true);
                return;
            }
        }
        
        return;
    }
    
    /* Process all active entities */
    for (int i = 0; i < game->entities.count; i++) {
        if (!game->entities.is_active[i]) {
            continue;
        }
        
        /* Check if this entity needs updating */
        if (current_time >= game->entities.next_update_time[i]) {
            /* Entity has reached a pivot point - process the collision */
            process_entity_collision(game, i);
            any_update = true;
        } else {
            /* Entity is still moving - update its position */
            if (game->entities.is_moving[i]) {
                /* Calculate how many frames have passed */
                uint64_t time_elapsed = current_time - (game->entities.next_update_time[i] -
                                                        (FRAMES_PER_TILE * LOGIC_TICK_MS));
                game->entities.move_frame[i] = time_elapsed / LOGIC_TICK_MS;
                
                /* Cap at FRAMES_PER_TILE */
                if (game->entities.move_frame[i] >= FRAMES_PER_TILE) {
                    /* Reached target - process collision next update */
                    game->entities.next_update_time[i] = current_time;
                    any_update = true;
                }
            }
        }
        
        /* Track the next update time */
        if (game->entities.next_update_time[i] > current_time) {
            if (next_event == 0 || game->entities.next_update_time[i] < next_event) {
                next_event = game->entities.next_update_time[i];
            }
        }
    }
    
    /* Check for player-entity collisions */
    if (game->player.is_moving || any_update) {
        if (check_player_entity_collision(game)) {
            /* Collision detected - reset the level */
            set_restart_requested(&game->input, true);
            return;
        }
    }
    
    /* Update the next event time */
    game->entities.next_event_time = next_event;
    game->entities.needs_update = (next_event > 0);
}
