#include <vector>
#include <string>
#include <Base64.hpp>

#define LORA_MAX_PACKET_SIZE 100

void split_string_into_parts(std::string* source, size_t part_length, std::vector<std::string>* result) {
    // Make sure source and result are not null
    if(source == nullptr || result == nullptr)
        return;
  
    // Make sure part_length is not zero
    if(part_length == 0)
        return;

    for (size_t i = 0; i < source->length(); i += part_length) {
        // substr function automatically takes care of the case when (i+part_length > length)
        result->push_back(source->substr(i, part_length));
    }
}

/**
 * @brief Encode a string to base64
 * 
 * @param message 
 * @return String 
 */
String base64Encode(String message) {
  unsigned char input[message.length() + 1];
  strncpy((char *)input, message.c_str(), sizeof(input));
  unsigned char encoded[2 * message.length()]; // Rough estimate should be safe for most cases
  
  unsigned int encoded_length = encode_base64(input, strlen((char *)input), encoded);
  encoded[encoded_length] = '\0'; // Add null-terminator
  
  return String((char *)encoded);
}

/**
 * @brief Decode a base64 encoded string
 * 
 * @param encodedMsg 
 * @return String 
 */
String base64Decode(String encodedMsg) {
  unsigned char input[encodedMsg.length() + 1];
  strncpy((char *)input, encodedMsg.c_str(), sizeof(input));
  unsigned char decoded[2 * encodedMsg.length()]; // Rough estimate should be safe for most cases
  
  unsigned int decoded_length = decode_base64(input, decoded);
  decoded[decoded_length] = '\0'; // Add null-terminator
  
  return String((char *)decoded);
}

const int MAX_RETRIES = 5;
const int RETRY_TIME = 3000; // 10 seconds

/**
 * @brief Broadcast a Nostr event over LoRa
 * 
 * @param serialisedEvent A serialised Nostr event JSON
 * @param callback A callback function to call when the ACK is received
 */
void broadcastNostrEvent(String* serialisedEvent, void (*callback)(DynamicJsonDocument*)) {

    Serial.println("Broadcasting Nostr event");

    std::string base64EncodedEvent = base64Encode(*serialisedEvent).c_str();
    
    // split the message into parts
    std::vector<std::string> messageParts;
    split_string_into_parts(&base64EncodedEvent, LORA_MAX_PACKET_SIZE, &messageParts);

    uint8_t totalParts = messageParts.size();
    // for each element of the messageParts
    for (uint8_t i = 0; i < totalParts; i++) {
        // take a subtring of length -5 of the current messagePart as a checksum
        std::string checksum = messageParts.at(i).substr(messageParts.at(i).length() - 5, messageParts.at(i).length());
        // create a DynamicJson document with items: numParts, currentPart, messagePart, checksum
        Serial.println("Part " + String(i) + " of " + String(totalParts) + " with checksum " + checksum.c_str());
        DynamicJsonDocument doc(222);
        doc["totalParts"] = totalParts;
        doc["currentPart"] = i + 1; // 1 indexed
        doc["messagePart"] = messageParts.at(i);
        doc["checksum"] = checksum;
        // serialise and base64 encode it
        String serialisedDoc = "";
        serializeJson(doc, serialisedDoc);
        String encodedDoc = base64Encode(serialisedDoc);
        doc.clear();

        // Initialize retry counter
        int retryCount = 0;

        // Loop until ACK is received or maximum retries reached
        while (retryCount < MAX_RETRIES) {
            Serial.println("Broadcast number " + String(retryCount) + " of " + String(MAX_RETRIES) + "");

            // transmit the encodedDoc
            LoRa.beginPacket();
            Serial.println("Sending packet " + encodedDoc);
            LoRa.print(encodedDoc);
            LoRa.endPacket();

            // Reset start time for each retry
            unsigned long startTime = millis();
            unsigned long currentTime = millis();

            // Loop for 10 seconds or until ACK is received
            while (currentTime - startTime < RETRY_TIME) {
                // Wait for a response
                int packetSize = LoRa.parsePacket();
                if (packetSize) {
                    // Received a packet
                    String recv = "";
                    // Read packet
                    while (LoRa.available()) {
                        recv += (char)LoRa.read();
                    }
                    // Decode the packet
                    String decodedRecv = base64Decode(recv);
                    Serial.println("Received packet " + decodedRecv);
                    // Deserialize the packet
                    DynamicJsonDocument recvDoc(222);
                    deserializeJson(recvDoc, decodedRecv);
                    // Check if the checksum matches
                    if (recvDoc["type"] == "ACK" && recvDoc["checksum"] == checksum) {
                        // run the callback function, doc as argument
                        callback(&recvDoc);
                        // If it does, break out of the while
                        retryCount = MAX_RETRIES; // Break out of the outer retry loop
                        break; // Break out of the inner while
                    }
                }
                
                currentTime = millis();
            }
            
            // Increase the retry count
            retryCount++;

            // If ACK is received, no need to retry
            if (retryCount == MAX_RETRIES) {
                break;
            }
        }
    }
}

/**
 * @brief Decode the LoRa package
 * 
 * @param loraPackage 
 * @param doc 
 */
void decodeLoraPackage(String* base64EncodedPackage, DynamicJsonDocument* doc) {
    // decode the lora package
    String decodedLoraPackage = base64Decode(*base64EncodedPackage);
    // deserialize the lora package
    deserializeJson(*doc, decodedLoraPackage);
}

/**
 * @brief Encode the LoRa package for transmission
 * 
 * @param doc 
 * @param base64EncodedPackage 
 */
void encodeLoraPackage(DynamicJsonDocument* doc, String* base64EncodedPackage) {
    // serialise the doc
    String serialisedDoc = "";
    serializeJson(*doc, serialisedDoc);
    // encode the doc
    *base64EncodedPackage = base64Encode(serialisedDoc);
}