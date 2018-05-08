# CONTENTS

1. How To Use
2. Customer Support

## 1. How to Use
### 1.1. Installation
1. Clone this repo to local machine.
```
# git clone https://github.com/chelsiocudbg/SW_Patches.git /home/SW_Patches
```

2. Patch the kernel source. Apply all the patches in the order of patch
numbering.
```
# cd <path to kernel source to 3.10.0-693.17.1 kernel>
# for i in /home/SW_Patches/patches/for_eval/upstream/*.patch; do patch -p1 < $i; done
# for i in /home/SW_Patches/patches/for_eval/cudbg/*.patch; do patch -p1 < $i; done
```

3. Ensure that all the Chelsio driver modules are selected in the kernel
configuration file.

4. Build and install the kernel.

5. Install the Firmware and the configuration files.
```
# cp /home/SW_Patches/firmware/* /lib/firmware/cxgb4/
```

6. Install CUDBG and chdebug.
```
# cp /home/SW_Patches/tools/* /sbin/
```

7. Reboot your machine into the newly installed kernel.

### 1.2. Firmware Update
The firmware (v1.16.33.0) is installed on the system, typically in
`/lib/firmware/cxgb4`, and the driver will auto-load the firmware if an update
is required. The kernel must be configured to enable userspace firmware loading
support:
```
Device Drivers -> Generic Driver Options -> Userspace firmware loading support
```
The firmware version can be verified using ethtool:
```
# ethtool -i <iface>
```

### 1.3. CUDBG
Chelsio Unified Debug (CUDBG) Library is a platform independent library developed
to provide hassle free debug log collection. To collect all the logs,
```
# cudbg_app --collect all ethX <outfilename>
```
where ethX is the Chelsio Interface.

> NOTE: To know more options of cudbg, refer cudbg help by typing `# cudbg_app -h`

# 2. Customer Support
Please contact Chelsio support at support@chelsio.com for any issues regarding
the product.
