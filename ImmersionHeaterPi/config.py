#!/usr/bin/python
# -*- coding:utf-8 -*-
import RPi.GPIO as GPIO
import serial
import smbus
import time
# dev = "/dev/ttySC0"


class config(object):
    def __init__(ser, Baudrate = 115200, dev = "/dev/ttyS0", data_mode = 1, address=0x47):
        
        ser.data_mode = data_mode
        GPIO.setmode(GPIO.BCM)
        GPIO.setwarnings(False)
        if(data_mode == 1):
            print (dev)
            print (Baudrate)
            ser.dev = dev
            ser.serial = serial.Serial("/dev/ttyS0",Baudrate)
        elif(data_mode == 0):
            print ('I2C')
            ser.i2c = smbus.SMBus(1)
            ser.address = address
            
    def UART_SendByte(ser, value): 
        ser.serial.write(value) 
    
    def UART_SendnByte(ser, value, Num):
        for i in range(0, Num):
            ser.UART_SendByte([value[i]]) 
    
    def UART_SendString(ser, value): 
        ser.serial.write(value.encode('ascii'))

    def UART_ReceiveByte(ser): 
        return ser.serial.read(1).decode("utf-8")

    def UART_ReceiveString(ser, value): 
        data = ser.serial.read(value)
        return data.decode("utf-8")
        
    def Uart_Set_Baudrate(ser, Baudrate):
        ser.serial = serial.Serial(ser.dev, Baudrate)
         
    def I2C_SendByte(ser, reg, value): 
        ser.i2c.write_byte_data(ser.address, reg, value) 
        
    def I2C_SendWord(ser, reg, value): 
        ser.i2c.write_word_data(ser.address, reg, value)     
        
         
         
         