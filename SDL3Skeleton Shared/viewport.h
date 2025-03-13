/*
 * viewport.h - Viewport system declarations
 *
 * This header defines the viewport system which provides a 16:9 view window
 * into the power-of-two-sized grid.
 */

#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Grid change tracking */
typedef struct GridState {
    bool cells_changed;       /* True if any cells changed */
    uint64_t last_frame_updated; /* Last frame the grid texture was updated */
    bool cache_valid;         /* True if viewport cache is valid */
} GridState;

/* Viewport state */
typedef struct ViewportState {
    uint8_t offset_x;         /* X offset of viewport within grid */
    uint8_t offset_y;         /* Y offset of viewport within grid */
    uint8_t cache[VIEWPORT_WIDTH * VIEWPORT_HEIGHT]; /* Cache of visible cells for better locality */
} ViewportState;

/* Viewport functions */
bool is_in_viewport(const GameState* game, int x, int y);
void grid_to_viewport(const GameState* game, int grid_x, int grid_y, int* viewport_x, int* viewport_y);
void viewport_to_grid(const GameState* game, int viewport_x, int viewport_y, int* grid_x, int* grid_y);
void update_viewport_cache(GameState* game);
CellType get_cell_from_cache(const GameState* game, int viewport_x, int viewport_y);

#endif /* VIEWPORT_H */
