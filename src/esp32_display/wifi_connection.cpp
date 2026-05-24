#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
//#include "mta_cert.h"
#include "secrets.h"

//WiFiClientSecure client;

void setup(){
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Booting...");
  connectWifi();
}

void loop() {}

void connectWifi() {
    //client.setCACert(MTA_ROOT_CA);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);

    int tries = 60;
    while (WiFi.status() != WL_CONNECTED && tries-- > 0)
    {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println();
        Serial.println("WiFi connect timeout");
    }
}

