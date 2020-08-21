# Chip-84 Pi Edition

Chip-84 is a CHIP-8 and SCHIP-8 interpreter originally written for the TI-84 Plus CE calculator and has been ported to many different systems.  This is the Raspberry Pi port tested on a Raspberry Pi Zero W.

Currently unusable unless you change the keyboard input device in main.c.

This version of Chip-84 requires no external libraries and instead uses lower-level techniques such as writing to the framebuffer and grabbing input directly from input event pipes.  This does have the advantage of running extremely fast compared to the SDL version on the Pi Zero.

## Building

### Raspberry Pi OS
Clone the repository and run `make`:

```bash
make
```

## Usage
### Keypad
```
1 2 3 4
Q W E R
A S D F
Z X C V
```

M - Exit the program.\
L - Choose a ROM from the rom folder / Down in the file browser.\
P - Set the CPF / Up in the file browser.\
O - Select a ROM from the file browser

### Command-Line Arguments
`-h, --help` - Display available command line arguments.\
`-o, --open [path]` - Directly load a ROM upon launch.\
`-c, --cpf [number]` - Start Chip-84 with an initial cycles per frame value.

## License
[MIT](https://choosealicense.com/licenses/mit/)
