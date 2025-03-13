/*
 * render.c - Rendering functions for the Bit-Twiddled Game Engine
 *
 * This file contains all rendering-related code, including:
 * - Texture management
 * - Scene drawing with dirty rectangle tracking
 * - Batch rendering for improved performance
 * - GPU-based visual interpolation for smooth movement
 */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <math.h>

#include "game.h"
#include "render.h"
#include "entity.h"
#include "viewport.h"
#include "collision.h"

/* Power state colors */
const SDL_Color CELL_COLORS[CELL_MAX] = {
    { 0,   0,   0,   255 },  /* CELL_EMPTY: black */
    { 64,  64,  192, 255 },  /* CELL_WALL: blue */
    { 255, 255, 0,   255 },  /* CELL_ITEM: yellow */
    { 128, 128, 255, 255 },  /* CELL_PIVOT: light blue */
};
const SDL_Color PLAYER_COLOR = { 0, 255, 0, 255 };  /* Player: green */
const SDL_Color GRID_LINE_COLOR = { 32, 32, 32, 255 }; /* Grid lines: dark gray */
const SDL_Color PAUSED_OVERLAY_COLOR = { 0, 0, 32, 80 }; /* Transparent dark blue */

/* Simple bitmap font for the "PAUSED" text using blocks (each character is 5×7 pixels) */
static const uint8_t BITMAP_FONT[26][7] = {
    /* A */
    { 0x6, 0x9, 0x9, 0xF, 0x9, 0x9, 0x9 },
    /* B */
    { 0xE, 0x9, 0x9, 0xE, 0x9, 0x9, 0xE },
    /* C */
    { 0x6, 0x9, 0x8, 0x8, 0x8, 0x9, 0x6 },
    /* D */
    { 0xE, 0x9, 0x9, 0x9, 0x9, 0x9, 0xE },
    /* E */
    { 0xF, 0x8, 0x8, 0xE, 0x8, 0x8, 0xF },
    /* F */
    { 0xF, 0x8, 0x8, 0xE, 0x8, 0x8, 0x8 },
    /* G */
    { 0x6, 0x9, 0x8, 0xB, 0x9, 0x9, 0x6 },
    /* H */
    { 0x9, 0x9, 0x9, 0xF, 0x9, 0x9, 0x9 },
    /* I */
    { 0xE, 0x4, 0x4, 0x4, 0x4, 0x4, 0xE },
    /* J */
    { 0x1, 0x1, 0x1, 0x1, 0x9, 0x9, 0x6 },
    /* K */
    { 0x9, 0x9, 0xA, 0xC, 0xA, 0x9, 0x9 },
    /* L */
    { 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0xF },
    /* M */
    { 0x9, 0xF, 0x9, 0x9, 0x9, 0x9, 0x9 },
    /* N */
    { 0x9, 0xD, 0xF, 0xB, 0x9, 0x9, 0x9 },
    /* O */
    { 0x6, 0x9, 0x9, 0x9, 0x9, 0x9, 0x6 },
    /* P */
    { 0xE, 0x9, 0x9, 0xE, 0x8, 0x8, 0x8 },
    /* Q */
    { 0x6, 0x9, 0x9, 0x9, 0xB, 0xA, 0x5 },
    /* R */
    { 0xE, 0x9, 0x9, 0xE, 0xA, 0x9, 0x9 },
    /* S */
    { 0x6, 0x9, 0x8, 0x6, 0x1, 0x9, 0x6 },
    /* T */
    { 0xE, 0x4, 0x4, 0x4, 0x4, 0x4, 0x4 },
    /* U */
    { 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x6 },
    /* V */
    { 0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x6 },
    /* W */
    { 0x9, 0x9, 0x9, 0x9, 0x9, 0xF, 0x9 },
    /* X */
    { 0x9, 0x9, 0x6, 0x6, 0x6, 0x9, 0x9 },
    /* Y */
    { 0x9, 0x9, 0x9, 0x6, 0x2, 0x2, 0x2 },
    /* Z */
    { 0xF, 0x1, 0x2, 0x4, 0x8, 0x8, 0xF }
};

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
    
    /* Batch render walls and pivot points */
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
    
    /* Collect all wall and pivot rectangles for batch rendering */
    for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
        for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
            /* Get cell directly from viewport cache - better cache locality */
            int cache_idx = vy * VIEWPORT_WIDTH + vx;
            CellType cell_type = (CellType)game->viewport->cache[cache_idx];
            
            if (cell_type == CELL_WALL || cell_type == CELL_PIVOT) {
                wall_rects[wall_count].x = vx * PIXEL_SCALE;
                wall_rects[wall_count].y = vy * PIXEL_SCALE;
                wall_rects[wall_count].w = wall_rects[wall_count].h = PIXEL_SCALE;
                
                /* Use appropriate color based on cell type */
                if (cell_type == CELL_WALL) {
                    SDL_SetRenderDrawColor(app->renderer,
                                           CELL_COLORS[CELL_WALL].r,
                                           CELL_COLORS[CELL_WALL].g,
                                           CELL_COLORS[CELL_WALL].b,
                                           CELL_COLORS[CELL_WALL].a);
                } else {
                    SDL_SetRenderDrawColor(app->renderer,
                                           CELL_COLORS[CELL_PIVOT].r,
                                           CELL_COLORS[CELL_PIVOT].g,
                                           CELL_COLORS[CELL_PIVOT].b,
                                           CELL_COLORS[CELL_PIVOT].a);
                }
                SDL_RenderFillRect(app->renderer, &wall_rects[wall_count]);
                wall_count++;
            }
        }
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
    game->grid_state->cells_changed = false;
    game->grid_state->last_frame_updated = game->frame_count;
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
 * Render a character from our bitmap font
 */
void render_bitmap_char(SDL_Renderer* renderer, char c, int x, int y, int scale, SDL_Color color) {
    if (c < 'A' || c > 'Z') {
        return; /* Only support uppercase A-Z */
    }
    
    int idx = c - 'A';
    
    /* Draw the character pixel by pixel */
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            /* Check if this pixel is on */
            if (BITMAP_FONT[idx][row] & (1 << (4 - col))) {
                SDL_FRect pixel = {
                    x + col * scale,
                    y + row * scale,
                    scale,
                    scale
                };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}

/*
 * Render a bitmap string
 */
void render_bitmap_string(SDL_Renderer* renderer, const char* str, int x, int y, int scale, SDL_Color color) {
    int pos_x = x;
    
    for (int i = 0; str[i] != '\0'; i++) {
        /* Only render if it's an uppercase letter */
        if (str[i] >= 'A' && str[i] <= 'Z') {
            render_bitmap_char(renderer, str[i], pos_x, y, scale, color);
        }
        pos_x += 6 * scale; /* Character width (5) + spacing (1) */
    }
}

/*
 * Rendering Functions
 */

/* Configure renderer with platform-specific optimizations */
void configure_rendering(AppState* app) {
    /* Set logical scaling */
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(app->renderer, WINDOW_WIDTH, WINDOW_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    
    /* Enable VSync for smooth rendering */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    
    /* Enable batching for more efficient rendering */
    SDL_SetHint("SDL_RENDER_BATCHING", "1");
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
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - FPS: %d %s",
                 app->current_fps,
                 app->is_paused ? "[PAUSED]" : "");
        SDL_SetWindowTitle(app->window, title);
    }
}

/* Render the game with GPU-based visual interpolation */
void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    uint64_t current_time = SDL_GetTicks(); /* Current time for animations */
    
    /* On first render or after changes, regenerate background texture */
    if (game->grid_state->cells_changed) {
        /* Ensure cache is up-to-date before rendering */
        if (!game->grid_state->cache_valid) {
            update_viewport_cache(game);
        }
        create_background_texture(app);
    }
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    /* 1. Render the background (walls, pivot points, and grid lines) */
    if (app->background_texture) {
        SDL_RenderTexture(renderer, app->background_texture, NULL, NULL);
    }
    
    /* 2. Batch render all items */
    SDL_FRect* rects = (SDL_FRect*)SDL_malloc(GRID_SIZE * sizeof(SDL_FRect));
    if (rects) {
        int rect_count = 0;
        
        /* Collect all item rectangles for batch rendering */
        for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
            for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
                /* Get cell from viewport cache */
                int cache_idx = vy * VIEWPORT_WIDTH + vx;
                CellType cell_type = (CellType)game->viewport->cache[cache_idx];
                
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
    
    /* 3. Render player with GPU-based visual interpolation */
    float visual_x, visual_y;
    get_visual_position(game->player, &visual_x, &visual_y);
    
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
    if (game->player->is_moving) {
        int target_viewport_x, target_viewport_y;
        grid_to_viewport(game, game->player->target_x, game->player->target_y,
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
    
    /* 4. Render all entities with GPU-based visual interpolation */
    for (int i = 0; i < game->entities->count; i++) {
        if (!game->entities->is_active[i]) {
            continue;
        }
        
        /* Get entity position with GPU-based interpolation */
        float entity_x, entity_y;
        get_entity_visual_position(game, i, current_time, &entity_x, &entity_y);
        
        /* Convert to viewport coordinates */
        int entity_viewport_x, entity_viewport_y;
        grid_to_viewport(game, (int)entity_x, (int)entity_y, &entity_viewport_x, &entity_viewport_y);
        
        /* Calculate fractional part */
        float entity_frac_x = entity_x - (int)entity_x;
        float entity_frac_y = entity_y - (int)entity_y;
        
        /* Create entity rectangle */
        SDL_FRect entity_rect = {
            (entity_viewport_x + entity_frac_x) * PIXEL_SCALE,
            (entity_viewport_y + entity_frac_y) * PIXEL_SCALE,
            PIXEL_SCALE,
            PIXEL_SCALE
        };
        
        /* Check for wrapping */
        bool entity_wrapping_x = false;
        bool entity_wrapping_y = false;
        
        if (game->entities->is_moving[i]) {
            int target_vx, target_vy;
            grid_to_viewport(game, game->entities->target_x[i], game->entities->target_y[i],
                             &target_vx, &target_vy);
            
            /* Check for horizontal wrapping */
            if (abs(target_vx - entity_viewport_x) > VIEWPORT_WIDTH/2) {
                entity_wrapping_x = true;
            }
            
            /* Check for vertical wrapping */
            if (abs(target_vy - entity_viewport_y) > VIEWPORT_HEIGHT/2) {
                entity_wrapping_y = true;
            }
        }
        
        /* Get entity color */
        uint8_t entity_type = game->entities->entity_type[i];
        if (entity_type >= ENTITY_MAX) {
            entity_type = 0;
        }
        
        /* Draw the entity */
        SDL_SetRenderDrawColor(renderer,
                               ENTITY_COLORS[entity_type].r,
                               ENTITY_COLORS[entity_type].g,
                               ENTITY_COLORS[entity_type].b,
                               ENTITY_COLORS[entity_type].a);
        SDL_RenderFillRect(renderer, &entity_rect);
        
        /* Handle wrapping */
        if (entity_wrapping_x || entity_wrapping_y) {
            SDL_FRect wrap_rect = entity_rect;
            
            if (entity_wrapping_x) {
                /* Draw entity wrapping horizontally */
                wrap_rect.x = entity_rect.x < WINDOW_WIDTH/2 ?
                entity_rect.x + WINDOW_WIDTH :
                entity_rect.x - WINDOW_WIDTH;
                SDL_RenderFillRect(renderer, &wrap_rect);
            }
            
            if (entity_wrapping_y) {
                /* Draw entity wrapping vertically */
                wrap_rect.x = entity_rect.x; /* Reset x from previous wrapping */
                wrap_rect.y = entity_rect.y < WINDOW_HEIGHT/2 ?
                entity_rect.y + WINDOW_HEIGHT :
                entity_rect.y - WINDOW_HEIGHT;
                SDL_RenderFillRect(renderer, &wrap_rect);
                
                if (entity_wrapping_x) {
                    /* Draw entity wrapping both horizontally and vertically */
                    wrap_rect.x = entity_rect.x < WINDOW_WIDTH/2 ?
                    entity_rect.x + WINDOW_WIDTH :
                    entity_rect.x - WINDOW_WIDTH;
                    SDL_RenderFillRect(renderer, &wrap_rect);
                }
            }
        }
    }
    
    /* 5. If paused and not in glitch transition, draw a semi-transparent overlay with PAUSED text */
    if (game->glitch->paused && !game->glitch->active) {
        /* Draw a translucent dark blue overlay */
        SDL_SetRenderDrawColor(renderer,
                               PAUSED_OVERLAY_COLOR.r,
                               PAUSED_OVERLAY_COLOR.g,
                               PAUSED_OVERLAY_COLOR.b,
                               PAUSED_OVERLAY_COLOR.a);
        SDL_FRect overlay = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        SDL_RenderFillRect(renderer, &overlay);
        
        /* Calculate position for centered "PAUSED" text */
        int text_width = 6 * 6 * 3; /* 6 letters × 6 pixel width × 3 scale */
        int text_height = 7 * 3;    /* 7 pixel height × 3 scale */
        int text_x = (WINDOW_WIDTH - text_width) / 2;
        int text_y = (WINDOW_HEIGHT - text_height) / 2;
        
        /* Draw "PAUSED" text */
        SDL_Color text_color = { 255, 255, 255, 255 }; /* White */
        render_bitmap_string(renderer, "PAUSED", text_x, text_y, 3, text_color);
    }
    
    /* 6. Render glitch effects if active */
    render_glitch_effects(renderer, game);
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
    
    /* Update FPS counter */
    update_fps(app);
}

/*
 * Render glitch effects over the current frame
 */
void render_glitch_effects(SDL_Renderer* renderer, const GameState* game) {
    const GlitchState* glitch = game->glitch;
    
    /* If not active, nothing to render */
    if (!glitch->active) {
        return;
    }
    
    /* 1. Render scanlines with horizontal displacement */
    for (int i = 0; i < glitch->scanline_count; i++) {
        int y = glitch->scanline_y[i];
        int height = glitch->scanline_height[i];
        int shift = glitch->scanline_shifts[i];
        
        /* Create two rects: one for the black gap and one for the shifted content */
        SDL_FRect gap_rect = {
            0, y, WINDOW_WIDTH, height
        };
        
        /* Set very dark color for the gap */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, &gap_rect);
        
        /* If we have a shift, copy part of the screen to create tearing effect */
        if (shift != 0) {
            /* This would normally use SDL_RenderTexture with source and dest rects
             to copy part of the screen, but that's complex to implement here.
             Instead, we'll draw colored rectangles to simulate the effect. */
            
            /* Create a rectangle for the shifted part */
            SDL_FRect shift_rect = {
                shift > 0 ? 0 : -shift, y,
                WINDOW_WIDTH - abs(shift), height
            };
            
            /* Use a distorted color for the shifted part */
            uint8_t r = (y % 255);
            uint8_t g = ((y * 3) % 255);
            uint8_t b = ((y * 7) % 255);
            SDL_SetRenderDrawColor(renderer, r, g, b, 180);
            SDL_RenderFillRect(renderer, &shift_rect);
            
            /* Add noise/static in the shifted part */
            for (int j = 0; j < 20; j++) {
                int noise_x = rand() % (int)shift_rect.w;
                SDL_FRect noise_rect = {
                    shift_rect.x + noise_x, y, 2, height
                };
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
                SDL_RenderFillRect(renderer, &noise_rect);
            }
        }
    }
    
    /* 2. Apply global color shifting if active */
    if (glitch->color_shift_r || glitch->color_shift_g || glitch->color_shift_b) {
        /* Create a full-screen semi-transparent overlay with shifted colors */
        SDL_FRect overlay = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        
        /* Set a semi-transparent color with the shifts */
        SDL_SetRenderDrawColor(renderer,
                               glitch->color_shift_r,
                               glitch->color_shift_g,
                               glitch->color_shift_b,
                               50);  /* Low alpha for subtle effect */
        SDL_RenderFillRect(renderer, &overlay);
    }
    
    /* 3. Random data corruption: add small colored rectangles randomly */
    int corruption_count = 20 + (rand() % 40);
    for (int i = 0; i < corruption_count; i++) {
        int x = rand() % WINDOW_WIDTH;
        int y = rand() % WINDOW_HEIGHT;
        int w = 1 + (rand() % 4);
        int h = 1 + (rand() % 4);
        
        SDL_FRect corrupt_rect = { x, y, w, h };
        
        /* Random color for corruption */
        SDL_SetRenderDrawColor(renderer,
                               rand() % 255,
                               rand() % 255,
                               rand() % 255,
                               128 + (rand() % 128));
        SDL_RenderFillRect(renderer, &corrupt_rect);
    }
    
    /* 4. Render expansion highlight */
    if (glitch->expansion_radius > 0 && glitch->expansion_radius < GLITCH_EXPAND_MAX) {
        /* Draw a circle expanding from the highlight position */
        float radius = glitch->expansion_radius;
        int center_x = glitch->highlight_pos_x;
        int center_y = glitch->highlight_pos_y;
        
        /* Calculate alpha based on radius (fade out as it expands) */
        uint8_t alpha = 255 - (uint8_t)((radius / GLITCH_EXPAND_MAX) * 255);
        
        /* Use a bright white color for the highlight */
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
        
        /* Draw a "circle" using line segments */
        for (int i = 0; i < 16; i++) {
            float angle1 = (i / 16.0f) * 2 * M_PI;
            float angle2 = ((i + 1) / 16.0f) * 2 * M_PI;
            
            float x1 = center_x + radius * cosf(angle1);
            float y1 = center_y + radius * sinf(angle1);
            float x2 = center_x + radius * cosf(angle2);
            float y2 = center_y + radius * sinf(angle2);
            
            SDL_RenderLine(renderer, x1, y1, x2, y2);
        }
        
        /* Draw another thinner circle with decreasing radius */
        float inner_radius = radius - 4;
        if (inner_radius > 0) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha/2);
            
            for (int i = 0; i < 16; i++) {
                float angle1 = (i / 16.0f) * 2 * M_PI;
                float angle2 = ((i + 1) / 16.0f) * 2 * M_PI;
                
                float x1 = center_x + inner_radius * cosf(angle1);
                float y1 = center_y + inner_radius * sinf(angle1);
                float x2 = center_x + inner_radius * cosf(angle2);
                float y2 = center_y + inner_radius * sinf(angle2);
                
                SDL_RenderLine(renderer, x1, y1, x2, y2);
            }
        }
    }
}
