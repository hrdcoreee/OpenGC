# OpenGameCoordinator

> [!CAUTION]
> This project is under active development and is approaching stability, but issues may still exist. Back up your game files before installation. 


## What is this?
In Valve games, the Game Coordinator (GC) is a backend service most notably responsible for matchmaking and inventory management (like loadouts and skins). This project redirects the GC traffic to a custom, in-process implementation.

**Need help, want to share ideas, or self-promote?**
Use [Discussions](https://github.com/hrdcoreee/OpenGC/discussions). Do not open issues for support.

## License
This project is licensed under the 2-Clause BSD License. See [LICENSE](LICENSE) for details.

## Credits
* **Mikko Kokko** - Original author
* **Theeto** - Code reused from the predecessor project, unusual loot lists
* **GT610** (`GT-610`) - Fork development
* Contributors who continued inventory, networking, diagnostics and RCON work after the original upstream changes

## Third party dependencies
- [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) ([Apache License 2.0](https://github.com/Mbed-TLS/mbedtls/blob/development/LICENSE))
- [funchook](https://github.com/kubo/funchook) ([GPL v2 with Classpath Exception](https://github.com/kubo/funchook/blob/master/LICENSE))
- [diStorm3](https://github.com/gdabah/distorm) ([3-Clause BSD License](https://github.com/gdabah/distorm/blob/master/COPYING))
- [protobuf](https://github.com/protocolbuffers/protobuf) ([3-Clause BSD License](https://github.com/protocolbuffers/protobuf/blob/main/LICENSE))
