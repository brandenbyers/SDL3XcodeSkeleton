/*
 * main.h - Main header file for the Bit-Twiddled Game Engine
 *
 * This header contains all declarations for the game engine components.
 * Optimized for minimal CPU usage and energy efficiency with a frame-based approach.
 */

#ifndef MAIN_H
#define MAIN_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
/* macOS specific headers for power management */
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif
#endif

/* Include all component headers */
#include "game.h"      /* Core game state definitions */
#include "collision.h" /* Collision detection and movement system */
#include "entity.h"    /* Entity behavior and management */
#include "level.h"     /* Level loading and management */
#include "render.h"    /* Rendering system */
#include "viewport.h"  /* Viewport transformations */
#include "input.h"     /* Input handling */

/* SDL App Callback Declarations */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]);
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event);
SDL_AppResult SDL_AppIterate(void* appstate);
void SDL_AppQuit(void* appstate, SDL_AppResult result);

/* Platform-specific functions (platform.c) */
bool is_running_on_battery(void);
void update_power_state(AppState* app);
float get_time_scale(const AppState* app);
void set_time_scale(AppState* app, uint8_t scale_index);
void toggle_fullscreen(AppState* app);
void cycle_time_scale(AppState* app);
void reset_activity_timer(AppState* app);

#endif /* MAIN_H */
