#include <vector>
#include <string>
#include <Base64.hpp>

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

/**
 * @brief Send the message to the lora receiver
 * 
 * @param messageParts 
 */
void broadcastMessage(std::vector<std::string>* messageParts) {
    uint8_t numParts = messageParts->size();
    uint8_t currentBroadcastAttempt = 0;
    // for each element of the messageParts
    for (uint8_t i = 0; i < numParts; i++) {
        // take a subtring of length -5 of the current messagePart as a checksum
        std::string checksum = messageParts->at(i).substr(messageParts->at(i).length() - 5, messageParts->at(i).length());
        // create a DynamicJson document with items: numParts, currentPart, messagePart, checksum
        DynamicJsonDocument doc(222);
        doc["numParts"] = numParts;
        doc["currentPart"] = i;
        doc["messagePart"] = messageParts->at(i);
        doc["checksum"] = checksum;
        // serialise and base64 encode it
        String serialisedDoc = "";
        serializeJson(doc, serialisedDoc);
        String encodedDoc = base64Encode(serialisedDoc);
        // transmit it over lora and wait for a response, if elapsed time > 10 seconds and currentBroadcastAttempt < 3, try again
        uint32_t startTime = millis();
        uint32_t currentTime = millis();
        while (currentTime - startTime < 10000 && currentBroadcastAttempt < 3) {
            // transmit the encodedDoc
            LoRa.beginPacket();
            LoRa.print(encodedDoc);
            LoRa.endPacket();
            // wait for a response
            int packetSize = LoRa.parsePacket();
            if (packetSize) {
                // received a packet
                String recv = "";
                // read packet
                while (LoRa.available()) {
                    recv += (char)LoRa.read();
                }
                // decode the packet
                String decodedRecv = base64Decode(recv);
                // deserialise the packet
                DynamicJsonDocument recvDoc(222);
                deserializeJson(recvDoc, decodedRecv);
                // check if the checksum matches
                if (recvDoc["checksum"] == checksum) {
                    // if it does, break out of the while
                    break;
                }
            }
            // if it doesn't, increment currentBroadcastAttempt and currentTime
            currentBroadcastAttempt++;
            currentTime = millis();
        }
    }
}