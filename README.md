# WebDOOM: Responsive DOOM for the web

Ported by [Neil Rackett](https://x.com/neilrackett)

## Introduction

WebDOOM frees DOOM from it's 320x200 constraints in a responsive, full-browser port of the original version of DOOM based on [doomgeneric](https://github.com/ozkl/doomgeneric).

Want to play DOOM in full-HD? Nothing to install. Just open the URL in a web browser on any device and it just works.

[Click here to try it now!](https://labs.neilrackett.com/doom)

Supports keyboard and joystick/gamepad controls (optimised for Xbox controllers). Double-click for full screen.

## Build

You can build WebDOOM using [Emscripten](https://emscripten.org/):

- Install the shareware version of DOOM (or [download if from Archive.org](https://archive.org/download/doom-wads))
- Copy `DOOM1.WAD` into a `tmp` folder in the root of this project
- Run `make`

All of the files you need to deploy WebROTT will be in the `build` folder, and you can play them locally by running `make serve` and opening http://localhost:8000 in your browser.

The build process uses Emscripten's internal version of SDL, so there's no need to install any dependencies.

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
