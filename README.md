# learning c with esp8266 (DEPRECATED)

## Notes

### Comands

for compile only:
`make -j4`
compiling and flashing:
`make -j4 flash`
and also monitor the output of device:
`make -j4 flash monitor`
monitor only:
`make -j4 monitor`

### Auto set the environment on terminal

Create the file export_idf_path.sh with the content:

```
export IDF_PATH="/home/franh/esp/ESP8266_RTOS_SDK/"
export PATH=/home/franh/esp/xtensa-lx106-elf/bin/:$PATH

# Exectues export.sh in IDF-Path
cd $IDF_PATH
. export.sh
### Note that you must run install.sh first

export PATH=/home/franh/esp/xtensa-lx106-elf/bin/:$PATH
```

add this file into in C:\msys32\etc\profile.d:

(The path can vary)
