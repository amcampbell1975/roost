/*
  xsns_79_as608.ino - AS608 and R503 fingerprint sensor support for Tasmota

  Copyright (C) 2021  boaschti and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef USE_AMC
#define USE_AMC
#endif


#ifdef USE_AMC
/*********************************************************************************************\
 * AS608 optical and R503 capacitive Fingerprint sensor
 *
 * Uses Adafruit-Fingerprint-sensor-library with TasmotaSerial
 *
 * Changes made to Adafruit_Fingerprint.h and Adafruit_Fingerprint.cpp:
 * - Replace SoftwareSerial with TasmotaSerial
 * - Add defined(ESP32) where also defined(ESP8266)
\*********************************************************************************************/

#define XSNS_100               100

//#define USE_AS608_MESSAGES

//#define D_JSON_FPRINT "FPrint"

//EMONLIB

// define theoretical vref calibration constant for use in readvcc()
// 1100mV*1024 ADC steps http://openenergymonitor.org/emon/node/1186
// override in your code with value for your specific AVR chip
// determined by procedure described under "Calibrating the internal reference voltage" at
// http://openenergymonitor.org/emon/buildingblocks/calibration

#define READVCC_CALIBRATION_CONST 1126400L

// to enable 12-bit ADC resolution on Arduino Due,
// include the following line in main sketch inside setup() function:
//  analogReadResolution(ADC_BITS);
// otherwise will default to 10 bits, as in regular Arduino-based boards.
#define ADC_BITS    12
#define ADC_COUNTS  (1<<ADC_BITS)


class EnergyMonitor
{
  public:

    void voltage(unsigned int _inPinV, double _VCAL, double _PHASECAL);
    void current(unsigned int _inPinI, double _ICAL);
    void calcVI(void);

    long readVcc();
    //Useful value variables
    float realPower,
      apparentPower,
      powerFactor,
      Vrms,
      Irms;

    float offsetV;                          //Low-pass filter output
    float offsetI;                          //Low-pass filter output
    float lastFilteredV,filteredV;          //Filtered_ is the raw analog value minus the DC offset

    int adc_i_max;
    int adc_i_min;
    int adc_v_max;
    int adc_v_min;
    
  private:

    //Set Voltage and current input pins
    unsigned int inPinV;
    unsigned int inPinI;
    //Calibration coefficients
    //These need to be set in order to obtain accurate results
    float VCAL;
    float ICAL;
    float PHASECAL;

    //--------------------------------------------------------------------------------------
    // Variable declaration for emon_calc procedure
    //--------------------------------------------------------------------------------------
    int sampleV;                        //sample_ holds the raw analog read value
    int sampleI;

    float filteredI;
    float phaseShiftedV;                      //Holds the calibrated phase shifted voltage.
    float sqV,sumV,sqI,sumI,instP,sumP;       //sq = squared, sum = Sum, inst = instantaneous
    //int startV;                               //Instantaneous voltage at start of sample window.
    //boolean lastVCross, checkVCross;          //Used to measure number of times threshold is crossed.

};


//--------------------------------------------------------------------------------------
// Sets the pins to be used for voltage and current sensors
//--------------------------------------------------------------------------------------
void EnergyMonitor::voltage(unsigned int _inPinV, double _VCAL, double _PHASECAL)
{
  inPinV = _inPinV;
  VCAL = _VCAL;
  PHASECAL = _PHASECAL;
  offsetV = ADC_COUNTS>>1;
}

void EnergyMonitor::current(unsigned int _inPinI, double _ICAL)
{
  inPinI = _inPinI;
  ICAL = _ICAL;
  offsetI = ADC_COUNTS>>1;
}


//--------------------------------------------------------------------------------------
// emon_calc procedure
// Calculates realPower,apparentPower,powerFactor,Vrms,Irms,kWh increment
// From a sample window of the mains AC voltage and current.
// The Sample window length is defined by the number of half wavelengths or crossings we choose to measure.
//--------------------------------------------------------------------------------------
void EnergyMonitor::calcVI(void)
{

  AddLog(LOG_LEVEL_DEBUG, PSTR("EnergyMonitor::calcVI(a)"));
  //Serial.println("EnergyMonitor::calcVI(a)");

  int SupplyVoltage = 3300;

  unsigned int crossCount = 0;                             //Used to measure number of times threshold is crossed.
  unsigned int numberOfSamples = 0;                        //This is now incremented

  adc_i_max=-1;  adc_i_min = 12345;
  adc_v_max=-1;  adc_v_min = 12345;

  //-------------------------------------------------------------------------------------------------------------------------
  // 1) Waits for the waveform to be close to 'zero' (mid-scale adc) part in sin curve.
  //-------------------------------------------------------------------------------------------------------------------------
  unsigned long start = millis();    //millis()-start makes sure it doesnt get stuck in the loop if there is an error.
  unsigned long end;
  //while(1)                                   //the while loop...
  //{
  //  const float ADC_VIN_ZERO = 1300;
  //  startV = analogRead(inPinV);                    //using the voltage waveform
  //  if ((startV > (ADC_VIN_ZERO*0.95)) && (startV < (ADC_VIN_ZERO*1.05))){
  //    Serial.println("EnergyMonitor::calcVI(out of range!)");
  //    break;  //check its within range
  //  }
  //  if ((millis()-start)>timeout) {
  //      Serial.println("EnergyMonitor::calcVI(timeout!)");
  //      break;
  //  }
  //  delay(2);
  //}
  
  //Serial.println("EnergyMonitor::calcVI(b)");



  start = millis();
  const int array_size=40; // tune this to get total sampling=20ms 1/50Hz
  uint16 V_array[array_size];
  uint16 I_array[array_size];
  for(int i=0;i<array_size;i++){
    sampleV = analogRead(inPinV);                 //Read in raw voltage signal
    sampleI = analogRead(inPinI);                 //Read in raw current signal
    V_array[i]=sampleV;
    I_array[i]=sampleI;
  }
  end=millis();
  
  AddLog(LOG_LEVEL_DEBUG, PSTR("EnergyMonitor::calcVI loop time = %d"),end-start );
  //Serial.print("loop time=");
  //Serial.println(end-start);

  //-------------------------------------------------------------------------------------------------------------------------
  // 2) Main measurement loop
  //-------------------------------------------------------------------------------------------------------------------------
  start = millis();
  
  unsigned long delta_ms;



  //while ((crossCount < crossings) && ((millis()-start)<timeout))
//  while ((delta_ms=(millis()-start))<timeout)
  
  for(int i=0;i<array_size;i++)
  {
    //if( (millis()-start) >timeout ){
    //    Serial.println("EnergyMonitor::calcVI(c timeout!)");
    //    break;
    //}
    numberOfSamples++;                       //Count number of times looped.
    lastFilteredV = filteredV;               //Used for delay/phase compensation

    //-----------------------------------------------------------------------------
    // A) Read in raw voltage and current samples
    //-----------------------------------------------------------------------------
    //Serial.print("EnergyMonitor::calcVI(cc 1)");
    //Serial.print(delta_ms);
    //Serial.print(" ");
    //Serial.println(numberOfSamples);

    sampleV = V_array[i];// VanalogRead(inPinV);                 //Read in raw voltage signal
    sampleI = I_array[i];// analogRead(inPinI);                 //Read in raw current signal

    //-----------------------------------------------------------------------------
    // B) Apply digital low pass filters to extract the 2.5 V or 1.65 V dc offset,
    //     then subtract this - signal is now centred on 0 counts.
    //-----------------------------------------------------------------------------
    offsetV = offsetV + ((sampleV-offsetV)/1024);
    filteredV = sampleV - offsetV;
    offsetI = offsetI + ((sampleI-offsetI)/1024);
    filteredI = sampleI - offsetI;
    
//    Serial.println("EnergyMonitor::calcVI(cc 2)");
//-----------------------------------------------------------------------------
    // C) Root-mean-square method voltage
    //-----------------------------------------------------------------------------
    sqV= filteredV * filteredV;                 //1) square voltage values
    sumV += sqV;                                //2) sum
 // 
 //   Serial.println("EnergyMonitor::calcVI(cc 3)");
 //   Serial.print(sqI);
 //   Serial.print(" ");
 //   Serial.print(filteredI);
 //   Serial.print(" ");
 //   Serial.print(sumI);
 //   Serial.println("");
 //   
    
    //-----------------------------------------------------------------------------
    // D) Root-mean-square method current
    //-----------------------------------------------------------------------------
    sqI = filteredI * filteredI;                //1) square current values
    sumI += sqI;                                //2) sum

    //-----------------------------------------------------------------------------
    // E) Phase calibration
    //-----------------------------------------------------------------------------
//    Serial.println("EnergyMonitor::calcVI(cc 4)");

    phaseShiftedV = lastFilteredV + PHASECAL * (filteredV - lastFilteredV);
    if (sampleV > adc_v_max)
      adc_v_max = sampleV;

    if (sampleI > adc_i_max)
      adc_i_max = sampleI;


    if (sampleV < adc_v_min)
      adc_v_min = sampleV;

    if (sampleI < adc_i_min)
      adc_i_min = sampleI;

//    Serial.println("EnergyMonitor::calcVI(cc 5)");

    //-----------------------------------------------------------------------------
    // F) Instantaneous power calc
    //-----------------------------------------------------------------------------
    instP = phaseShiftedV * filteredI;          //Instantaneous Power
    sumP +=instP;                               //Sum

//    Serial.println("EnergyMonitor::calcVI(cc 6)");

    //-----------------------------------------------------------------------------
    // G) Find the number of times the voltage has crossed the initial voltage
    //    - every 2 crosses we will have sampled 1 wavelength
    //    - so this method allows us to sample an integer number of half wavelengths which increases accuracy
    //-----------------------------------------------------------------------------
    //lastVCross = checkVCross;
    //if (sampleV > startV) checkVCross = true;
    //                 else checkVCross = false;
    //
    //Serial.println("EnergyMonitor::calcVI(cc 7)");
    //
    //if (numberOfSamples==1) lastVCross = checkVCross;
    //Serial.println("EnergyMonitor::calcVI(cc 8)");

    //if (lastVCross != checkVCross) crossCount++;
//    Serial.println("EnergyMonitor::calcVI(cc 9)");

  }
  //Serial.println("EnergyMonitor::calcVI(c)");

  //Serial.println(adc_v_min);
  //Serial.println(adc_v_max);
  //-------------------------------------------------------------------------------------------------------------------------
  // 3) Post loop calculations
  //-------------------------------------------------------------------------------------------------------------------------
  //Calculation of the root of the mean of the voltage and current squared (rms)
  //Calibration coefficients applied.

  float V_RATIO = VCAL *((SupplyVoltage/1000.0) / (ADC_COUNTS));
  Vrms = V_RATIO * sqrt(sumV / numberOfSamples);

  float I_RATIO = ICAL *((SupplyVoltage/1000.0) / (ADC_COUNTS));
  Irms = I_RATIO * sqrt(sumI / numberOfSamples);

  //Calculation power values
  realPower = V_RATIO * I_RATIO * sumP / numberOfSamples;
  apparentPower = Vrms * Irms;
  powerFactor=realPower / apparentPower;


  AddLog(LOG_LEVEL_DEBUG, PSTR("EnergyMonitor::calcVI realPower     = %f"),realPower );
  AddLog(LOG_LEVEL_DEBUG, PSTR("EnergyMonitor::calcVI apparentPower = %f"),apparentPower );
  AddLog(LOG_LEVEL_DEBUG, PSTR("EnergyMonitor::calcVI powerFactor   = %f"),powerFactor );

  //Reset accumulators
  sumV = 0;
  sumI = 0;
  sumP = 0;
  
//--------------------------------------------------------------------------------------
}


//EMONLIB
EnergyMonitor emon1;  




void As608Init(void) {
}


/*********************************************************************************************\
* Commands
\*********************************************************************************************/


/*********************************************************************************************\
* Interface
\*********************************************************************************************/

bool Xsns100(uint8_t function) {
  bool result = false;

  if (FUNC_INIT == function) {
    //As608Init();
    AddLog(LOG_LEVEL_DEBUG, PSTR("AMC debug log message"));
    


    //esp8266 debug
    //emon1.current(A0, 54.1);       // Current: input pin, calibration.
    //emon1.voltage(A0, 1427, 2.5);  // Voltage: input pin, calibration, phase_shift
    //esp32 
    const int VoltsPin = 34;   //GPIO 34 (Analog ADC1_CH6) 
    const int AmpsPin = 32;
    emon1.current(AmpsPin, 54.1);       // Current: input pin, calibration.
    emon1.voltage(VoltsPin, 1427, 2.5);  // Voltage: input pin, calibration, phase_shift

  }
  //else if(1==2) // (As608.selected) 
  {
    switch (function) {
      case FUNC_EVERY_SECOND:
        static int count=0;
        if(count%30==0){
          emon1.calcVI();
        }
        count++;
        break;
      case FUNC_JSON_APPEND:
        AddLog(LOG_LEVEL_DEBUG, PSTR("AMC debug log FUNC_JSON_APPEND, %f "),emon1.realPower);
        ResponseAppend_P(PSTR(",\"PowerMonitor\": %f "),emon1.realPower);//   {\"" D_JSON_ECO2 "\":%d,\"" D_JSON_TVOC "\":%d}"), 123,456);
        break;

      //case FUNC_COMMAND:
      //  result = DecodeCommand(kAs608Commands, As608Commands);
      //  break;
    }
  }
  return result;
}

#endif  // USE_AMC
