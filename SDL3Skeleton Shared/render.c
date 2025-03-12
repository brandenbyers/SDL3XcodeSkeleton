/*
 * render.c - Rendering functions for the Bit-Twiddled Game Engine
 *
 * This file contains all rendering-related code, including:
 * - Texture management
 * - Scene drawing with dirty rectangle tracking
 * - Batch rendering for improved performance
 * - Optimized rendering configuration
 */

#include "main.h"

/* Power state colors */
const SDL_Color CELL_COLORS[CELL_MAX] = {
    { 0,   0,   0,   255 },  /* CELL_EMPTY: black */
    { 64,  64,  192, 255 },  /* CELL_WALL: blue */
    { 255, 255, 0,   255 },  /* CELL_ITEM: yellow */
};
const SDL_Color PLAYER_COLOR = { 0, 255, 0, 255 };  /* Player: green */
const SDL_Color GRID_LINE_COLOR = { 32, 32, 32, 255 }; /* Grid lines: dark gray */
const SDL_Color PAUSED_OVERLAY_COLOR = { 128, 128, 128, 192 }; /* Semi-transparent gray */

/*
 * Texture Creation Functions
 */

/* Create background texture containing walls and grid lines */
void create_background_texture(AppState* app) {
    GameState* game = &app->game;
    
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
    }
    
    /* Create texture for walls and grid lines (static elements) */
    app->background_texture = SDL_CreateTexture(
                                                app->renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                WINDOW_WIDTH,
                                                WINDOW_HEIGHT
                                                );
    
    /* Set render target to background texture */
    SDL_SetRenderTarget(app->renderer, app->background_texture);
    
    /* Clear with black background */
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    
    /* Batch render walls */
    SDL_FRect* wall_rects = NULL;
    int wall_count = 0;
    const int max_walls = GRID_SIZE; /* Maximum possible walls */
    
    /* Allocate rectangle array once */
    wall_rects = (SDL_FRect*)SDL_malloc(max_walls * sizeof(SDL_FRect));
    if (!wall_rects) {
        SDL_Log("Failed to allocate rectangles for wall rendering");
        SDL_SetRenderTarget(app->renderer, NULL);
        return;
    }
    
    /* Collect all wall rectangles for batch rendering */
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (get_cell(game, x, y) == CELL_WALL) {
                wall_rects[wall_count].x = x * PIXEL_SCALE;
                wall_rects[wall_count].y = y * PIXEL_SCALE;
                wall_rects[wall_count].w = wall_rects[wall_count].h = PIXEL_SCALE;
                wall_count++;
            }
        }
    }
    
    /* Batch render all wall rectangles at once */
    if (wall_count > 0) {
        SDL_SetRenderDrawColor(app->renderer,
                               CELL_COLORS[CELL_WALL].r,
                               CELL_COLORS[CELL_WALL].g,
                               CELL_COLORS[CELL_WALL].b,
                               CELL_COLORS[CELL_WALL].a);
        SDL_RenderFillRects(app->renderer, wall_rects, wall_count);
    }
    
    /* Free temporary rectangles */
    SDL_free(wall_rects);
    
    /* Draw grid lines */
    SDL_SetRenderDrawColor(app->renderer,
                           GRID_LINE_COLOR.r,
                           GRID_LINE_COLOR.g,
                           GRID_LINE_COLOR.b,
                           GRID_LINE_COLOR.a);
    
    /* Batch the grid lines for more efficient rendering */
    SDL_FRect* grid_lines = (SDL_FRect*)SDL_malloc((GRID_WIDTH + GRID_HEIGHT + 2) * sizeof(SDL_FRect));
    if (!grid_lines) {
        SDL_Log("Failed to allocate rectangles for grid line rendering");
        SDL_SetRenderTarget(app->renderer, NULL);
        return;
    }
    
    int line_count = 0;
    
    /* Vertical grid lines (thin rectangles) */
    for (int i = 0; i <= GRID_WIDTH; i++) {
        grid_lines[line_count].x = i * PIXEL_SCALE;
        grid_lines[line_count].y = 0;
        grid_lines[line_count].w = 1;
        grid_lines[line_count].h = WINDOW_HEIGHT;
        line_count++;
    }
    
    /* Horizontal grid lines (thin rectangles) */
    for (int i = 0; i <= GRID_HEIGHT; i++) {
        grid_lines[line_count].x = 0;
        grid_lines[line_count].y = i * PIXEL_SCALE;
        grid_lines[line_count].w = WINDOW_WIDTH;
        grid_lines[line_count].h = 1;
        line_count++;
    }
    
    /* Batch render all grid lines at once */
    SDL_RenderFillRects(app->renderer, grid_lines, line_count);
    
    /* Free temporary grid lines */
    SDL_free(grid_lines);
    
    /* Reset render target */
    SDL_SetRenderTarget(app->renderer, NULL);
    
    /* Mark grid as updated */
    game->grid_state.cells_changed = false;
    game->grid_state.last_frame_updated = game->frame_count;
}

/* Create all textures */
void create_textures(AppState* app) {
    /* Create background texture (walls and grid lines) */
    create_background_texture(app);
}

/* Destroy all textures */
void destroy_textures(AppState* app) {
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
        app->background_texture = NULL;
    }
}

/*
 * Add a region to the dirty regions list
 */
void add_dirty_region(AppState* app, float x, float y, float w, float h) {
    if (app->dirty_region_count < MAX_DIRTY_REGIONS) {
        app->dirty_regions[app->dirty_region_count].x = x;
        app->dirty_regions[app->dirty_region_count].y = y;
        app->dirty_regions[app->dirty_region_count].w = w;
        app->dirty_regions[app->dirty_region_count].h = h;
        app->dirty_region_count++;
    }
}

/*
 * Rendering Functions
 */

/* Configure renderer for proper scaling and VSync */
void configure_rendering(AppState* app) {
    /* Configure VSync through hints */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    
    /* For Metal renderer, set additional hints */
    const char* renderer_name = SDL_GetRendererName(app->renderer);
    if (renderer_name && SDL_strstr(renderer_name, "metal")) {
        /* Prefer integrated GPU on laptops for power efficiency */
        SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "1");
        
        /* Set maximum FPS based on power mode */
        if (app->is_on_battery) {
            /* On battery, be more aggressive with power saving */
            SDL_SetHint("SDL_METAL_MAX_COMMAND_BUFFERS_PER_FRAME", "1");
        } else {
            /* When plugged in, allow more command buffers */
            SDL_SetHint("SDL_METAL_MAX_COMMAND_BUFFERS_PER_FRAME", "3");
        }
        
        /* Reduce CPU usage by limiting render target changes */
        SDL_SetHint("SDL_METAL_MINIMIZE_TARGET_CHANGES", "1");
    }
    
    /* Set logical scaling */
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(app->renderer, WINDOW_WIDTH, WINDOW_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

/* Calculate and display FPS - only update once per second */
void update_fps(AppState* app) {
    app->fps_count++;
    
    Uint64 current_time = SDL_GetTicks();
    if (current_time - app->last_fps_time >= 1000) {
        app->current_fps = app->fps_count;
        app->fps_count = 0;
        app->last_fps_time = current_time;
        
        char title[128];
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - FPS: %d %s Mode: %s",
                 app->current_fps,
                 app->is_paused ? "[PAUSED]" : "",
                 app->power_mode == 0 ? "Performance" :
                 (app->power_mode == 1 ? "Balanced" : "Efficient"));
        SDL_SetWindowTitle(app->window, title);
    }
}

/* Optimized render function with batch rendering and dirty rect tracking */
void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    
    /* On first render or after changes, regenerate background texture */
    static bool first_render = true;
    if (first_render || game->grid_state.cells_changed) {
        create_background_texture(app);
        first_render = false;
    }
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    /* 1. Render the background (walls and grid lines) */
    if (app->background_texture) {
        SDL_RenderTexture(renderer, app->background_texture, NULL, NULL);
    }
    
    /* 2. Render items in a single batch */
    SDL_FRect* rects = NULL;
    int rect_count = 0;
    const int max_rects = GRID_SIZE; /* Maximum possible items */
    
    /* Allocate rectangle array once */
    rects = (SDL_FRect*)SDL_malloc(max_rects * sizeof(SDL_FRect));
    if (!rects) {
        SDL_Log("Failed to allocate rectangles for batch rendering");
        return;
    }
    
    /* Collect all item rectangles for batch rendering */
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (get_cell(game, x, y) == CELL_ITEM) {
                rects[rect_count].x = x * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rects[rect_count].y = y * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rects[rect_count].w = rects[rect_count].h = PIXEL_SCALE * 0.5f;
                rect_count++;
            }
        }
    }
    
    /* Batch render all item rectangles at once */
    if (rect_count > 0) {
        SDL_SetRenderDrawColor(renderer,
                               CELL_COLORS[CELL_ITEM].r,
                               CELL_COLORS[CELL_ITEM].g,
                               CELL_COLORS[CELL_ITEM].b,
                               CELL_COLORS[CELL_ITEM].a);
        SDL_RenderFillRects(renderer, rects, rect_count);
    }
    
    /* Free temporary rectangles */
    SDL_free(rects);
    
    /* 3. Render player */
    float visual_x, visual_y;
    get_visual_position(&game->player, &visual_x, &visual_y);
    
    SDL_FRect player_rect = {
        visual_x * PIXEL_SCALE,
        visual_y * PIXEL_SCALE,
        PIXEL_SCALE,
        PIXEL_SCALE
    };
    
    /* Track dirty regions for player movement */
    if (game->player.is_moving) {
        /* Add player's previous position to dirty regions */
        add_dirty_region(app,
                         game->player.pos_x * PIXEL_SCALE - PIXEL_SCALE,
                         game->player.pos_y * PIXEL_SCALE - PIXEL_SCALE,
                         PIXEL_SCALE * 3,
                         PIXEL_SCALE * 3);
        
        /* Add player's new position to dirty regions */
        add_dirty_region(app,
                         visual_x * PIXEL_SCALE - PIXEL_SCALE,
                         visual_y * PIXEL_SCALE - PIXEL_SCALE,
                         PIXEL_SCALE * 3,
                         PIXEL_SCALE * 3);
    }
    
    SDL_SetRenderDrawColor(renderer,
                           PLAYER_COLOR.r,
                           PLAYER_COLOR.g,
                           PLAYER_COLOR.b,
                           PLAYER_COLOR.a);
    SDL_RenderFillRect(renderer, &player_rect);
    
    /* 4. If paused, draw a semi-transparent overlay */
    if (app->is_paused && !app->is_in_background) {
        SDL_SetRenderDrawColor(renderer,
                               PAUSED_OVERLAY_COLOR.r,
                               PAUSED_OVERLAY_COLOR.g,
                               PAUSED_OVERLAY_COLOR.b,
                               PAUSED_OVERLAY_COLOR.a);
        SDL_FRect overlay = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &overlay);
    }
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
    
    /* Reset dirty region count */
    app->dirty_region_count = 0;
    
    /* Update FPS counter (only visible in window title, so update less frequently) */
    static Uint64 last_fps_update_time = 0;
    Uint64 current_time = SDL_GetTicks();
    if (current_time - last_fps_update_time > 1000) {
        update_fps(app);
        last_fps_update_time = current_time;
    }
}
