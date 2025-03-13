/*
 * render.c - Rendering functions for the Bit-Twiddled Game Engine
 *
 * This file contains all rendering-related code, including:
 * - Texture management
 * - Scene drawing with dirty rectangle tracking
 * - Batch rendering for improved performance
 * - Optimized rendering configuration
 * - Viewport rendering
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
    
    /* Collect all wall rectangles for batch rendering - using cached values for efficiency */
    for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
        for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
            /* Get cell directly from viewport cache - much better cache locality */
            int cache_idx = vy * VIEWPORT_WIDTH + vx;
            CellType cell_type = (CellType)game->viewport.cache[cache_idx];
            
            if (cell_type == CELL_WALL) {
                wall_rects[wall_count].x = vx * PIXEL_SCALE;
                wall_rects[wall_count].y = vy * PIXEL_SCALE;
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
    SDL_FRect* grid_lines = (SDL_FRect*)SDL_malloc((VIEWPORT_WIDTH + VIEWPORT_HEIGHT + 2) * sizeof(SDL_FRect));
    if (!grid_lines) {
        SDL_Log("Failed to allocate rectangles for grid line rendering");
        SDL_SetRenderTarget(app->renderer, NULL);
        return;
    }
    
    int line_count = 0;
    
    /* Vertical grid lines (thin rectangles) */
    for (int i = 0; i <= VIEWPORT_WIDTH; i++) {
        grid_lines[line_count].x = i * PIXEL_SCALE;
        grid_lines[line_count].y = 0;
        grid_lines[line_count].w = 1;
        grid_lines[line_count].h = WINDOW_HEIGHT;
        line_count++;
    }
    
    /* Horizontal grid lines (thin rectangles) */
    for (int i = 0; i <= VIEWPORT_HEIGHT; i++) {
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

/* Configure renderer with platform-specific optimizations */
void configure_rendering(AppState* app) {
    const char* renderer_name = SDL_GetRendererName(app->renderer);
    
    if (app->power_mode == POWER_MODE_EFFICIENT) {
        /* When in efficient mode, aggressively reduce rendering overhead */
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");                     /* Disable VSync */
        SDL_SetHint("SDL_RENDER_BATCHING", "1");                     /* Enable batching */
        SDL_SetHint("SDL_RENDER_LINE_METHOD", "3");                  /* Fastest line method */
        
        if (renderer_name && SDL_strstr(renderer_name, "metal")) {
            /* Metal-specific extreme power saving */
            SDL_SetHint("SDL_METAL_FORCE_LOW_POWER_DEVICE", "1");    /* Force low power device */
            SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "1");   /* Prefer integrated GPU */
            SDL_SetHint("SDL_METAL_FORCE_DEPTH_STENCIL_SHARED", "1");/* Additional optimization */
            SDL_SetHint("SDL_METAL_MINIMIZE_TARGET_CHANGES", "1");   /* Reduce target changes */
            SDL_SetHint("SDL_METAL_MAX_COMMAND_BUFFERS_PER_FRAME", "1"); /* Single command buffer */
            SDL_SetHint("SDL_METAL_NEAREST_FILTERING", "1");         /* Use nearest filtering */
        }
    } else {
        /* Performance or balanced mode - optimize for smooth gameplay */
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");                     /* Enable VSync */
        SDL_SetHint("SDL_RENDER_BATCHING", "1");                     /* Enable batching */
        
        if (renderer_name && SDL_strstr(renderer_name, "metal")) {
            /* Metal-specific balanced settings */
            if (app->is_on_battery) {
                SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "1");/* Prefer integrated GPU */
            } else {
                SDL_SetHint("SDL_METAL_PREFER_LOW_POWER_DEVICE", "0");/* Allow discrete GPU */
            }
        }
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

/* Ultra-optimized render function */
void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    
    /* Static optimization - track if content has actually changed */
    static uint32_t last_render_frame = 0;
    static bool player_was_moving = false;
    
    /* Skip rendering completely if game state hasn't changed */
    if (last_render_frame == game->frame_count &&
        player_was_moving == game->player.is_moving &&
        !app->is_paused) {
        /* No change at all - skip rendering completely */
        return;
    }
    
    /* Track state for next time */
    last_render_frame = game->frame_count;
    player_was_moving = game->player.is_moving;
    
    /* On first render or after changes, regenerate background texture */
    if (game->grid_state.cells_changed) {
        /* Ensure cache is up-to-date before rendering */
        if (!game->grid_state.cache_valid) {
            update_viewport_cache(&app->game);
        }
        create_background_texture(app);
    }
    
    /* Clear the screen only on context changes or when paused */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    /* 1. Render the background (walls and grid lines) */
    if (app->background_texture) {
        SDL_RenderTexture(renderer, app->background_texture, NULL, NULL);
    }
    
    /* 2. Batch render all items */
    SDL_FRect* rects = (SDL_FRect*)SDL_malloc(GRID_SIZE * sizeof(SDL_FRect));
    if (rects) {
        int rect_count = 0;
        
        /* Collect all item rectangles for batch rendering - using cached values for efficiency */
        for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
            for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
                /* Get cell directly from viewport cache - much better cache locality */
                int cache_idx = vy * VIEWPORT_WIDTH + vx;
                CellType cell_type = (CellType)game->viewport.cache[cache_idx];
                
                if (cell_type == CELL_ITEM) {
                    rects[rect_count].x = vx * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                    rects[rect_count].y = vy * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
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
        
        SDL_free(rects);
    }
    
    /* 3. Render player as a single rect */
    float visual_x, visual_y;
    get_visual_position(&game->player, &visual_x, &visual_y);
    
    /* Convert the player's grid position to viewport position */
    int viewport_x, viewport_y;
    grid_to_viewport(game, (int)visual_x, (int)visual_y, &viewport_x, &viewport_y);
    
    /* Calculate fractional part for smooth movement */
    float frac_x = visual_x - (int)visual_x;
    float frac_y = visual_y - (int)visual_y;
    
    /* Create rectangle for player in viewport coordinates */
    SDL_FRect player_rect = {
        (viewport_x + frac_x) * PIXEL_SCALE,
        (viewport_y + frac_y) * PIXEL_SCALE,
        PIXEL_SCALE,
        PIXEL_SCALE
    };
    
    /* Special case for wrapping at viewport edges */
    bool is_wrapping_x = false;
    bool is_wrapping_y = false;
    
    /* Check if we're moving across a viewport edge with wrapping */
    if (game->player.is_moving) {
        int target_viewport_x, target_viewport_y;
        grid_to_viewport(game, game->player.target_x, game->player.target_y,
                         &target_viewport_x, &target_viewport_y);
        
        /* Check for horizontal wrapping */
        if (abs(target_viewport_x - viewport_x) > VIEWPORT_WIDTH/2) {
            is_wrapping_x = true;
        }
        
        /* Check for vertical wrapping */
        if (abs(target_viewport_y - viewport_y) > VIEWPORT_HEIGHT/2) {
            is_wrapping_y = true;
        }
    }
    
    /* Draw the main player rectangle */
    SDL_SetRenderDrawColor(renderer,
                           PLAYER_COLOR.r,
                           PLAYER_COLOR.g,
                           PLAYER_COLOR.b,
                           PLAYER_COLOR.a);
    SDL_RenderFillRect(renderer, &player_rect);
    
    /* If wrapping, draw additional player rectangles at the wrapped positions */
    if (is_wrapping_x || is_wrapping_y) {
        SDL_FRect wrap_rect = player_rect;
        
        if (is_wrapping_x) {
            /* Draw player wrapping horizontally */
            wrap_rect.x = player_rect.x < WINDOW_WIDTH/2 ?
            player_rect.x + WINDOW_WIDTH :
            player_rect.x - WINDOW_WIDTH;
            SDL_RenderFillRect(renderer, &wrap_rect);
        }
        
        if (is_wrapping_y) {
            /* Draw player wrapping vertically */
            wrap_rect.x = player_rect.x; /* Reset x from previous wrapping if any */
            wrap_rect.y = player_rect.y < WINDOW_HEIGHT/2 ?
            player_rect.y + WINDOW_HEIGHT :
            player_rect.y - WINDOW_HEIGHT;
            SDL_RenderFillRect(renderer, &wrap_rect);
            
            if (is_wrapping_x) {
                /* Draw player wrapping both horizontally and vertically */
                wrap_rect.x = player_rect.x < WINDOW_WIDTH/2 ?
                player_rect.x + WINDOW_WIDTH :
                player_rect.x - WINDOW_WIDTH;
                SDL_RenderFillRect(renderer, &wrap_rect);
            }
        }
    }
    
    /* 4. If paused, draw a semi-transparent overlay */
    if (app->is_paused) {
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
    
    /* Update FPS counter only once per second */
    app->fps_count++;
    
    uint64_t current_time = SDL_GetTicks();
    if (current_time - app->last_fps_time >= 1000) {
        app->current_fps = app->fps_count;
        app->fps_count = 0;
        app->last_fps_time = current_time;
        
        /* Update window title with current FPS and mode */
        char title[128];
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - FPS: %d %s Mode: %s",
                 app->current_fps,
                 app->is_paused ? "[PAUSED]" : "",
                 app->power_mode == 0 ? "Performance" :
                 (app->power_mode == 1 ? "Balanced" : "Efficient"));
        SDL_SetWindowTitle(app->window, title);
    }
}
