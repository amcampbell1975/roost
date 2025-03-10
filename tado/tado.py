#
#python3 -m pip install requests
#python3 -m pip install urllib3==1.26.6  #https://stackoverflow.com/questions/76187256/importerror-urllib3-v2-0-only-supports-openssl-1-1-1-currently-the-ssl-modu
import requests
from tado_secret import * # USERNAME & PASSWORD

#https://blog.scphillips.com/posts/2017/01/the-tado-api-v2/
#https://shkspr.mobi/blog/2019/02/tado-api-guide-updated-for-2019/

#mqtt import paho.mqtt.client as mqtt
import time
import json

MQTT_ADDRESS="localhost"
MQTT_ADDRESS="192.168.1.250"

print("Tado Starting...")
#mqtt client = mqtt.Client("MyTado") #create new instance
#mqtt client.connect(MQTT_ADDRESS) #connect to broker



CLIENT_SECRET="wZaRN7rpjn3FoNyF5IFuxg9uMzYJcvOoQ8QWiIqS3hfk6gLhVlG57j5YNoZL2Rtc"

data = {
    'client_id' : 'tado-web-app',
    'grant_type': 'password',
    'scope'     : "home.user" ,
    'username'  : USERNAME ,
    'password'  : PASSWORD,
    "client_secret" : CLIENT_SECRET,
}

def GetTado():
    mqtt_data=[]
    response = requests.post('https://auth.tado.com/oauth/token', data=data)
    try:
        if response.status_code==200:
            ACCESS_TOKEN=response.json()["access_token"]

            headers = {
                'Authorization': 'Bearer ' + ACCESS_TOKEN,
            }
            response = requests.get('https://my.tado.com/api/v1/me', headers=headers)
            
            if response.status_code==200:
                HOME_ID=response.json()["homeId"]
                print("HOME_ID=",  HOME_ID)


                headers = {
                    'Authorization': 'Bearer ' + ACCESS_TOKEN,
                }
                response = requests.get('https://my.tado.com/api/v2/homes/' + str(HOME_ID) +'/zones', headers=headers, timeout=5)

                sensors={}
                if response.status_code==200:
                    for d in response.json():
                        #print (d["id"], d["name"])
                        sensors[d["id"]]= d["name"]

                    for id, name in sensors.items():
                        response = requests.get('https://my.tado.com/api/v2/homes/' + str(HOME_ID) +'/zones/' + str(id)+  '/state', headers=headers)
                        
                        if response.status_code==200:
                            #HOME_ID=response.json()["homeId"]
                            current_temp=response.json()["sensorDataPoints"]["insideTemperature"]["celsius"]
                            request_temp=response.json()["setting"]["temperature"]["celsius"]
                            valve       =response.json()["activityDataPoints"]["heatingPower"]["percentage"]
                            humidity    =response.json()["sensorDataPoints"]["humidity"]["percentage"]
                            print("%5s   " %id, 
                                "%13s  " %name, 
                                "%5s   " %current_temp, 
                                "%5s   " %request_temp, 
                                "%5s   " %valve, 
                                "%5s   " %humidity )
                            # pack data ready for MQTT
                            #mqtt_data=[current_temp,request_temp,valve,humidity]
                            mqtt_data.append([name, current_temp,request_temp,valve,humidity])
                            #mqtt client.publish("tado/%s" %name, json.dumps(mqtt_data) )
                        else:
                            print("FAIL (D) %d" %response.status_code)
                            raise Exception("FAIL (D) %d" %response.status_code )
                else:
                    print("FAIL (C) %d" %response.status_code)
                    raise Exception("FAIL (C) %d" %response.status_code )
            else:
                print("FAIL (B) %d" %response.status_code)
                raise Exception("FAIL (B) %d" %response.status_code )
        else:
            print("FAIL (A) %d" %response.status_code)
            print("response.text='%s'" %response.text)
            raise Exception("FAIL (A) %d" %response.status_code )
    except:
        None    
    return mqtt_data

if __name__ == '__main__':
    while(True):
        mqtt=GetTado()
        for i in mqtt:
            print("tado/current_temp/%s/%s" %(i[0],i[1]) )
            print("tado/request_temp/%s/%s" %(i[0],i[2]) )
            print("tado/valve/%s/%s"        %(i[0],i[3]) )
            print("tado/humidity/%s/%s"     %(i[0],i[4]) )

        #print(mqtt)
        time.sleep(60*5)
