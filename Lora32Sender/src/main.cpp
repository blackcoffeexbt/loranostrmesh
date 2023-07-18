#include <LoRa.h>
#include "boards.h"
#include "BluetoothSerial.h"
#include "WiFiClientSecure.h"
#include <NostrEvent.h>

#include "loranostrmesh.h"

BluetoothSerial SerialBT;

NostrEvent nostr;

char const *nsecHex = "bdd19cecd942ed8964c2e0ddc92d5e09838d3a09ebb230d974868be00886704b";
char const *npubHex = "d0bfc94bd4324f7df2a7601c4177209828047c4d3904d64009a3c67fb5d5e7ca";

long timestamp = 0;

void logToSerialAndBT(String message) {
    Serial.println(message);
    SerialBT.println(message);
}

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

    LoRa.setTxPower(20); // 20 is max power!

    SerialBT.begin("loranostrmesh"); // Name of your Bluetooth Signal
    Serial.println("Bluetooth Device is Ready to Pair");

    if (SerialBT.available()) {
        logToSerialAndBT("Type something in your phone app...");
    }

}

void loraReceive() {
    // try to parse packet
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        // received a packet
        String recv = "";
        // read packet
        while (LoRa.available()) {
            recv += (char)LoRa.read();
        }
        // lora messages in this network are all base64 encoded json docs
        String decodedRecv = base64Decode(recv);
        Serial.println("Received packet " + decodedRecv);
        // deserialize the packet
        DynamicJsonDocument doc(222);

        decodeLoraPackage(&recv, &doc);

        // action depending on "type"
        if(doc["type"] == "TIME") {
            // set the time
            timestamp = doc["content"];
            // if timestamp is real (i.e. between two realisitic dates)
            if(timestamp > 1689673884 && timestamp < 4845347483) {
                logToSerialAndBT("Set timestamp to " + String(timestamp));
            }
        } else if(doc["type"] == "ACK") {
            // do nothing
            Serial.println("Received ACK");
        } else if(doc["type"] == "NOSTR_EVENT") {
            logToSerialAndBT("Received Nostr event");
            logToSerialAndBT("Content: " + doc["content"].as<String>());
        } else {
            Serial.println("Received unknown type " + doc["type"].as<String>());
        }

        delay(20);

        // print RSSI of packet
        logToSerialAndBT("with RSSI " + LoRa.packetRssi());
    }
}

uint8_t currentPart = 0;

String message = "";

void ackCallback(DynamicJsonDocument* doc) {
    logToSerialAndBT("Received ACK for part " + String(doc->as<JsonObject>()["currentPart"].as<uint8_t>()) + " of " + String(doc->as<JsonObject>()["totalParts"].as<uint8_t>()));
}

void handleBluetooth() {
    if (Serial.available()) {
        SerialBT.write(Serial.read());
    }
    if (SerialBT.available()) {
        // read bluetooth message to string and then send back to the user over bluetooth and output on Serial.
        message = SerialBT.readString();
        logToSerialAndBT("This is what you sent: " + message);
    }

    if(message == "") {
        return;
    }
    
        // Create the nostr note
    nostr.setLogging(false);
    String note = nostr.getNote(nsecHex, npubHex, timestamp, message);
    message = "";

    broadcastNostrEvent(&note, &ackCallback);

    logToSerialAndBT("Packet sent");
}


void loop()
{

    loraReceive();

    handleBluetooth();

    

#ifdef HAS_DISPLAY
    if (u8g2) {
        char buf[256];
        u8g2->clearBuffer();
        u8g2->drawStr(0, 12, "Transmitting: OK!");
        snprintf(buf, sizeof(buf), "Sending: %d", message);
        u8g2->drawStr(0, 30, buf);
        u8g2->sendBuffer();
    }
#endif
}
