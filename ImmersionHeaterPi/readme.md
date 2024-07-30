https://www.waveshare.com/2-ch-triac-hat.htm
https://www.waveshare.com/wiki/2-CH_TRIAC_HAT#Introduction

## Setup 
```
sudo apt install python3-serial
sudo apt install python3-rpi.gpio
sudo apt install python3-pip
sudo apt install python3-smbus
sudo raspi-config # Select I2C from the interfaces.

# Check i2c is connected
sudo i2cdetect -y 0

```
