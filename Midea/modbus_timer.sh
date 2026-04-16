# -u  unbuffer nohup
# https://unix.stackexchange.com/questions/225460/nohup-not-updating-nohup-out
nohup python3 -u modbus_timer.py > nohup.modbus_timer.log & 
