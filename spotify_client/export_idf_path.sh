
# add in C:\msys32\etc\profile.d

export IDF_PATH="/home/franh/esp/ESP8266_RTOS_SDK/"
export PATH=/home/franh/esp/xtensa-lx106-elf/bin/:$PATH


# Exectues export.sh in IDF-Path
cd $IDF_PATH
. export.sh
### Note that you must run install.sh first

export PATH=/home/franh/esp/xtensa-lx106-elf/bin/:$PATH
