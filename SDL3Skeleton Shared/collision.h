/*
 * collision.h - Collision system declarations
 *
 * This header defines the collision detection and response system.
 * Uses a grid-based approach with bit manipulation for maximum efficiency.
 */

#ifndef COLLISION_H
#define COLLISION_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Collision map cell contents (bit flags) */
#define CMAP_EMPTY         0x00
#define CMAP_WALL          0x01
#define CMAP_ITEM          0x02
#define CMAP_ENTITY        0x04
#define CMAP_PLAYER        0x08
#define CMAP_PIVOT         0x10

/* Pivot direction flags */
#define PIVOT_UP           0x01
#define PIVOT_RIGHT        0x02
#define PIVOT_DOWN         0x04
#define PIVOT_LEFT         0x08

/* Entity rule flags */
#define RULE_REVERSE       0x01  /* Prefers to reverse direction */
#define RULE_CLOCKWISE     0x02  /* Prefers to turn clockwise */
#define RULE_COUNTER_CW    0x04  /* Prefers to turn counter-clockwise */

/* Collision system structure */
typedef struct CollisionSystem {
    uint8_t map[GRID_SIZE];         /* What's in each cell (bit flags) */
    uint8_t pivot_dirs[GRID_SIZE];  /* Allowed directions at each pivot point */
} CollisionSystem;

/* Initialize the collision system */
void init_collision_system(CollisionSystem* collision);

/* Update the collision map based on entity positions */
void update_collision_map(GameState* game);

/* Check if a move is valid */
bool is_move_valid(const GameState* game, int x, int y, Direction dir);

/* Get possible directions at a position */
uint8_t get_pivot_directions(const GameState* game, int x, int y);

/* Determine new direction based on pivot directions and entity rules */
Direction get_new_direction(uint8_t pivot_directions, uint8_t entity_rule, Direction current);

/* Set pivot point with specified directions */
void set_pivot_point(GameState* game, int x, int y, uint8_t directions);

/* Get what's at a specific position in the collision map */
uint8_t get_collision_cell(const GameState* game, int x, int y);

/* Set a specific type in the collision map */
void set_collision_cell(GameState* game, int x, int y, uint8_t type, bool value);

/* Check for collisions between player and entities */
bool check_player_entity_collision(const GameState* game);

#endif /* COLLISION_H */
