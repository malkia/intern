# github.com/malkia/intern
Playground for C++ string interning

# Setup
* On Debian/ubuntu you may need to install bazel & gcc-snapshot:
  * https://docs.bazel.build/versions/master/install-ubuntu.html

    ```
    sudo apt install gcc-snapshot bazel
    ```
  * __TODO__: Windows, OSX, etc. 

# Run

    ```
    time -p bazel run --config=gcc-snapshot -c opt intern
    ```


# Results
```
malkia@penguin:~/p/intern$ time -p bazel run --config=gcc-snapshot -c opt intern
INFO: Analyzed target //:intern (0 packages loaded, 0 targets configured).
INFO: Found 1 target...
Target //:intern up-to-date:
  bazel-bin/intern
INFO: Elapsed time: 0.079s, Critical Path: 0.00s
INFO: 0 processes.
INFO: Build completed successfully, 1 total action
INFO: Build completed successfully, 1 total action
Pool 0x7a5d66d79010 (size=268435456, used=0), ref=(0, 0 bytes), used=(0, 0 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
Pool 0x7a5d66d79010 (size=268435456, used=5), ref=(9, 45 bytes), used=(1, 5 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
Pool 0x7a5d66d79010 (size=268435456, used=5), ref=(9, 45 bytes), used=(1, 5 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=541
Pool 0x7a5d66d79010 (size=268435456, used=290483), ref=(16774123, 1570762771 bytes), used=(3103, 290483 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=379
Pool 0x7a5d66d79010 (size=268435456, used=705511), ref=(33546912, 3143199147 bytes), used=(7530, 705511 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=229
Pool 0x7a5d66d79010 (size=268435456, used=1393139), ref=(50316801, 4717027923 bytes), used=(14857, 1393139 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=97
Pool 0x7a5d66d79010 (size=268435456, used=3025244), ref=(67076720, 6298454638 bytes), used=(32154, 3025244 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=59
Pool 0x7a5d66d79010 (size=268435456, used=5715554), ref=(83825500, 7883044948 bytes), used=(60590, 5715554 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=7
Pool 0x7a5d66d79010 (size=268435456, used=28613244), ref=(100363041, 9462982294 bytes), used=(300265, 28613244 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=5
Pool 0x7a5d66d79010 (size=268435456, used=60714454), ref=(116804712, 11035938320 bytes), used=(635810, 60714454 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=3
Pool 0x7a5d66d79010 (size=268435456, used=114290480), ref=(133022687, 12589641730 bytes), used=(1195051, 114290480 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=2
Pool 0x7a5d66d79010 (size=268435456, used=194710026), ref=(148961042, 14117612720 bytes), used=(2033912, 194710026 bytes), leaks=(0, 0 bytes), fails=(0, 0 bytes)
prime=1
Pool 0x7a5d66d79010 (size=268435456, used=1022199362), ref=(157153618, 14903999787 bytes), used=(2800715, 268435433 bytes), leaks=(0, 0 bytes), fails=(7817837, 753763929 bytes)
real 54.87
user 80.62
sys 2.67
malkia@penguin:~/p/intern$
```

# System

* Chromebook with crostini

```
malkia@penguin:~/p/intern$ lsb_release -a
No LSB modules are available.
Distributor ID: Debian
Description:    Debian GNU/Linux bullseye/sid
Release:        unstable
Codename:       sid
```
