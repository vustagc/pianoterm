# pianoterm

Run shell commands from your piano

## Description

Linux CLI tool to assign shell commands to keys or knobs on a USB MIDI Keyboard/Controller

## Usage

```bash
pianoterm <port>
```

Note: 
- It will keep trying to connect until it finds the port, use ctrl+c to stop it
- Assumes ALSA is used as the soundcard driver, you can use acconnect -i to find the desired midi port.

## Configuration

- path: ~/.config/pianoterm/config

### Example Config

```conf
## Control audio playback
on_press
21 = playerctl previous # first key on an 88-key keyboard
22 = playerctl play-pause
23 = playerctl next

## Map directly to keyboard keys (Wayland)
on_press
107 = ydotool key 108:1
108 = ydotool key 103:1
on_release
107 = ydotool key 108:0
108 = ydotool key 103:0

## Run custom scripts
69 = /home/me/my_script.sh

## Assign multiple commands to run sequentially on the same key press
60 = notify-send "command 1"
60 = notify-send "command 2"

## Map controller events (pedal presses, knob switches, ...)
64 (127) = notify-send "Pressed pedal" # 127 in this case is the max pedal output
64 (0)   = notify-send "Released pedal"
```

### Syntax Explanation

- Keys and buttons:
```conf
# Default - on_press
[on_press|on_release|on_hold]

key = command
```

- Controllers (pedals, knobs, etc.):
```conf
# Must include a trigger value
# A trigger is numeric value greater than 0. (usually the pressure on a pedal, the volume on a knob, etc.)

key (trigger) = command
```

Notes:
- A different key and controller can share the same key.
- You can use aseqdump -p <port> to find keycodes and controller values.

## Building

```bash
git clone https://github.com/vustagc/pianoterm.git
cd pianoterm && make
```

### Dependencies
- C compiler
- alsactl (1.2.15.2)
- make (optional)

## Playing Supertux on the Piano

https://github.com/user-attachments/assets/718a9328-6b70-4cfa-97d6-e355dd0e5b6a
