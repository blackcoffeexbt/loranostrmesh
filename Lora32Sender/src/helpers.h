#pragma once


/**
 * @brief Display text on the OLED
 * 
 * @param message 
 * @param x pos - optional
 * @param y pos - optional
 */
void oledDisplay(String message, int x = 5, int y = 5, bool clearScreen = true) {
#ifdef HAS_DISPLAY
    if (u8g2) {
        char buf[256];
        if (clearScreen) {
            u8g2->clearBuffer();
        }
        snprintf(buf, sizeof(buf), message.c_str());
        u8g2->drawStr(x, y, buf);
        u8g2->sendBuffer();
    }
#endif
}