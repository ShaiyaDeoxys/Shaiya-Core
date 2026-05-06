# Shaiya Core

Shaiya Core is a C++ library and patching framework designed to modernize, improve, and extend Shaiya private servers through an elegant, configurable, and easy-to-maintain universal base.

The repository contains a bundle of modules targeting the official server services (`ps_game.exe`, `ps_dbAgent.exe`, and `ps_login.exe`) and the client binary (`Game.exe`). It continues the former Shaiya Essentials project as its official successor, with a stronger focus on clean development practices, maintainability, documentation, and configurable behavior.

Shaiya Core is intended to be used with the official server, database, and client bundles maintained by the community. Updated bundles, guides, and support are shared through the Discord group:

https://discord.gg/ZUSuWQsRMB

The Core Discord community grew from the original ShaiyaGG Discord and focuses on open development, knowledge sharing, and building a healthy, stable community over time.

## The Basics

- Shaiya Core provides a preconfigured and stable database, server-side files, SQL installers, tools, and client bundle.
- A detailed video guide for basic installation is available in the Discord group's `Guides` section.
- Core is highly declarative and configurable. Many features are controlled through `.ini` files instead of hardcoded binary edits.
- For example, you can skip the updater or configure the game IP through `CONFIG.INI`, and you can change the server level cap by editing a value in `ServerConfig.ini`.
- You keep control over the project. There are no direct manual assembly edits applied to released files; behavior is defined through source code, hooks, and configuration.
- Features can be disabled, adjusted, or extended in code, and many runtime options can be changed through configuration.
- The project does not assume advanced technical knowledge from the final user. It can be used by someone starting from zero, especially with the community guides and support.
- This repository includes extensive documentation, with more to be added over time. Reading the module READMEs is strongly recommended before making changes.
- As of `v0.5.1`, Shaiya Core is still in beta and should not be used for a public production server yet. The project changes frequently: bugs are fixed, new systems are added, and regressions can still appear. Until a stable release is available, production servers should consider staying with Shaiya Essentials.

## Repository Layout

- `sdev/` - game-service/server hooks, packet handlers, configuration loaders, and gameplay fixes.
- `sdev-client/` - `Game.exe` hooks, ImGui support, Unicode/chat fixes, UI patches, and client quality-of-life features.
- `sdev-login/` - login-service hooks.
- `sdev-db/` - database-agent hooks.
- `shaiya/` - shared game structures, enums, and packet definitions.
- `util/` - common memory patching helpers.
- `external/` - vendored third-party code used by client features.
- `mini-gmp/` - vendored dependency used by the project.

## Build

This project targets Windows, Visual Studio 2022/MSBuild, and x86 builds.

Server module:

```powershell
MSBuild.exe .\Shaiya-Core.sln /t:sdev /p:Configuration=Release /p:Platform=x86 /m
```

Client module:

```powershell
.\build-client.cmd
```

`build-client.cmd` wraps the standard Release|x86 client build and restores the DirectX SDK package dependency used by the client project. Build outputs are ignored by git. Public releases should be built from a clean checkout and tested against the matching binaries.

## Configuration

Runtime behavior is controlled through external files such as `CONFIG.INI`, server-side `.ini` files, PNG interface assets, and feature-specific configuration files. Keep production secrets and server-specific private data out of commits.

- The updater is skipped by default for testing. Change `SKIPUPDATER` in `CONFIG.INI` to restore updater-required behavior.
- The login IP can be configured through `CONFIG.INI`. If the option is absent, the client falls back to executable defaults.
- Server selection and mode selection can be skipped through `CONFIG.INI` if desired.
- Multi-UI folders are supported. `UI=0` uses the standard interface folder, while `UI=1` uses the EP6.4 interface folder.
- The current default setup uses the custom EP4.5-style UI.
- Interface assets and screenshots use `.png` instead of the original Shaiya `.tga` screenshot/interface workflow where supported.
- GM-only ID view for mobs/NPCs can be controlled with `IDVIEW`.
- Cosmetic and visual features can be toggled through commands such as `/wings off`, `/pets off`, `/costumes off`, `/titles off`, `/colour off`, and `/effects off`.
- Performance Mode is tied to `F7`. It quickly toggles wings, pets, effects, and FPS boost for PvP or crowded scenes.
- FPS boost can also be controlled manually with `/fpsboost on` and `/fpsboost off`.
- Level cap and enchant cap are configured server-side through `ServerConfig.ini`.
- `/font` changes the live game font and persists the selected font properties.
- `F8` opens the ImGui panel, currently used by the server-backed Roulette feature.

## Feature Documentation

- Client features are documented in `sdev-client/README.md`.
- Server features are documented in `sdev/README.md`.
- Shared packet/structure notes are documented in `shaiya/README.md`.

## Special Thanks

This project exists thanks to Bowie's work and contributions to the Shaiya community through the Episode 6 and Shaiya Essentials repositories. Without that foundation, Shaiya Core would not be what it is today.

The Shaiya development community changed because of that effort and standard of quality. Core aims to carry that spirit forward with the same level of care, respect, and long-term usefulness.

Thank you, my friend, for everything.

## License

This project's own code is licensed under the BSD 3-Clause License. See `LICENSE`.

Third-party code keeps its original license terms. See `THIRD_PARTY_NOTICES.md`.

## Disclaimer

This project is provided as-is, without warranties. Test every patch in a controlled environment before using it in production.
