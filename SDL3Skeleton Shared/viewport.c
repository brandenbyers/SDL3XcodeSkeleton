/*
 * viewport.c - Viewport system implementation
 *
 * This file manages the 16:9 viewport into the 64x64 grid.
 * It handles coordinate conversion and cache management.
 */

#include "game.h"
#include "viewport.h"

/*
 * Check if a grid coordinate is within the visible viewport
 */
bool is_in_viewport(const GameState* game, int x, int y) {
    /* Convert to viewport-relative coordinates with wrapping */
    int rel_x = (x - game->viewport->offset_x) & GRID_WIDTH_MASK;
    int rel_y = (y - game->viewport->offset_y) & GRID_HEIGHT_MASK;
    
    /* Check if within viewport bounds */
    return (rel_x < VIEWPORT_WIDTH && rel_y < VIEWPORT_HEIGHT);
}

/*
 * Convert grid coordinates to viewport coordinates
 */
void grid_to_viewport(const GameState* game, int grid_x, int grid_y, int* viewport_x, int* viewport_y) {
    /* Apply viewport offset with wrapping */
    *viewport_x = (grid_x - game->viewport->offset_x) & GRID_WIDTH_MASK;
    *viewport_y = (grid_y - game->viewport->offset_y) & GRID_HEIGHT_MASK;
}

/*
 * Convert viewport coordinates to grid coordinates
 */
void viewport_to_grid(const GameState* game, int viewport_x, int viewport_y, int* grid_x, int* grid_y) {
    /* Apply viewport offset with wrapping */
    *grid_x = (viewport_x + game->viewport->offset_x) & GRID_WIDTH_MASK;
    *grid_y = (viewport_y + game->viewport->offset_y) & GRID_HEIGHT_MASK;
}

/*
 * Update the viewport cache for better CPU cache locality
 */
void update_viewport_cache(GameState* game) {
    /* Fill the viewport cache with current grid data */
    for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
        for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
            /* Get grid coordinates */
            int grid_x, grid_y;
            viewport_to_grid(game, vx, vy, &grid_x, &grid_y);
            
            /* Get cell from main grid */
            CellType cell = get_cell(game, grid_x, grid_y);
            
            /* Store in cache using linear indexing for contiguous memory access */
            int cache_idx = vy * VIEWPORT_WIDTH + vx;
            game->viewport->cache[cache_idx] = cell;
        }
    }
    
    /* Mark cache as valid */
    game->grid_state->cache_valid = true;
}

/*
 * Get cell from viewport cache for efficient rendering
 */
CellType get_cell_from_cache(const GameState* game, int viewport_x, int viewport_y) {
    /* Bounds check */
    if (viewport_x < 0 || viewport_x >= VIEWPORT_WIDTH ||
        viewport_y < 0 || viewport_y >= VIEWPORT_HEIGHT) {
        return CELL_EMPTY;
    }
    
    /* Direct access to the cache with linear indexing */
    int cache_idx = viewport_y * VIEWPORT_WIDTH + viewport_x;
    return (CellType)game->viewport->cache[cache_idx];
}
