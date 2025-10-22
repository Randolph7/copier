Run Binder 

## compile kernel and install kernel

Make sure you have the following settings in your .config.
```
CONFIG_STAGING=y
CONFIG_ASHMEM=m
CONFIG_ANDROID=y
CONFIG_ANDROID_BINDER_IPC=m
# CONFIG_ANDROID_BINDERFS is not set
CONFIG_ANDROID_BINDER_DEVICES="binder"
CONFIG_ANDROID_BINDER_IPC_SELFTEST=y
```

Compile and install kernel as usual

## load binder and ashmem module
```sh
$ modprobe ashmem_linux
$ modprobe binder_linux
```

The device `binder` and `ashmem` will be created at  `/dev`
