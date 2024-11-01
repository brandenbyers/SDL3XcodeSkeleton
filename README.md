# SDL3 Xcode Skeleton

Multiplatform SDL3 game skeleton in Xcode with Swift Testing for C through bridging headers.

TODO:

- [ ] fix window sizing across platforms
- [ ] fix sigterm when quitting TV app:
        ```
        #else /* platforms that use a standard main() and just call SDL_RunApp(), like iOS and 3DS */
        int main(int argc, char *argv[])
        {
            return SDL_RunApp(argc, argv, SDL_main, NULL);
        }
        ```
