/*
 * render.h - Rendering system declarations
 *
 * This header defines the rendering system that displays the game state.
 */

#ifndef RENDER_H
#define RENDER_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include "game.h"
#include "entity.h"

/* Power state colors */
extern const SDL_Color CELL_COLORS[CELL_MAX];  /* Colors for different cell types */
extern const SDL_Color PLAYER_COLOR;           /* Player color */
extern const SDL_Color GRID_LINE_COLOR;        /* Grid line color */
extern const SDL_Color PAUSED_OVERLAY_COLOR;   /* Semi-transparent overlay for paused state */

/* Rendering functions */
void render_game(AppState* app);
void create_textures(AppState* app);
void destroy_textures(AppState* app);
void create_background_texture(AppState* app);
void configure_rendering(AppState* app);
void update_fps(AppState* app);
void add_dirty_region(AppState* app, float x, float y, float w, float h);

#endif /* RENDER_H */
