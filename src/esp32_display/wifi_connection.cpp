#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "mta_cert.h"
#include "secrets.h"

const char* MTA_HOST = "api-endpoint.mta.info";
const char* MTA_PATH = "/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace";
const int HTTPS_PORT = 443;

void connectWifi();
void syncClock();
void testMtaFeed();

void setup(){
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Booting...");
  connectWifi();
  if (WiFi.status() == WL_CONNECTED)
  {
    syncClock();
    testMtaFeed();
  }
}

void loop() {}

void connectWifi() {
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

void syncClock() {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    Serial.print("Waiting for NTP time sync");
    time_t now = time(nullptr);
    int tries = 30;
    while (now < 8 * 3600 * 2 && tries-- > 0)
    {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }

    Serial.println();
    if (now < 8 * 3600 * 2)
    {
        Serial.println("NTP sync failed; HTTPS certificate validation may fail");
        return;
    }

    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current UTC time: ");
    Serial.print(asctime(&timeinfo));
}

void testMtaFeed() {
    WiFiClientSecure client;
    client.setCACert(MTA_ROOT_CA);

    Serial.print("Connecting to ");
    Serial.println(MTA_HOST);

    if (!client.connect(MTA_HOST, HTTPS_PORT))
    {
        Serial.println("MTA feed connection failed");
        return;
    }

    Serial.print("Requesting ");
    Serial.println(MTA_PATH);

    client.print(String("GET ") + MTA_PATH + " HTTP/1.1\r\n" +
                 "Host: " + MTA_HOST + "\r\n" +
                 "User-Agent: nyc-train-time-esp32\r\n" +
                 "Accept: application/x-google-protobuf\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long start = millis();
    while (!client.available() && millis() - start < 10000)
    {
        delay(100);
    }

    if (!client.available())
    {
        Serial.println("Timed out waiting for MTA response");
        client.stop();
        return;
    }

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    Serial.print("MTA response: ");
    Serial.println(statusLine);

    if (statusLine.startsWith("HTTP/1.1 200") || statusLine.startsWith("HTTP/1.0 200"))
    {
        Serial.println("MTA feed smoke test passed");
    }
    else
    {
        Serial.println("MTA feed smoke test failed");
    }

    client.stop();
}
