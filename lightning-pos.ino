// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

/**
 *  Flux Capacitor PoS Terminal - a point of sale terminal which can
 *  accept bitcoin via lightning network
 *
 *  Epaper PIN MAP: [
 *      VCC - 3.3V
 *      GND - GND
 *      SDI - GPIO23
 *     SCLK - GPIO18,
 *       CS - GPIO5
 *      D/C - GPIO17
 *    Reset - GPIO16
 *     Busy - GPIO4
 *  ]
 *
 *  Keypad Matrix PIN MAP: [
 *     Pin8 - GPIO13
 *     .............
 *     Pin1 - GPIO32
 *  ]
 *
 *  LED PIN MAP: [
 *    POS (long leg)  - GPIO15
 *    NEG (short leg) - GND
 *  ]
 *
 */


#include <WiFiClientSecure.h>

#include <ArduinoJson.h> //Use version 5.3.0!
#include <GxEPD2_BW.h>
#include <qrcode.h>
#include <string.h>

#include <Keypad.h>

#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

struct wifi_conf_t {
    String ssid;
    String pass;
};

struct preset_t {
    String title;
    float price;
};

#include "config.h"

GxEPD2_BW<GxEPD2_154, GxEPD2_154::HEIGHT> display(GxEPD2_154(/*CS=5*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));

// OpenNode config
const char* host = "api.opennode.co";
const int httpsPort = 443;
String hints = "true";

// fiat/btc price
String price;
unsigned long price_tstamp = 0;

struct payreq_t {
    String id;
    String invoice;
};

//Set other Arduino Strings used
String setoffour = "";
String qrline = "";
String hexvalues = "";
String result = "";

//Set keypad
const byte rows = 4;
const byte cols = 4;
char keys[rows][cols] = {
                         {'1','2','3','A'},
                         {'4','5','6','B'},
                         {'7','8','9','C'},
                         {'*','0','#','D'}
};
byte rowPins[rows] = {13, 12, 14, 27};
byte colPins[cols] = {26, 25, 33, 32};
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, rows, cols );
char keybuf[20];

unsigned long sats;
int preset = -1;

void displayText(int col, int row, String txt) {
    display.firstPage();
    do
    {
        display.setRotation(1);
        display.setPartialWindow(0, 0, 200, 200);
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(col, row);
        display.println(txt);
    }
    while (display.nextPage());
}

void displayMenu() {
    Serial.printf("displayMenu\n");
    display.firstPage();
    do
    {
        display.setRotation(1);
        display.setPartialWindow(0, 0, 200, 200);
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSansBold18pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 40);
        display.println(" Pay with");
        display.println(" Lightning");
        display.setFont(&FreeSansBold9pt7b);
        for (int ndx = 0; ndx < 4; ++ndx) {
            char but = 'A' + ndx;
            display.printf("  %c - %s\n", but, cfg_presets[ndx].title.c_str());
        }
    }
    while (display.nextPage());
}

void setup() {
    display.init(115200);

    displayText(20, 100, "Loading ...");

    Serial.begin(115200);

    int nconfs = sizeof(cfg_wifi_confs) / sizeof(wifi_conf_t);
    Serial.printf("scanning %d wifi confs\n", nconfs);
    int ndx = 0;
    while (true) {
        const char* ssid = cfg_wifi_confs[ndx].ssid.c_str();
        const char* pass = cfg_wifi_confs[ndx].pass.c_str();

        Serial.printf("trying %s\n", ssid);
        displayText(10, 100, String("Trying ") + ssid);

        WiFi.begin(ssid, pass);

        // Poll the status for a while.
        for (int nn = 0; nn < 20; ++nn) {
            if (WiFi.status() == WL_CONNECTED)
                goto Connected;
            delay(100);
        }

        // Try the next access point.
        if (++ndx == nconfs) {
            ndx = 0;
        }
    }

 Connected:
    Serial.println("connected");

    pinMode(19, OUTPUT);

    check_price();
}

void loop() {
    preset = -1;
    displayMenu();
    while (preset == -1) {
        char key = keypad.getKey();
        switch (key) {
        case NO_KEY:
            break;
        case 'A':
        case 'B':
        case 'C':
        case 'D':
            preset = key - 'A';
            break;
        default:
            break;
        }
    }

    memset(keybuf, 0, sizeof(keybuf));
    int counta = 0;

    hexvalues = "";

    // If no amount is selected, return to main menu.
    if (!keypadamount()) {
        return;
    }
    
    Serial.printf("pay %d %lu\n", preset, sats);

    payreq_t payreq = fetchpayment(sats);
    if (payreq.id == "") {
        return;
    }

    if (!displayQR(&payreq)) {
        return;
    }

    bool ispaid = checkpayment(payreq.id);
    while (counta < 40) {
        if (!ispaid) {
            // Delay, checking for abort.
            for (int nn = 0; nn < 200; ++nn) {
                if (keypad.getKey() == '*') {
                    return;
                }
                delay(10);
            }
            ispaid = checkpayment(payreq.id);
            counta++;
        }
        else
        {
            // Display big success message.
            display.firstPage();
            do
            {
                display.setRotation(1);
                display.setPartialWindow(0, 0, 200, 200);
                display.fillScreen(GxEPD_WHITE);
                display.setFont(&FreeSansBold18pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(0, 80);
                display.println(" Success!");
                display.println("Thank you!");
            }
            while (display.nextPage());
            
            digitalWrite(19, HIGH);
            delay(8000);
            digitalWrite(19, LOW);
            delay(500);
            counta = 40;
        }
    }
    counta = 0;
}

// QR maker function
void qrmmaker(String xxx){
    int str_len = xxx.length() + 1;
    char xxxx[str_len];
    xxx.toCharArray(xxxx, str_len);

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(11)];
    qrcode_initText(&qrcode, qrcodeData, 11, 0, xxxx);

    int une = 0;

    qrline = "";

    for (uint8_t y = 0; y < qrcode.size; y++) {

        // Each horizontal module
        for (uint8_t x = 0; x < qrcode.size; x++) {
            qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        qrline += "1";
        for (uint8_t x = 0; x < qrcode.size; x++) {
            qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        qrline += "1";
        for (uint8_t x = 0; x < qrcode.size; x++) {
            qrline += (qrcode_getModule(&qrcode, x, y) ? "111": "000");
        }
        qrline += "1";
    }
}

int applyPreset() {
    String centstr = String(long(cfg_presets[preset].price * 100));
    memset(keybuf, 0, sizeof(keybuf));
    memcpy(keybuf, centstr.c_str(), centstr.length());
    Serial.printf("applyPreset %d keybuf=%s\n", preset, keybuf);
    displayAmountPage();
    showPartialUpdate(keybuf);
    return centstr.length();
}

//Function for keypad
unsigned long keypadamount() {
    // Refresh the exchange rate.
    check_price();
    applyPreset();
    int checker = 0;
    while (checker < sizeof(keybuf)) {
        char key = keypad.getKey();
        switch (key) {
        case NO_KEY:
            break;
        case '#':
            displayText(20, 100, "Processing ...");
            return true;
        case '*':
            if (cfg_presets[preset].price != 0.00) {
                // Pressing '*' with preset value returns to main menu.
                return false;
            } else {
                // Otherwise clear value and stay on screen.
                checker = applyPreset();
                break;
            }
        case 'A':
        case 'B':
        case 'C':
        case 'D':
            preset = key - 'A';
            checker = applyPreset();
            break;
        default:
            keybuf[checker] = key;
            checker++;
            Serial.printf("keybuf=%s\n", keybuf);
            showPartialUpdate(keybuf);
            break;
        }
    }
    // Only get here when we overflow the keybuf.
    return false;
}

void displayAmountPage() {
    display.firstPage();
    do
    {
        display.setRotation(1);
        display.setPartialWindow(0, 0, 200, 200);
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);

        display.setCursor(0, 20);
        display.println(" " + cfg_presets[preset].title);

        display.setCursor(0, 60);
        if (cfg_presets[preset].price == 0.00) {
            display.println(" Enter Amount");
        } else {
            display.println();
        }
        display.println(" " + cfg_currency.substring(3) + ": ");
        display.println(" Sats: ");

        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(0, 160);
        if (cfg_presets[preset].price == 0.00) {
            display.println("   Press * to clear");
        } else {
            display.println("   Press * to cancel");
        }
        display.println("   Press # when done");

    }
    while (display.nextPage());
}

// Display current amount
void showPartialUpdate(String centsStr) {

    float rate = price.toFloat();

    float fiat = centsStr.toFloat() / 100.0;
    sats = long(fiat * 100e6 / rate);

    display.firstPage();
    do
    {
        display.setRotation(1);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);

        // display.fillRect(box_x, box_y, box_w, box_h, GxEPD_WHITE);
        display.setPartialWindow(70, 69, 120, 20);
        display.setCursor(70, 89);
        display.print(fiat);

    }
    while (display.nextPage());

    display.firstPage();
    do
    {
        display.setRotation(1);
        display.setFont(&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);

        // display.fillRect(box_x, box_y, box_w, box_h, GxEPD_WHITE);
        display.setPartialWindow(70, 98, 120, 20);
        display.setCursor(70, 118);
        display.print(sats);

    }
    while (display.nextPage());
}
    
//Char for holding the QR byte array
unsigned char PROGMEM singlehex[4209];

// Display QRcode
bool displayQR(payreq_t * payreqp) {
    
    //Char dictionary for conversion from 1s and 0s
    const char ref[2][16][5]={
        {
         "0000","0001","0010","0011","0100","0101","0110","0111",
         "1000","1001","1010","1011","1100","1101","1110","1111"
        },
        {
         "0","1","2","3","4","5","6","7","8","9","a","b","c","d","e","f"
        }
    };

    qrmmaker(payreqp->invoice);

    for (int i = 0;  i < qrline.length(); i+=4) {
        int tmp = i;
        setoffour = qrline.substring(tmp, tmp+4);

        for (int z = 0; z < 16; z++){
            if (setoffour == ref[0][z]){
                hexvalues += ref[1][z];
            }
        }
    }

    qrline = "";

    //for loop to build the epaper friendly char singlehex byte array
    //image of the QR
    for (int i = 0;  i < 4209; i++) {
        int tmp = i;
        int pmt = tmp*2;
        result = "0x" + hexvalues.substring(pmt, pmt+2) + ",";
        singlehex[tmp] =
            (unsigned char)strtol(hexvalues.substring(pmt, pmt+2).c_str(),
                                  NULL, 16);
    }

    display.firstPage();
    do
    {
        display.setPartialWindow(0, 0, 200, 200);
        display.fillScreen(GxEPD_WHITE);
        display.drawBitmap( 7, 7, singlehex, 184, 183, GxEPD_BLACK);

    }
    while (display.nextPage());

    return true;
}

///////////////////////////// GET/POST REQUESTS///////////////////////////

void check_price() {
    // Only check the price if it is older than 10 minutes.
    unsigned long now = millis();
    if (price_tstamp == 0 ||	/* first time */
        now < price_tstamp ||	/* wraps after 50 days */
        now - price_tstamp > (10 * 60 * 1000) /* 10 min old */) {

        Serial.printf("updating %s price\n", cfg_currency.c_str());
        displayText(10, 100, "Updating " + cfg_currency + " ...");

        WiFiClientSecure client;

        if (!client.connect(host, httpsPort)) {
            return;
        }

        String url = "/v1/rates";

        client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "User-Agent: ESP32\r\n" +
                     "Connection: close\r\n\r\n");

        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") {
                break;
            }
        }
        String line = client.readStringUntil('\n');

        const size_t capacity =
            169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
        DynamicJsonDocument doc(capacity);

        deserializeJson(doc, line);

        String temp = doc["data"][cfg_currency][cfg_currency.substring(3)];
        price = temp;
        price_tstamp = now;
        Serial.printf("1 BTC = %s %s\n",
                      price.c_str(), cfg_currency.substring(3).c_str());
    }
}

payreq_t fetchpayment(unsigned long sats){
    WiFiClientSecure client;

    Serial.printf("fetchpayment %lu\n", sats);
    
    if (!client.connect(host, httpsPort)) {
        Serial.printf("fetchpayment connect failed\n");
        return { "", "" };
    }

    String SATSAMOUNT = String(sats);
    String topost =
        "{  \"amount\": \"" + SATSAMOUNT + "\", \"description\": \"" +
        cfg_prefix + cfg_presets[preset].title + "\", \"route_hints\": \"" +
        hints + "\"}";
    String url = "/v1/charges";

    client.print(String("POST ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Authorization: " + cfg_apikey + "\r\n" +
                 "Content-Type: application/json\r\n" +
                 "Connection: close\r\n" +
                 "Content-Length: " + topost.length() + "\r\n" +
                 "\r\n" +
                 topost + "\n");

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }
    String line = client.readStringUntil('\n');

    const size_t capacity =
        169*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(168) + 3800;
    DynamicJsonDocument doc(capacity);

    deserializeJson(doc, line);
    String id = doc["data"]["id"];
    String payreq = doc["data"]["lightning_invoice"]["payreq"];
    
    Serial.printf("fetchpayment -> %d %s\n", id, payreq.c_str());
    return { id, payreq };
}

// Check the status of the payment, return true if it has been paid.
bool checkpayment(String PAYID){

    WiFiClientSecure client;

    Serial.printf("checkpayment %s\n", PAYID.c_str());
    
    if (!client.connect(host, httpsPort)) {
        Serial.printf("checkpayment connect failed\n");
        return false;
    }

    String url = "/v1/charge/" + PAYID;

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Authorization: " + cfg_apikey + "\r\n" +
                 "User-Agent: ESP32\r\n" +
                 "Connection: close\r\n\r\n");

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }
    String line = client.readStringUntil('\n');

    const size_t capacity =
        JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) +
        JSON_OBJECT_SIZE(14) + 650;
    DynamicJsonDocument doc(capacity);

    deserializeJson(doc, line);
    String stat = doc["data"]["status"];
    
    Serial.printf("checkpayment -> %s\n", stat.c_str());
    return stat == "paid";
}
