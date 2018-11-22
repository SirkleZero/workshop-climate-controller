#include <Arduino.h>
#include <Wire.h>
#include <MemoryFree.h>
#include <pgmStrToRAM.h>

#include "workshop-climate-lib.h" // unsure exactly why this has to be here for this to compile. without it, the sub-directory .h files aren't found. Probably has something to do with not finding the library if nothing is loaded from the root of the src folder.
#include "Sensors\SensorData.h"
#include "Relay\HumidityRelayManager.h"
#include "RX\RFM69RXProxy.h"
#include "RX\SensorTransmissionResult.h"
#include "TX\AdafruitIOProxy.h"
#include "TX\IoTUploadResult.h"
#include "Display\RXTFTFeatherwingProxy.h"
#include "Configuration\SDCardProxy.h"
#include "Configuration\Secrets.h"
#include "Configuration\ControllerConfiguration.h"

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
bool radioEnabled, internetEnabled, sdCardEnabled, displayEnabled, relayEnabled;

// objects that handle functionality
HumidityRelayManager relayManager;
RFM69RXProxy radio;
AdafruitIOProxy httpClient;
RXTFTFeatherwingProxy display;
SDCardProxy sdCard;

/*
we need our modules in the following priority order:
1. SD Card - Contains all of our configuration. Can't run without this information.
2. The Display - we can technically function without a display, but we're going to use it to display errors.
3. Relay Manager - no point in doing any of this if the relay handling isn't there
4. The RFM69 Radio - Sends us data. Without it, again, no point.
5. The Internet / adafruit - again, we can function without this.
*/
void setup()
{
	Serial.begin(115200);
	//while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	if (sdCard.Initialize().IsSuccessful)
	{
		sdCard.LoadSecrets(&secrets);
		sdCard.LoadConfiguration(&controllerConfiguration);

		if (display.Initialize().IsSuccessful)
		{
			display.Clear();
			display.DrawLayout();

			display.PrintSensors(SensorData::EmptyData());
			display.PrintFreeMemory(freeMemory());

			if (relayManager.Initialize(&controllerConfiguration).IsSuccessful)
			{
				if (radio.Initialize().IsSuccessful)
				{
					internetEnabled = httpClient.Initialize(&secrets).IsSuccessful;
				}
			}
		}
	}
}

void loop()
{
	/*
	Use the emergency shutoff function to shut off the relays if a pre-determined time amount has lapsed. All of this logic is within this method, no other calls are necessary. The KeepAlive() method is essentially a dead man switch that this method uses to either keep things going, or, if the sensor array functionality doesn't transmit anything or we don't receive anything, we shut down power to all our devices.

	This is a safety thing.
	*/
	relayManager.EmergencyShutoff();

	// !!! CRITICAL !!!
	// the rxProxy listen function needs to execute as often as possible to not miss any messages or acknowledgements. it would be bad to have the loop have a delay call in it, messages will be lost.
	// DO NOT put a delay call in the loop function!
	result = radio.Listen();

	if (result.HasResult)
	{
		display.PrintSensors(result.Data);
		display.PrintFreeMemory(freeMemory());

		relayManager.AdjustClimate(result.Data);

		// if the internet isn't working for some reason, don't bother trying to upload anything.
		if (internetEnabled)
		{
			uploadResult = httpClient.Transmit(result.Data);
			if (!uploadResult.IsSuccess)
			{
				// something didn't work here, so let's display an error message!
			}
		}

		// calling Initialize on the rxProxy is a total hack. It re-initializes the RF69 radio because the radio head library doesn't handle shared SPI bus very well (apparently). If we don't reinitialize this, the loop will catch only the first transmission, and after that it won't catch anything. This "fixes" that issue. Yes, it's dumb and shared SPI sucks, at least in this case.
		InitializationResult resetResult = radio.Reset();
		if (!resetResult.IsSuccessful)
		{
			// something didn't work here, so let's display an error message!
		}

		// display free memory after things have run.
		display.PrintFreeMemory(freeMemory());
	}
}
