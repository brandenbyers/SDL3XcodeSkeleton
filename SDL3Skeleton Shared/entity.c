/*
 * entity.c - Entity system implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the frame-based entity system that uses minimal CPU.
 * Entities update their position each frame, with direction changes at pivot points.
 */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "game.h"
#include "entity.h"
#include "collision.h"
#include "input.h"
#include "viewport.h"

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
    memset(game->entities, 0, sizeof(EntitySystem));
    
    /* No entities active at start */
    game->entities->count = 0;
}

/*
 * Add a new entity to the system
 */
void add_entity(GameState* game, uint8_t x, uint8_t y, Direction dir, EntityType type) {
    /* Ensure we don't exceed max entities */
    if (game->entities->count >= MAX_ENTITIES) {
        return;
    }
    
    /* Get next available entity index */
    uint8_t idx = game->entities->count;
    
    /* Initialize entity data */
    game->entities->pos_x[idx] = x;
    game->entities->pos_y[idx] = y;
    game->entities->target_x[idx] = x;
    game->entities->target_y[idx] = y;
    game->entities->direction[idx] = dir;
    game->entities->entity_type[idx] = type;
    game->entities->is_active[idx] = 1;
    game->entities->is_moving[idx] = 0;
    game->entities->move_frame[idx] = 0;
    
    /* Add to collision map */
    set_collision_cell(game, x, y, CMAP_ENTITY, true);
    
    /* Increment entity count */
    game->entities->count++;
}

/*
 * Update all entities (called once per frame)
 */
void update_entities(GameState* game) {
    /* Process all active entities */
    for (int i = 0; i < game->entities->count; i++) {
        if (!game->entities->is_active[i]) {
            continue;
        }
        
        /* If entity is not moving, start movement */
        if (!game->entities->is_moving[i]) {
            /* Get current position and direction */
            uint8_t x = game->entities->pos_x[i];
            uint8_t y = game->entities->pos_y[i];
            Direction dir = game->entities->direction[i];
            
            /* Check if at a pivot point */
            uint8_t pivot_directions = get_pivot_directions(game, x, y);
            if (pivot_directions) {
                /* Determine new direction based on entity type */
                uint8_t entity_rule;
                switch (game->entities->entity_type[i]) {
                    case ENTITY_PATROL:
                        entity_rule = RULE_REVERSE;
                        break;
                    case ENTITY_CLOCKWISE:
                        entity_rule = RULE_CLOCKWISE;
                        break;
                    default:
                        entity_rule = 0;
                        break;
                }
                
                /* Apply rules to get new direction */
                Direction new_dir = get_new_direction(pivot_directions, entity_rule, dir);
                game->entities->direction[i] = new_dir;
                dir = new_dir;
            }
            
            /* Calculate target position */
            int next_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
            int next_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
            
            /* Check if we can move in this direction */
            if (!(get_collision_cell(game, next_x, next_y) & CMAP_WALL)) {
                /* Start moving to target position */
                game->entities->target_x[i] = next_x;
                game->entities->target_y[i] = next_y;
                game->entities->is_moving[i] = 1;
                game->entities->move_frame[i] = 0;
            } else {
                /* Can't move, reverse direction */
                game->entities->direction[i] = (dir ^ 0x02) & 0x03;  /* Flip direction */
            }
        }
        else {
            /* Entity is moving, update movement progress */
            game->entities->move_frame[i]++;
            
            /* Check if movement is complete */
            if (game->entities->move_frame[i] >= FRAMES_PER_TILE) {
                /* Update position to target */
                uint8_t old_x = game->entities->pos_x[i];
                uint8_t old_y = game->entities->pos_y[i];
                
                game->entities->pos_x[i] = game->entities->target_x[i];
                game->entities->pos_y[i] = game->entities->target_y[i];
                game->entities->is_moving[i] = 0;
                game->entities->move_frame[i] = 0;
                
                /* Update collision map - remove from old position, add to new position */
                set_collision_cell(game, old_x, old_y, CMAP_ENTITY, false);
                set_collision_cell(game, game->entities->pos_x[i], game->entities->pos_y[i], CMAP_ENTITY, true);
            }
        }
    }
}

/*
 * Get current visual position of an entity (for rendering)
 */
void get_entity_visual_position(const GameState* game, int entity_idx, uint64_t current_time,
                                float* visual_x, float* visual_y) {
    if (!game->entities->is_active[entity_idx] || !game->entities->is_moving[entity_idx]) {
        /* Not moving - use exact grid position */
        *visual_x = (float)game->entities->pos_x[entity_idx];
        *visual_y = (float)game->entities->pos_y[entity_idx];
        return;
    }
    
    /* Calculate how far along the path we are (0.0 to 1.0) */
    float progress = (float)game->entities->move_frame[entity_idx] / FRAMES_PER_TILE;
    
    /* Get start and target positions */
    float start_x = (float)game->entities->pos_x[entity_idx];
    float start_y = (float)game->entities->pos_y[entity_idx];
    float target_x = (float)game->entities->target_x[entity_idx];
    float target_y = (float)game->entities->target_y[entity_idx];
    
    /* Check if wrapping horizontally */
    int dx = abs((int)target_x - (int)start_x);
    if (dx > GRID_WIDTH/2) {
        /* We're wrapping around the edge */
        if (target_x < start_x) {
            /* Moving right to left across the edge */
            target_x += GRID_WIDTH;
        } else {
            /* Moving left to right across the edge */
            start_x += GRID_WIDTH;
        }
    }
    
    /* Check if wrapping vertically */
    int dy = abs((int)target_y - (int)start_y);
    if (dy > GRID_HEIGHT/2) {
        /* We're wrapping around the edge */
        if (target_y < start_y) {
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

/*
 * Check if an entity is at a specific position
 */
bool is_entity_at_position(const GameState* game, int x, int y) {
    /* Use the collision map for efficient checking */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Check if the cell has an entity */
    return (get_collision_cell(game, x, y) & CMAP_ENTITY) != 0;
}
