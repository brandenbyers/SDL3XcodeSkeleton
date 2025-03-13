# SDL3 Guide: Common Issues and Solutions

This guide addresses common issues encountered when working with SDL3, particularly regarding initialization patterns and event handling that differ from SDL2.

## SDL3 Initialization Pattern

When initializing SDL3, the return value semantics are different from SDL2 and many traditional C functions:

```c
/* CORRECT: In SDL3, initialization returns true (non-zero) on success */
if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

/* INCORRECT: This is the SDL2 pattern and will fail in SDL3 */
if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) < 0) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}
```

### The Correct Pattern:
- SDL3's `SDL_Init()` returns a boolean-like value
- Returns TRUE (non-zero) on SUCCESS
- Returns FALSE (zero) on FAILURE
- This is the opposite of traditional Unix-style error handling where 0 means success!

To make your intent clear, you can use:

```c
int init_result = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
if (!init_result) {  // Explicitly check for failure (0/false)
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}
```

## SDL3 Event Handling Structure

In SDL3, gamepad events use specific event union members:

```c
/* CORRECT: Access gamepad events through their specific union members */
case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
process_gamepad_button_event(&game->input, event->gbutton.button, true);
break;

case SDL_EVENT_GAMEPAD_AXIS_MOTION:
process_gamepad_axis_event(&game->input, event->gaxis.axis, event->gaxis.value);
break;

/* INCORRECT: There is no 'gamepad' member in the SDL_Event union */
case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
process_gamepad_button_event(&game->input, event->gamepad.button, true); // WRONG
break;
```

### Event Union Member Reference:

| Event Type | Correct Union Member | Relevant Fields |
|------------|----------------------|-----------------|
| `SDL_EVENT_GAMEPAD_BUTTON_DOWN` | `event->gbutton` | `.button`, `.which` |
| `SDL_EVENT_GAMEPAD_BUTTON_UP` | `event->gbutton` | `.button`, `.which` |
| `SDL_EVENT_GAMEPAD_AXIS_MOTION` | `event->gaxis` | `.axis`, `.value`, `.which` |
| `SDL_EVENT_GAMEPAD_ADDED` | `event->gdevice` | `.which` |
| `SDL_EVENT_GAMEPAD_REMOVED` | `event->gdevice` | `.which` |

## Subsystem Initialization

In SDL3, subsystems should be initialized during the main `SDL_Init()` call:

```c
/* Include all needed subsystems in the initial SDL_Init call */
if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}
```

Only use `SDL_InitSubSystem()` for subsystems you need to add later that weren't in the initial call. Most applications should initialize all needed subsystems at startup.
                                    
                                    ## Missing Functions From SDL2
                                    
                                    Some SDL2 functions have been renamed or removed in SDL3. For example:
                                        
                                        | SDL2 Function | SDL3 Replacement | Notes |
                                    |---------------|------------------|-------|
                                    | `SDL_GamepadHasRumble` | Not available | Check SDL3 docs for haptic alternatives |
                                    | `SDL_JoystickInstanceID` | `SDL_GetJoystickInstanceID` | Function renamed |
                                    | `SDL_RenderCopy` | `SDL_RenderTexture` | Function renamed |
                                    
                                    ## General Best Practices for SDL3
                                    
                                    1. **Initialization**: Always use `SDL_Init()` with all subsystems you need at startup
                                    2. **Event Access**: Access event data through the specific union member for that event type
                                    3. **Error Checks**: Remember that SDL3 init functions return true (non-zero) on success
                                    4. **Documentation**: When in doubt, refer to the SDL3 documentation for the correct API
                                    5. **Gamepad Handling**: Use the `SDL_GAMEPAD_*` constants instead of hardcoded values
                                    
                                    By following these patterns, you'll avoid common pitfalls when working with SDL3.
