#include <Arduino.h>
#include <Wire.h>
#include <MemoryFree.h>
#include <pgmStrToRAM.h>
#include <Adafruit_SleepyDog.h>

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
bool systemRunnable = true;
InitializationResult internetEnabled;

// objects that handle functionality
SDCardProxy sdCard;
RXTFTFeatherwingProxy display;
HumidityRelayManager relayManager;
RFM69RXProxy radio;
AdafruitIOProxy httpClient;

/*
we need our modules in the following priority order:
1. SD Card - Contains all of our configuration. Can't run without this information.
2. The Display - we can technically function without a display, but we're going to use it to display errors, so uh, it's required.
3. Relay Manager - no point in doing any of this if the relay handling isn't there
4. The RFM69 Radio - Sends us data. Without it, again, no point.
5. The Internet / adafruit - we can function without this, it's the only component not really required.
*/
void setup()
{
	Serial.begin(115200);
	//while (!Serial); // MAKE SURE TO REMOVE THIS!!!

	// cascading checks to make sure all our everything thats required is initialized properly.
	if (display.Initialize().IsSuccessful)
	{
		display.Clear();
		display.DrawLayout();

		display.PrintSensors(SensorData::EmptyData());
		display.PrintFreeMemory(freeMemory());

		if (sdCard.Initialize().IsSuccessful)
		{
			sdCard.LoadSecrets(&secrets);
			sdCard.LoadConfiguration(&controllerConfiguration);

			InitializationResult relayManagerResult = relayManager.Initialize(&controllerConfiguration);
			if (relayManagerResult.IsSuccessful)
			{
				InitializationResult radioResult = radio.Initialize();
				if (radioResult.IsSuccessful)
				{
					internetEnabled = httpClient.Initialize(&secrets);
				}
				else
				{
					systemRunnable = false;
					display.PrintError(radioResult.ErrorMessage);
					Serial.println(radioResult.ErrorMessage);
					sdCard.LogMessage(radioResult.ErrorMessage);
				}
			}
			else
			{
				systemRunnable = false;
				display.PrintError(relayManagerResult.ErrorMessage);
				Serial.println(relayManagerResult.ErrorMessage);
				sdCard.LogMessage(relayManagerResult.ErrorMessage);
			}
		}

		// IMPORTANT! Turn on the watch dog timer and enable at the maximum value. For the M0 this is approximately 16 seconds, after whic the watch dog will restart the device.
		// This exists purely as a stability mechanism to mitigate device lockups / hangs / etc.
		Watchdog.enable();
		sdCard.LogMessage(F("Watchdog timer enabled during device setup."));
	}
}

void loop()
{
	if (systemRunnable)
	{
		// reset the watchdog with each loop iteration. If the loop hangs, the watchdog will reset the device.
		Watchdog.reset();

		/*
		Use the emergency shutoff function to shut off the relays if a pre-determined time amount has lapsed. All of this logic is within this method, no other calls are necessary. The KeepAlive() method is essentially a dead man switch that this method uses to either keep things going, or, if the sensor array functionality doesn't transmit anything or we don't receive anything, we shut down power to all our devices.

		This is a safety thing.
		*/
		relayManager.EmergencyShutoff();

		// !!! CRITICAL !!!
		// the rxProxy listen function needs to execute as often as possible to not miss any messages or acknowledgements. it would be bad to have the loop have a delay call in it, messages will be lost.
		// DO NOT put a delay call in the loop function!
		result = radio.Listen();
		Watchdog.reset();

		if (result.HasResult)
		{
			display.PrintSensors(result.Data);
			display.PrintFreeMemory(freeMemory());

			relayManager.AdjustClimate(result.Data);
			Watchdog.reset();

			// if the internet isn't working for some reason, don't bother trying to upload anything.
			if (internetEnabled.IsSuccessful)
			{
				uploadResult = httpClient.Transmit(result.Data);
				// The http transmission is likely the most time consuming thing in this application.
				// Make sure to reset the watchdog after it's completed or the device will reboot!
				Watchdog.reset();

				if (!uploadResult.IsSuccess)
				{
					display.PrintError(uploadResult.ErrorMessage);
					Serial.println(uploadResult.ErrorMessage);
					sdCard.LogMessage(uploadResult.ErrorMessage);
				}
			}

			// calling Reset on the radio is a total hack. It re-initializes the RF69 radio because the radio head library doesn't handle shared SPI bus very well (apparently). If we don't reinitialize this, the loop will catch only the first transmission, and after that it won't catch anything. This "fixes" that issue. Yes, it's dumb and shared SPI sucks, at least in this case.
			InitializationResult resetResult = radio.Reset();
			if (!resetResult.IsSuccessful)
			{
				// something didn't work here, so let's...
				// 1. Shut down due to an error. This should keep us from turning on a device and then having an error prevent another cycle from running. This prevents us from finding the system in a permanently on state; a permanently off state is preferable by far.
				// 2. Display an error message
				relayManager.ShutDownError();

				display.PrintError(resetResult.ErrorMessage);
				Serial.println(resetResult.ErrorMessage);
				sdCard.LogMessage(resetResult.ErrorMessage);
			}

			// display free memory after things have run.
			display.PrintFreeMemory(freeMemory());
		}
	}
	else
	{
		// the needed components of the system are not present or working, show a message
		const __FlashStringHelper *msg = F("One or more components failed to initialize or run.");
		Serial.println(msg);
		display.PrintError(msg);
	}
}
