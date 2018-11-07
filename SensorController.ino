#include <Arduino.h>
#include <Wire.h>
#include "SensorData.h"
#include "HumidityRelayManager.h"
#include "RFM69RXProxy.h"
#include "SensorTransmissionResult.h"
#include "AdafruitIOProxy.h"
#include "RXTFTFeatherwingProxy.h"
#include "SDCardProxy.h"
#include "Secrets.h"

using namespace Display;
using namespace Relay;
using namespace RX;
using namespace TX;
using namespace Configuration;

const unsigned long sampleFrequency = 10000; // ms, how often the sensors are configured to send data
const unsigned int allowedReceiveFailures = 10; // number of times we can fail to get a sensor reading before we terminate

Secrets secrets;
HumidityRelayManager relayManager;
RFM69RXProxy rxProxy;
AdafruitIOProxy httpClient;
RXTFTFeatherwingProxy display;
SDCardProxy sdCard;

void setup() {
    Serial.begin(115200);

    //while (!Serial); // MAKE SURE TO REMOVE THIS!!!

    sdCard.Initialize();
    sdCard.LoadSecrets(&secrets);
    httpClient.Initialize(&secrets);

    display.Initialize();
    display.Clear();
    display.DrawLayout();

    display.PrintSensors(SensorData::EmptyData());

    rxProxy.Initialize();

    //relayManager.Initialize();
}

SensorTransmissionResult result;
void loop() {
    // !!! CRITICAL !!!
    // the rxProxy listen function needs to execute as often as possible to not miss any messages
    // or acknowledgements. it would be bad to have the loop have a delay call in it, messages will be lost.
    // DO NOT put a delay call in the loop function!
    result = rxProxy.Listen();

    if(result.HasResult) {
        display.PrintSensors(result.Data);

        //relayManager.SetRelayState(&result.Data);

        httpClient.Transmit(result.Data);

        // calling Initialize on the rxProxy is a total hack. It re-initializes the RF69 radio
        // because the radio head library doesn't handle shared SPI bus very well (apparently).
        // If we don't reinitialize this, the loop will catch only the first transmission, and 
        // after that it won't catch anything. This "fixes" that issue. Yes, it's dumb and shared
        // SPI sucks.
        rxProxy.Initialize();
    }
}
