# myhome

This simple application for ESP32 acts as a gateway between a UART channel and a MQTT publish/suscribe client over AWS IoT service. 
This program is derived from examples in the esp-idf SDK. The version used for this program is esp-idf v2.1 (https://github.com/espressif/esp-idf).

Data received from the subscribed topic are sent through UART2 while data received from UART2 are published to another topic. These topics are listed here below:
* subscribe: myhome/command
* publish: myhome/monitor

Data received from UART are in JSON format as array of key and value fields: See the following example:

```javascript
{"pubkey1":"pubvalue1"},{"pubkey2":"pubvalue2"},{"pubkey3":"pubvalue3"}
```

These data are encapsulated as a JSON value field and then the resulting string is sent to the topic (publish) as above. See the example string below:

```javascript
{"monitor":[{"pubkey1":"pubvalue1"},{"pubkey2":"pubvalue2"},{"pubkey3":"pubvalue3"}]}
```

Similarly, data received from the subscribed topic, in JSON format, are encapsulated and then the resulting string is sent through UART. See the example string below:

```javascript
{"command":[{"subkey1":"subvalue1"},{"subkey2":"subvalue2"},{"subkey3":"subvalue3"}]}
```

Two special messages are sent to the UART channel to notify when device is connected to AWS and subscribed to the topic and in case of a disconnection or error event.
This messages are respectively
* connected, subscribed and ready to publish: {"config":"ready"}
* disconnection or an other error occurred: {"config":"error"}

# Configuration

Clone this repository. Remember to export the IDF_PATH and add the location of the esp32 compiler to the environment PATH.

```
export PATH=$PATH:[your_directory]
export IDF_PATH=[your_directory]
```

Then configure necessary parameters as any other example of the SDK

	$ make menuconfig 

Configure your AWS Client ID, WiFi SSID and password. Following the AWS example in the SDK, create your certificate and place the necessary files in the certs folder (certificate.pem.crt and private.pem.key). These files are then embedded in the flash memory. Be sure that the certificate is enabled from your AWS console, create and add a policy and a thing (see README file of the AWS example in the SDK).
Compile and flash the device

	$ make
	$ make flash

NOTE: log info is on UART0 while data are exchanged through UART2.




