# Vimonized interception plugin

@maricn's ~hideous~ humble, performant key remapping C++ code that should work on Linux for any input device that emits keys.
With addition of typing detection and runtime toggle on signal.

## tl;dr;
 - Changed default config to suit my liking
 - USR1 sent to the process toggles the passthrough mode, effectively enabling/disabling the remapping
 - Time deltas of last couple keys are tracked to find the "gap" and determine if the user is typing or not, drastically reducing the chance of false positives. Assumption is that delta before intentional chord is noticeably higher than unintentional one.

## Requirements
Basically any OS that works with `libevdev` (linux with kernel newer than 2.6.36), no matter what desktop environment, or even if any DE is used (yes, it works the same in X server instead of `xmodmap`, but also in plain terminal without graphical environment).
For building, `meson` (and `ninja`), and `make`.
At runtime, the `https://github.com/jbeder/yaml-cpp` YAML library.

## Installation
```bash
$ git clone "https://github.com/maricn/interception-vimproved"
$ cd interception-vimproved
$ sudo make install
```

## Running
Use it with a job specification for `udevmon` (from [Interception Tools](https://gitlab.com/interception/linux/tools)). I install the binary to `/opt/interception/interception-vimproved` and use it like the following on Arch linux on Thinkpad x1c gen7.

```yaml
- JOB: "intercept -g $DEVNODE | interception-vimproved | uinput -d $DEVNODE"
  DEVICE:
    NAME: ".*((k|K)(eyboard|EYBOARD)|TADA68).*"
```

That matches any udev devices containing keyboard in the name (or my external TADA68 keyboard).

Alternatively, you can run it with `udevmon` binary straight, just make sure to be negatively nice (`nice -n -20 udevmon -c /etc/interception-vimproved/config.yaml`) so your input is always available.

### Configuration
If you want to customize the functionality, you can take a look at the [`config.yaml`](./config.yaml).
The configuration file is copied to `/etc/interception-vimproved/config.yaml` when invoking `sudo make install`.
You can configure that file and add its path as an argument to `interception-vimproved` when run by updating your `/etc/interception/udevmon.yaml`:
```yaml
...
- JOB:
  - |
    intercept -g $DEVNODE \
    | interception-vimproved /etc/interception-vimproved/config.yaml \
    | uinput -d $DEVNODE
...
```


### Testing
In case you want to edit the source code, kill the `udevmon` daemon, and manually try the following to avoid getting stuck with broken input. Trust me, you can get yourself in a dead end situation easily.

```bash
# sleep buys you some time to focus away from terminal to your playground, also you'll probably need to add a sudo
sleep 1 && timeout 10 udevmon -c /etc/interception-vimproved/config.yaml
```

## Why make this
- I absolutely hate curling my finger to reach Super, and interception tools is a nice way to remap it to Space.
- Original interception-vimproved was producing a bunch of false positives because of me remapping Space, so I fixed it.
- I wanted to learn C++ and this was a fun project to do so.

## Acknowledgments

Thanks to [@maricn](https://github.com/maricn) for his original code and help with the project
Kudos to [@dceluis](https://github.com/dceluis) and [@exprpapi](https://github.com/exprpapi) for their contributions.

### Related work / inspiration

- [Interception Tools](https://gitlab.com/interception/linux/tools)
- [space2meta](https://gitlab.com/interception/linux/plugins/space2meta)
- [dual-function-keys](https://gitlab.com/interception/linux/plugins/dual-function-keys)
- [interception-k2k](https://github.com/zsugabubus/interception-k2k)
