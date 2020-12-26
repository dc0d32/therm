#include "disp.h"
#include "config.h"
#include "tasks.h"

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono24pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

// screen related
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

///////////////////////////////////////////////////////////////////////////////////////
// graphics

// 'flame', 16x16px
const unsigned char bmp_flame[] PROGMEM = {
    0x01, 0x00, 0x03, 0x80, 0x03, 0xc0, 0x03, 0xe0, 0x07, 0xf0, 0x07, 0xf0, 0x0f, 0xf8, 0x1f, 0x78,
    0x1f, 0x38, 0x1e, 0x38, 0x1e, 0x38, 0x1c, 0x38, 0x0e, 0x70, 0x0e, 0x60, 0x03, 0x40, 0x00, 0x00};

// 'fan', 16x16px
const unsigned char bmp_fan[] PROGMEM = {
    0x03, 0xe0, 0x07, 0xf0, 0x07, 0xf0, 0x07, 0xe0, 0x63, 0xc0, 0xf3, 0xce, 0xff, 0x7f, 0xfd, 0xbf,
    0xfd, 0xff, 0xfe, 0x7f, 0x73, 0xcf, 0x03, 0xc6, 0x07, 0xe0, 0x0f, 0xe0, 0x0f, 0xe0, 0x07, 0xc0};

// 'person', 16x16px
const unsigned char bmp_person[] PROGMEM = {
    0x00, 0x18, 0x00, 0x18, 0x07, 0xd8, 0x0c, 0xe0, 0x08, 0xf0, 0x01, 0xd0, 0x01, 0x9f, 0x03, 0x80,
    0x07, 0x00, 0x0f, 0xc0, 0xfc, 0xe0, 0x00, 0x60, 0x00, 0x40, 0x00, 0xc0, 0x00, 0x80, 0x00, 0x00};

// 'wifi', 16x16px
const unsigned char bmp_wifi[] PROGMEM = {
    0x00, 0x00, 0x01, 0x80, 0x1f, 0xf8, 0x7e, 0x7e, 0xf0, 0x0f, 0xc7, 0xe3, 0x1f, 0xf8, 0x38, 0x1c,
    0x11, 0x88, 0x07, 0xe0, 0x06, 0x60, 0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00};

// 'homeassistant', 16x16px
const unsigned char bmp_homeassistant[] PROGMEM = {
    0x00, 0x00, 0x01, 0x8c, 0x03, 0xcc, 0x07, 0xec, 0x0e, 0x7c, 0x1e, 0x7c, 0x3e, 0x7c, 0x7f, 0x7e,
    0xf3, 0x67, 0x6b, 0x57, 0x33, 0x4c, 0x3c, 0x3c, 0x3e, 0x7c, 0x3f, 0x7c, 0x3f, 0x7c, 0x00, 0x00};

// 'hand with button', 16x16px
const unsigned char bmp_local_mode[] PROGMEM = {
    0x30, 0x00, 0x78, 0x00, 0x98, 0x00, 0xb8, 0x30, 0xf4, 0xe8, 0x37, 0xa8, 0x0a, 0xd4, 0x09, 0x44, 
    0x05, 0x02, 0x04, 0x01, 0x02, 0x01, 0x02, 0x01, 0x1e, 0x06, 0x1c, 0x0c, 0x07, 0xb0, 0x00, 0xc0};

///////////////////////////////////////////////////////////////////////////////////////

void refresh_display()
{
    display.display();
}

void init_disp()
{
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    { // Address 0x3D for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ; // Don't proceed, loop forever
    }

    // no need to display the default adafruit logo, clear it.
    display.clearDisplay();
    display.display();
    display.dim(true);

    sched.add_or_update_task((void *)refresh_display, 0, NULL, 0, 40, 0); // about 25 FPS
}

void draw_current_temp()
{
    GFXcanvas1 canvas(54, 48);
    canvas.setTextColor(SSD1306_WHITE); // Draw white text

    // write heading
    canvas.setCursor(0, 0); // Start at top-left corner
    canvas.write("Inside:");

    // write value
    canvas.setTextSize(1);               // Normal 1:1 pixel scale
    canvas.setFont(&FreeMonoBold24pt7b); // set custom font
    canvas.setCursor(0, 42);             // this depends on the font

    String temp_str = "??";
    if (!isnan(therm_state.cur_temp))
    {
        int truncated_temp = therm_state.cur_temp;
        temp_str.clear();
        temp_str += truncated_temp % 100;

        // draw fraction bar
        // canvas.drawPixel(0, 47, WHITE);
        // canvas.drawPixel(canvas.width() - 1, 47, WHITE);
        canvas.drawRect(0, 46, (int)(canvas.width() * (therm_state.cur_temp - truncated_temp)), 2, WHITE);

        if (truncated_temp >= 100)
        {
            // for 3 digit temperatures
            canvas.drawRect(0, 20, 2, 16, WHITE);
        }
    }
    canvas.write(temp_str.c_str());

    display.drawBitmap(0, 16, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_target_temp()
{
    GFXcanvas1 canvas(32, 32);
    canvas.setTextColor(SSD1306_WHITE); // Draw white text

    // write heading
    canvas.setCursor(0, 0); // Start at top-left corner
    canvas.write("Set:");

    // write value
    canvas.setTextSize(1);               // Normal 1:1 pixel scale
    canvas.setFont(&FreeMonoBold12pt7b); // set custom font
    canvas.setCursor(0, 26);             // depends on the font

    String temp_str = "??";
    if (!isnan(therm_state.tgt_temp))
    {
        int truncated_temp = therm_state.tgt_temp;
        temp_str.clear();
        temp_str += truncated_temp % 100;

        // draw fraction bar
        // canvas.drawPixel(0, 47, WHITE);
        // canvas.drawPixel(canvas.width() - 1, 47, WHITE);
        canvas.drawRect(0, 30, (int)(canvas.width() * (therm_state.tgt_temp - truncated_temp)), 2, WHITE);

        if (truncated_temp >= 100)
        {
            // for 3 digit temperatures
            canvas.drawFastVLine(0, 12, 10, WHITE);
        }
    }
    canvas.write(temp_str.c_str());

    display.drawBitmap(56, 32, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_heat(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_flame, 16, 16, WHITE);
    display.drawBitmap(112, 0, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_fan(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_fan, 16, 16, WHITE);
    display.drawBitmap(112, 16, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_person(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_person, 16, 16, WHITE);
    display.drawBitmap(96, 0, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_wifi(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_wifi, 16, 16, WHITE);
    display.drawBitmap(80, 0, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_homeassistant(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_homeassistant, 16, 16, WHITE);
    display.drawBitmap(64, 0, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}

void draw_icon_local_mode(bool show)
{
    GFXcanvas1 canvas(16, 16);
    if (show)
        canvas.drawBitmap(0, 0, bmp_local_mode, 16, 16, WHITE);
    display.drawBitmap(64, 16, canvas.getBuffer(), canvas.width(), canvas.height(), WHITE, BLACK);
}
