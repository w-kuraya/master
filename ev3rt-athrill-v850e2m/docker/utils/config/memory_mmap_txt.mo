ROM, 0x00000000, 2048
RAM, 0x00200000, 2048
RAM, 0x03FF7000, 1024
RAM, 0x05FF7000, 10240
RAM, 0x07FF7000, 10240
DEV,  0x090F0000, {{ATHRILL_DEVICE_PATH}}/device/ev3com/build/libev3com.so
MMAP, 0x40000000, athrill_mmap.bin
MMAP, 0x40010000, unity_mmap.bin
