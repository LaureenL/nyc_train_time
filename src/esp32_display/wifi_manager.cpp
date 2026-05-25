#include <Arduino.h>
#include <WiFi.h>

#include "wifi_manager.h"

void connectWifi(const char *ssid, const char *password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to ");
    Serial.println(ssid);

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

bool isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}
