Waxcape
=======
This is a simple xcape replacement for Wayland. This initial version is hardcoded to implement what I need: Capslock is escape when tapped alone, ctrl when pressed with another key.

It uses libinput to implement a similar functionality in a lower layer. It exclusively opens every keyboard and clones it to create a uinput device. Every incoming key press is evaluated before being passed to the uinput device. 

## Building
meson build
ninja -C build

## Installation
```bash
# Copy waxcape to /usr/local/bin
# Change waxcape@.service if you modify the path
sudo cp build/waxcape /usr/local/bin

# Add udev and systemd rules in Fedora 29
sudo cp systemd-conf/waxcape@.service /etc/systemd/system/

sudo cp udev-rules/99-waxcape-input.rules /etc/udev/rules.d/
```
