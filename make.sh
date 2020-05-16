./arduino-cli 
./arduino-cli config init
./arduino-cli sketch new MyFirstSketch
./arduino-cli core update-index
./arduino-cli board list
./arduino-cli board listall
./arduino-cli  core update-index --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
./arduino-cli lib install arduino-cli NTPClient
#./arduino-cli compile --fqbn arduino:esp8266 MyFirstSketch
./arduino-cli board listall
./arduino-cli compile --fqbn esp32:esp32:node32s adc_test
./arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:node32s adc_test
