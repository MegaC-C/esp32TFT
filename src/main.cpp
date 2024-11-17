#include "in.h"
#include "out.h"
#include <AnimatedGIF.h>
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(); // Initialize TFT
AnimatedGIF gif;           // Initialize GIF decoder

#define DISPLAY_WIDTH  tft.width()
#define DISPLAY_HEIGHT tft.height()
#define BUFFER_SIZE    256 // Optimum is >= GIF width or integral division of width

#define BUTTON_PIN_30   35  // GPIO 35 für den 30-Sekunden-Timer
#define BUTTON_PIN_5MIN 0   // GPIO 0 für den 5-Minuten-Timer
#define START_TIME_30   30  // Startzeit für 30 Sekunden
#define START_TIME_5MIN 300 // Startzeit für 5 Minuten (300 Sekunden)
#define GIF_IMAGE       inOut

uint16_t usTemp[1][BUFFER_SIZE]; // Global to support DMA use
bool dmaBuf = 0;

int countdown                = 0;
bool timerRunning            = false;
unsigned long previousMillis = 0; // Zur Zeitmessung mit millis()

void showStartScreen();
void showEndScreen();
void updateTimerDisplay();
void GIFDraw(GIFDRAW *pDraw);
void animateGif(const uint8_t gifBuffer[], size_t gifSize);

void setup()
{
    Serial.begin(9600);

    // Initialisiere Display
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(3);
    pinMode(BUTTON_PIN_30, INPUT_PULLUP);
    pinMode(BUTTON_PIN_5MIN, INPUT_PULLUP);

    // Initialisiere gif
    gif.begin(BIG_ENDIAN_PIXELS);

    // Begrüßungsanzeige
    animateGif(out, sizeof(out));
    showStartScreen();
}

void loop()
{
    // Prüfe, ob der Button für den 30-Sekunden-Timer gedrückt wurde
    if (digitalRead(BUTTON_PIN_30) == LOW)
    {
        delay(50); // Entprellung
        if (digitalRead(BUTTON_PIN_30) == LOW)
        {
            countdown      = START_TIME_30;
            timerRunning   = true;
            previousMillis = millis(); // Fix: Timer korrekt starten
        }
    }

    // Prüfe, ob der Button für den 5-Minuten-Timer gedrückt wurde
    if (digitalRead(BUTTON_PIN_5MIN) == LOW)
    {
        delay(50); // Entprellung
        if (digitalRead(BUTTON_PIN_5MIN) == LOW)
        {
            countdown      = START_TIME_5MIN;
            timerRunning   = true;
            previousMillis = millis(); // Fix: Timer korrekt starten
        }
    }

    // Aktualisiere den Timer, wenn er läuft
    if (timerRunning)
    {
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= 1000)
        { // Jede Sekunde
            previousMillis = currentMillis;

            if (countdown > 0)
            {
                countdown--;
                updateTimerDisplay();
            }
            else
            {
                // Timer beendet
                timerRunning = false;
                showEndScreen();
            }
        }
    }
}

void showStartScreen()
{
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_BLACK, TFT_RED);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("<- 5min", 0, 0, 4);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("<- 30s", 0, tft.height(), 4);
    tft.setTextDatum(MR_DATUM);
    tft.setTextSize(2);
    tft.drawString("Heizen!", tft.width() - 10, tft.height() / 2, 4);
}

void showEndScreen()
{
    animateGif(in, sizeof(in));

    tft.fillScreen(TFT_GREEN);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("<- 5min", 0, 0, 4);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("<- 30s", 0, tft.height(), 4);
    tft.setTextDatum(MR_DATUM);
    tft.setTextSize(2);
    tft.drawString("Vape!", tft.width() - 10, tft.height() / 2, 4);
}

void updateTimerDisplay()
{
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_BLACK, TFT_RED);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("<- 5min", 0, 0, 4);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("<- 30s", 0, tft.height(), 4);

    // Formatierte Zeit (MM:SS) anzeigen
    int minutes = countdown / 60;
    int seconds = countdown % 60;
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", minutes, seconds);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(timeStr, tft.width() - 10, tft.height() / 2, 7);
}

void animateGif(const uint8_t gifBuffer[], size_t gifSize)
{
    if (gif.open((uint8_t *)gifBuffer, gifSize, GIFDraw))
    {
        Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
        tft.startWrite(); // The TFT chip select is locked low
        while (gif.playFrame(true, NULL))
        {
            yield();
        }
        gif.close();
        tft.endWrite(); // Release TFT chip select for other SPI devices
    }
}

// Draw a line of image directly on the LCD
void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette;
    int x, y, iWidth, iCount;

    // Display bounds check and cropping
    iWidth = pDraw->iWidth;
    if (iWidth + pDraw->iX > DISPLAY_WIDTH)
        iWidth = DISPLAY_WIDTH - pDraw->iX;
    usPalette = pDraw->pPalette;
    y         = pDraw->iY + pDraw->y; // current line
    if (y >= DISPLAY_HEIGHT || pDraw->iX >= DISPLAY_WIDTH || iWidth < 1)
        return;

    // Old image disposal
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
        for (x = 0; x < iWidth; x++)
        {
            if (s[x] == pDraw->ucTransparent)
                s[x] = pDraw->ucBackground;
        }
        pDraw->ucHasTransparency = 0;
    }

    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
        uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
        pEnd   = s + iWidth;
        x      = 0;
        iCount = 0; // count non-transparent pixels
        while (x < iWidth)
        {
            c = ucTransparent - 1;
            d = &usTemp[0][0];
            while (c != ucTransparent && s < pEnd && iCount < BUFFER_SIZE)
            {
                c = *s++;
                if (c == ucTransparent) // done, stop
                {
                    s--; // back up to treat it like transparent
                }
                else // opaque
                {
                    *d++ = usPalette[c];
                    iCount++;
                }
            } // while looking for opaque pixels
            if (iCount) // any opaque pixels?
            {
                // DMA would degrtade performance here due to short line segments
                tft.setAddrWindow(pDraw->iX + x, y, iCount, 1);
                tft.pushPixels(usTemp, iCount);
                x += iCount;
                iCount = 0;
            }
            // no, look for a run of transparent pixels
            c = ucTransparent;
            while (c == ucTransparent && s < pEnd)
            {
                c = *s++;
                if (c == ucTransparent)
                    x++;
                else
                    s--;
            }
        }
    }
    else
    {
        s = pDraw->pPixels;

        // Unroll the first pass to boost DMA performance
        // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
        if (iWidth <= BUFFER_SIZE)
            for (iCount = 0; iCount < iWidth; iCount++)
                usTemp[dmaBuf][iCount] = usPalette[*s++];
        else
            for (iCount = 0; iCount < BUFFER_SIZE; iCount++)
                usTemp[dmaBuf][iCount] = usPalette[*s++];

#ifdef USE_DMA // 71.6 fps (ST7796 84.5 fps)
        tft.dmaWait();
        tft.setAddrWindow(pDraw->iX, y, iWidth, 1);
        tft.pushPixelsDMA(&usTemp[dmaBuf][0], iCount);
        dmaBuf = !dmaBuf;
#else // 57.0 fps
        tft.setAddrWindow(pDraw->iX, y, iWidth, 1);
        tft.pushPixels(&usTemp[0][0], iCount);
#endif

        iWidth -= iCount;
        // Loop if pixel buffer smaller than width
        while (iWidth > 0)
        {
            // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
            if (iWidth <= BUFFER_SIZE)
                for (iCount = 0; iCount < iWidth; iCount++)
                    usTemp[dmaBuf][iCount] = usPalette[*s++];
            else
                for (iCount = 0; iCount < BUFFER_SIZE; iCount++)
                    usTemp[dmaBuf][iCount] = usPalette[*s++];

#ifdef USE_DMA
            tft.dmaWait();
            tft.pushPixelsDMA(&usTemp[dmaBuf][0], iCount);
            dmaBuf = !dmaBuf;
#else
            tft.pushPixels(&usTemp[0][0], iCount);
#endif
            iWidth -= iCount;
        }
    }
} /* GIFDraw() */