`mpcv` is a TUI for [mpc](https://www.musicpd.org/clients/mpc/), because searching & playing a song is a tedious task for a CLI, and you probably don't want to install other clients like [ncmpcpp](https://github.com/ncmpcpp/ncmpcpp) because you already have `mpc` installed and that would be redundant.

You obviously require to have `mpc` installed

# How to install
use `make` to generate the binary, and then do whatever you want with it, put it under `/bin` for example.

# Controls
## "Normal" Mode

`h/j/k/l`: Left/Down/Up/Right

`Arrows`: As expected

`/`: Search

`p`: Play song on cursor

## "Search" Mode

`ESC`: Quit
`Arrows`: Search Foward / Backwards
`Enter`: Confirm Search
