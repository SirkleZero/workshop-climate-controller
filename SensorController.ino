#include <Arduino.h>
#include <Wire.h>
#include <MemoryFree.h>
#include <pgmStrToRAM.h>

#include "workshop-climate-lib.h" // unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files aren't found. Probably has something to do with not finding the library if nothing is loaded from the root of the src folder.
#include "Sensors\SensorData.h"
#include "Relay\HumidityRelayManager.h"
#include "RX\RFM69RXProxy.h"
#include "TX\AdafruitIOProxy.h"
#include "TX\IoTUploadResult.h"
#include "Display\RXTFTFeatherwingProxy.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\Secrets.h"
#include "Configuration\ControllerConfiguration.h"
#include "SensorTransmissionResult.h"

using namespace Configuration;
using namespace Display;
using namespace Relay;
using namespace RX;
using namespace Sensors;
using namespace TX;

// objects that store data
Secrets secrets;
SensorTransmissionResult result;
IoTUploadResult uploadResult;
ControllerConfiguration controllerConfiguration;

// objects that handle functionality
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
    sdCard.LoadConfiguration(&controllerConfiguration);

    httpClient.Initialize(&secrets);

    display.Initialize();
    display.Clear();
    display.DrawLayout();

    display.PrintSensors(SensorData::EmptyData());
    display.PrintFreeMemory(freeMemory());

    rxProxy.Initialize();

    relayManager.Initialize(&controllerConfiguration);
}

void loop() {
    // !!! CRITICAL !!!
    // the rxProxy listen function needs to execute as often as possible to not miss any messages
    // or acknowledgements. it would be bad to have the loop have a delay call in it, messages will be lost.
    // DO NOT put a delay call in the loop function!
    result = rxProxy.Listen();

    if(result.HasResult) {
        display.PrintSensors(result.Data);
        display.PrintFreeMemory(freeMemory());

        uploadResult = httpClient.Transmit(result.Data);
        if(uploadResult.IsSuccess){
            relayManager.KeepAlive();
            relayManager.AdjustClimate(result.Data);
        }
        //uploadResult.PrintDebug();

        // calling Initialize on the rxProxy is a total hack. It re-initializes the RF69 radio
        // because the radio head library doesn't handle shared SPI bus very well (apparently).
        // If we don't reinitialize this, the loop will catch only the first transmission, and 
        // after that it won't catch anything. This "fixes" that issue. Yes, it's dumb and shared
        // SPI sucks.
        rxProxy.Initialize();

        // display free memory after things have run.
        display.PrintFreeMemory(freeMemory());
    }
}
