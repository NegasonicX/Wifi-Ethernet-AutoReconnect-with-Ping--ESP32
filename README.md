# Wifi & Ethernet AutoReconnect with Ping on ESP32
This code aims to use both Wifi and Ethernet together as well as determines where ESP32 is getting Internet services vis Ping process.

## GPIO Functions:
I am using ENC28J60 Ethernet Module and Devkit v1's integrated Wifi.
* GPIO 18 :
*
*
*
*

## Understanding the Flow:
* This code is developed for ESP32 on Embedded C Language using FreeRTOS.
* Even if ESP32 is connected to an AP in station mode or Ethernet cable is plug in, you will still get the IP but that won't ensure whether ESP32 is receiving Internet services.
* As per my rigorous research and programming, the only way I found out to know whether ESP32 has internet services or not was to ping say any open address ( "www.google.com" for this example ).
* We initialize the Wifi and Ethernet services on ESP32. ( Remember, both works on DHCP! I will add static options in other examples )
* ESP32 tries to connect with the provided SSID & Password. If radio conenction is not established, it will try reconnecting...
* Meanwhile if Ethernet Cable is connected, ESP32 will start the ping process to know whether it is receiving the Internet services.
* You can set various ping parameters like : ping intervals, ping counts, ping host, ping loss tolerance and so on.

## Conclusion:
* So far, I believe ping is the only method to know whether we are having active Internet Services
* **VERY IMPORTANT: ESP gives WIFI connection prioirty over Ethernet by default ( at least in my test cases ). Hence, if your ESP is connected to an AP but isn't receiving any Internet Packages, it won't shift to Ethernet!!**
