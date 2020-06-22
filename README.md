# blindsmcu

NodeMCU (ESP8266) based controller system for blinds. This project includes the following:

* Adapted code for ESP8266 based chips that allows the following
	* Wifi Discovery (configure wifi when the board first launches or fails to connect)
	* Captivate based Wifi setup.
	* Web based Wifi Config.
	* Supports two blind motors independently.
	* Web based controller interface.
* Code to bridge the device to Amazon Alexa.
* Mobile Application for iOS and Android.

## Example

```sh
curl "http://192.168.65.183/binds?blind=1&position=0"
```

## Links

**Wifi Manager**

https://github.com/tzapu/WiFiManager
https://tzapu.com/esp8266-wifi-connection-manager-library-arduino-ide/

**ESP8266 Web Server**

https://techtutorialsx.com/2016/10/22/esp8266-webserver-getting-query-parameters/


