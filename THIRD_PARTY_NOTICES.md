# Third-Party Notices

This repository includes third-party code that is not covered by the root `LICENSE`.

## Dear ImGui

Path: `external/imgui/`

Dear ImGui is licensed under the MIT License. See:

- `external/imgui/LICENSE.txt`

## Discord RPC

Path: `external/discord-rpc/`

Discord RPC source files are vendored for the optional Discord Rich Presence integration. Keep upstream copyright and license notices intact when updating this dependency.

## RapidJSON

Path: `external/discord-rpc/include/rapidjson/`

RapidJSON is included through the Discord RPC dependency. RapidJSON is distributed under its own permissive license terms in upstream releases.

## stb_image

Path: `external/stb/stb_image.h`

stb_image is a single-header public-domain image loading library by Sean Barrett. Used for PNG decoding in the client module. The file is in the public domain (or MIT licensed, at your option). See the license block at the top of the header.

## mini-gmp

Path: `mini-gmp/`

The `mini-gmp` files are part of the GNU MP Library and keep their original license terms. According to the headers included in this repository, `mini-gmp` is available under:

- GNU Lesser General Public License v3.0 or later
- GNU General Public License v2.0 or later

See the original notices in:

- `mini-gmp/mini-gmp.h`
- `mini-gmp/mini-gmp.c`
- `mini-gmp/mini-mpq.h`
- `mini-gmp/mini-mpq.c`

The root `LICENSE` applies to this project's own code, but not to third-party code that ships with its own license.
