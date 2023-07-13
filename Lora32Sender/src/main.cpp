#include "WiFiClientSecure.h"
#include <LoRa.h>
#include "boards.h"
#include <NostrEvent.h>

int counter = 0;

NostrEvent nostr;

char const *nsecHex = "bdd19cecd942ed8964c2e0ddc92d5e09838d3a09ebb230d974868be00886704b";
char const *npubHex = "d0bfc94bd4324f7df2a7601c4177209828047c4d3904d64009a3c67fb5d5e7ca";

long timestamp = 0;

void setup()
{
    initBoard();
    // When the power is turned on, a delay is required.
    delay(1500);

    Serial.println("LoRa Sender");
    LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);
    if (!LoRa.begin(LoRa_frequency)) {
        Serial.println("Starting LoRa failed!");
        while (1);
    }
}

void listenForTimestampOnLora()
{
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        Serial.print("Received packet '");
        
        String recv = "";
        while (LoRa.available()) {
            recv += (char)LoRa.read();
        }

        Serial.println(recv + "'");
        // if the string contains "TIMESTAMP|" it means it's a timestamp
        if(recv.indexOf("TIMESTAMP|") != -1) {

            // Split the recv string by pipe. array[1] is the timestamp
            char *str = strdup(recv.c_str());
            char *token = strtok(str, "|");
            int i = 0;
            while (token != NULL) {
                if (i == 1) {
                    timestamp = atol(token);
                }
                token = strtok(NULL, "|");
                i++;
            }


            Serial.println("Timestamp is" + String(timestamp));
        }
        Serial.println("Not a timestamp ");
    }
}

void loop()
{
    listenForTimestampOnLora();

    if(timestamp > 0) {
        Serial.print("Sending packet: ");
        Serial.println(counter);
        String noteContent = "nostr note constructed on LoRa sender. This is counter number " + String(counter);
        String note = nostr.getNote(nsecHex, npubHex, timestamp, noteContent.c_str());
        Serial.println("Nostr is sending: " + note);

        // // send packet
        // LoRa.beginPacket();
        // LoRa.print(note);
        // LoRa.endPacket();

        String longMessage = note; // your long message
        int maxPacketSize = 100; // set a maximum packet size (leave some room for headers etc.)
        String endOfPacketMarker = "<EOP>"; // end of packet marker

        int msgLen = longMessage.length();
        for (int i = 0; i < msgLen; i += maxPacketSize) {
            String chunk = longMessage.substring(i, min(i + maxPacketSize, msgLen));
            
            LoRa.beginPacket();
            LoRa.print(chunk);
            LoRa.print(endOfPacketMarker); // append the end of packet marker
            Serial.println("Sending chunk: " + chunk);
            Serial.println("Sending end of packet marker: " + endOfPacketMarker);
            LoRa.endPacket();
            
            delay(10); // give the receiver a short break to process the packet
        }

    #ifdef HAS_DISPLAY
        if (u8g2) {
            char buf[256];
            u8g2->clearBuffer();
            u8g2->drawStr(0, 12, "Transmitting: OK!");
            snprintf(buf, sizeof(buf), "Sending: %d", counter);
            u8g2->drawStr(0, 30, buf);
            u8g2->sendBuffer();
        }
    #endif
        counter++;
        delay(60000);
    }
}
