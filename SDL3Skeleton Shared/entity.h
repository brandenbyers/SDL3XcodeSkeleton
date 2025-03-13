/*
 * entity.h - Entity system declarations
 *
 * This header defines the frame-based entity system that enables
 * efficient CPU usage while maintaining smooth animations.
 */

#ifndef ENTITY_H
#define ENTITY_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Entity type definitions */
typedef enum {
    ENTITY_PATROL = 0,        /* Simple back-and-forth patrol */
    ENTITY_CLOCKWISE = 1,     /* Always turns right (clockwise) at intersections */
    ENTITY_MAX = 2            /* Maximum number of entity types */
} EntityType;

/* Maximum number of entities */
#define MAX_ENTITIES 16

/* Entity structure - designed for cache efficiency with SoA pattern */
typedef struct EntitySystem {
    uint8_t pos_x[MAX_ENTITIES];          /* Current X positions */
    uint8_t pos_y[MAX_ENTITIES];          /* Current Y positions */
    uint8_t target_x[MAX_ENTITIES];       /* Target X positions */
    uint8_t target_y[MAX_ENTITIES];       /* Target Y positions */
    uint8_t direction[MAX_ENTITIES];      /* Current directions */
    uint8_t entity_type[MAX_ENTITIES];    /* Entity behavior types */
    uint8_t is_active[MAX_ENTITIES];      /* 1 if entity is active, 0 if not */
    uint8_t is_moving[MAX_ENTITIES];      /* 1 if entity is moving, 0 if not */
    uint8_t move_frame[MAX_ENTITIES];     /* Current movement frame */
    uint8_t count;                        /* Number of active entities */
} EntitySystem;

/* Entity colors */
extern const SDL_Color ENTITY_COLORS[ENTITY_MAX];

/* Entity functions */
void init_entity_system(GameState* game);
void add_entity(GameState* game, uint8_t x, uint8_t y, Direction dir, EntityType type);
void update_entities(GameState* game);
void get_entity_visual_position(const GameState* game, int entity_idx, uint64_t current_time,
                                float* visual_x, float* visual_y);
bool is_entity_at_position(const GameState* game, int x, int y);

#endif /* ENTITY_H */
