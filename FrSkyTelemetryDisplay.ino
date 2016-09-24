/*
 * FrSky Telemetry Display for Arduino
 *
 * Main Logic
 * Copyright 2016 by Thomas Buck <xythobuz@xythobuz.de>
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <xythobuz@xythobuz.de> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return.   Thomas Buck
 * ----------------------------------------------------------------------------
 */
#include "i2c.h"
#include "oled.h"
#include "frsky.h"
#include "logo.h"

// Hardware configuration
#define LED_OUTPUT 13
#define BEEPER_OUTPUT 4
#define BAUDRATE 9600

// How to calculate battery voltage from analog value (a1 or a2)
// Voltage is stored with factor 100, so 430 -> 4.30V
#define BATTERY_ANALOG a2
#define BATTERY_VALUE_MIN 207
#define BATTERY_VALUE_MAX 254
#define BATTERY_VOLTAGE_MIN 350
#define BATTERY_VOLTAGE_MAX 430

// How often to refresh display (in ms)
#define DISPLAY_MAX_UPDATE_RATE 100

// How long until logo is shown when no data is received (in ms)
#define DISPLAY_REVERT_LOGO_TIME 2500

// When to sound the voltage alarm. Don't warn below MIN_WARN_LEVEL.
#define BATTERY_MIN_WARN_LEVEL 100
#define BATTERY_LOW_WARN_LEVEL 325
#define BATTERY_HIGH_WARN_LEVEL 315

// Time between beeps for different alarm levels (in ms)
#define BATTERY_LOW_WARN_DELAY 400
#define BATTERY_HIGH_WARN_DELAY 200

FrSky frsky(&Serial);
uint8_t showingLogo = 0;
uint8_t ledState = 0;
uint8_t redrawScreen = 0;
int16_t rssiRx = 0;
int16_t rssiTx = 0;
int16_t voltageBattery = 0;
uint8_t analog1 = 0;
uint8_t analog2 = 0;
String userDataString = "";
uint16_t beeperDelay = 0;
uint8_t beeperState = 0;
unsigned long beeperTime = 0;

void setBeeper(uint16_t timing) {
    beeperDelay = timing;
    if (timing == 0) {
        beeperState = 0;
        digitalWrite(BEEPER_OUTPUT, LOW);
    }
}

void writeLine(int l, String s) {
    setXY(l, 0);
    sendStr(s.c_str());

    // Fill rest of line with whitespace so there are no wrong stale characters
    String whitespace;
    for (int i = s.length(); i < 16; i++) {
        whitespace += ' ';
    }
    sendStr(whitespace.c_str());
}

// Print battery voltage with dot (-402 -> -4.02V)
String voltageToString(int16_t voltage) {
    String volt = String(abs(voltage) / 100);
    String fract = String(abs(voltage) % 100);
    String s;
    if (voltage < 0) {
        s += '-';
    }
    s += volt + ".";
    for (int i = 0; i < (2 - fract.length()); i++) {
        s += '0';
    }
    s += fract + "V";
    return s;
}

void drawInfoScreen(void) {
    writeLine(0, "FrSky Telemetry");
    writeLine(1, "Version: 1.0.1");
    writeLine(2, "by xythobuz");
    writeLine(3, "Warning Volt:");
    writeLine(4, voltageToString(BATTERY_LOW_WARN_LEVEL));
    writeLine(5, "Alarm Volt:");
    writeLine(6, voltageToString(BATTERY_HIGH_WARN_LEVEL));
}

void dataHandler(uint8_t a1, uint8_t a2, uint8_t q1, uint8_t q2) {
    ledState ^= 1;
    digitalWrite(LED_OUTPUT, ledState ? HIGH : LOW);
    redrawScreen = 1;

    rssiRx = map(q1, 0, 255, 0, 100);
    rssiTx = map(q2, 0, 255, 0, 100);
    voltageBattery = map(BATTERY_ANALOG, BATTERY_VALUE_MIN, BATTERY_VALUE_MAX,
            BATTERY_VOLTAGE_MIN, BATTERY_VOLTAGE_MAX);
    analog1 = a1;
    analog2 = a2;
}

void alarmThresholdHandler(FrSky::AlarmThreshold alarm) {
    ledState ^= 1;
    digitalWrite(LED_OUTPUT, ledState ? HIGH : LOW);
}

void userDataHandler(const uint8_t* buf, uint8_t len) {
    ledState ^= 1;
    digitalWrite(LED_OUTPUT, ledState ? HIGH : LOW);
    redrawScreen = 1;

    String s;
    for (int i = 0; i < len; i++) {
        s += buf[i];
    }
    userDataString = s;
}

void setup(void) {
    delay(200);

    pinMode(BEEPER_OUTPUT, OUTPUT);
    pinMode(LED_OUTPUT, OUTPUT);
    digitalWrite(LED_OUTPUT, HIGH);
    digitalWrite(BEEPER_OUTPUT, HIGH);

    Serial.begin(BAUDRATE);
    i2c_init();
    i2c_OLED_init();

    clear_display();
    delay(50);
    i2c_OLED_send_cmd(0x20);
    i2c_OLED_send_cmd(0x02);
    i2c_OLED_send_cmd(0xA6);

    drawInfoScreen();
    digitalWrite(BEEPER_OUTPUT, LOW);
    delay(DISPLAY_REVERT_LOGO_TIME);

    drawLogo(bootLogo);
    showingLogo = 1;

    frsky.setDataHandler(&dataHandler);
    frsky.setAlarmThresholdHandler(&alarmThresholdHandler);
    frsky.setUserDataHandler(&userDataHandler);

    digitalWrite(BEEPER_OUTPUT, HIGH);
    delay(100);
    digitalWrite(BEEPER_OUTPUT, LOW);
    digitalWrite(LED_OUTPUT, LOW);
}

void loop(void) {
    frsky.poll();

    if ((beeperDelay > 0) && ((millis() - beeperTime) > beeperDelay)) {
        beeperState ^= 1;
        digitalWrite(BEEPER_OUTPUT, beeperState ? HIGH : LOW);
    } else if (beeperDelay == 0) {
        digitalWrite(BEEPER_OUTPUT, LOW);
    }

    static unsigned long lastTime = 0;
    if (redrawScreen && ((millis() - lastTime) > DISPLAY_MAX_UPDATE_RATE)) {
        redrawScreen = 0;
        lastTime = millis();

        // Clear the display removing the logo on first received packet
        if (showingLogo) {
            showingLogo = 0;
            clear_display();
        }

        writeLine(0, "RSSI Rx: " + String(rssiRx) + "%");
        writeLine(1, "RSSI Tx: " + String(rssiTx) + "%");
        writeLine(2, "A1 Value: " + String(analog1));
        writeLine(3, "A2 Value: " + String(analog2));
        writeLine(4, "User Data:");
        writeLine(5, userDataString);
        writeLine(6, "Battery Volt:");
        writeLine(7, voltageToString(voltageBattery));

        // Enable battery alarm beeper as required
        if (voltageBattery > BATTERY_MIN_WARN_LEVEL) {
            if (voltageBattery > BATTERY_LOW_WARN_LEVEL) {
                setBeeper(0);
            } else if (voltageBattery > BATTERY_HIGH_WARN_LEVEL) {
                setBeeper(BATTERY_LOW_WARN_DELAY);
            } else {
                setBeeper(BATTERY_HIGH_WARN_DELAY);
            }
        } else {
            setBeeper(0);
        }
    } else if ((!redrawScreen) && ((millis() - lastTime) > DISPLAY_REVERT_LOGO_TIME) && (!showingLogo)) {
        // Show the logo again if nothing has been received for a while
        drawLogo(bootLogo);
        showingLogo = 1;
        setBeeper(0);
    }
}
