# APIs in OpenBTS

There are going to be more and more APIs made available in OpenBTS. This is a live reference to their schemas and current versions. The Commands API is documented in the handbook in the NodeManager section. This document focuses on the the new events APIs.

Individual Events API can be enabled or disabled via the key listed in each section. Also, a specific version of that API can be selected for application compatibility. Events are published on the port defined by the key: ```NodeManager.Events.Port```. An example client is in apps/JSONEventsClient.cpp.


## PhysicalStatus Events API

 - Current Version: 0.1
 - Available Versions: 0.1
 - Configured Via: ```NodeManager.API.PhysicalStatus```

The PhysicalStatus API provides raw physical readings from the SACCH (Slow Associated Control CHannel). The SACCH is active when interacting with a MS (Mobile Station) via a SDCCH (Standard Dedicated Control CHannel) or a TCH (Traffic CHannel). SDCCH interactions can be SMS exchanges, LUR (Location Update Requests) or voice call setups. TCH interactions are voice calls once media is flowing or GPRS sessions.

Readings are available approximately every half-second on the SACCH. These readings contain information about the burst itself, the logical channel that is being used and neighbor reports from the handset.

An example for version 0.1 of the event data emitted by this API follows:

```
{
	"name" : "PhysicalStatus",
	"timestamp" : "18446744072283447705",
	"version" : "0.1",
	"data" : {
		"burst" : {
			"RSSI" : -49.4808,
			"RSSP" : -27.4808,
			"actualMSPower" : 11,
			"actualMSTimingAdvance" : 0,
			"timingError" : 1.59709
		},
		"channel" : {
			"ARFCN" : 153,
			"IMSI" : "001010000000001",
			"carrierNumber" : 0,
			"timeslotNumber" : 0,
			"typeAndOffset" : "SDCCH/4-1",
			"uplinkFrameErrorRate" : 0
		},
		"reports" : {
			"neighboringCells" : [],
			"servingCell" : {
				"RXLEVEL_FULL_dBm" : -67,
				"RXLEVEL_SUB_dBm" : -67,
				"RXQUALITY_FULL_BER" : 0,
				"RXQUALITY_SUB_BER" : 0
			}
		}
	}
}
```
