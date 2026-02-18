#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <util/delay.h>
#include <stdint.h>

#define LED_COUNT 24

// SK6812 RGBW data output on PB7 (board-specific mapping)
#define LED_DDR  DDRB
#define LED_PORT PORTB
#define LED_PIN  PB7

// Tactile switch input (active low with pull-up)
// NOTE: verify this mapping against your exact package + PCB net.
#define SW_DDR   DDRD
#define SW_PORT  PORTD
#define SW_PINR  PIND
#define SW_PIN   PD5

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
} rgbw_t;

static inline void sk6812_send_bit(uint8_t bit)
{
    // SK6812/WS2812-like 800kHz single-wire timing.
    // Tuned for F_CPU = 8MHz.
    if (bit) {
        // T1H ~0.7-0.8us, T1L ~0.5-0.6us
        LED_PORT |= (1 << LED_PIN);
        __asm__ __volatile__(
            "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
        );
        LED_PORT &= ~(1 << LED_PIN);
        __asm__ __volatile__("nop\n\t" "nop\n\t" "nop\n\t");
    } else {
        // T0H ~0.3-0.4us, T0L ~0.9us
        LED_PORT |= (1 << LED_PIN);
        __asm__ __volatile__("nop\n\t" "nop\n\t");
        LED_PORT &= ~(1 << LED_PIN);
        __asm__ __volatile__(
            "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t" "nop\n\t"
        );
    }
}

static void sk6812_send_byte(uint8_t byte)
{
    for (uint8_t i = 0; i < 8; i++) {
        sk6812_send_bit(byte & 0x80);
        byte <<= 1;
    }
}

static void sk6812_show(const rgbw_t *leds, uint8_t count)
{
    cli();
    for (uint8_t i = 0; i < count; i++) {
        // SK6812 RGBW common order is GRBW.
        sk6812_send_byte(leds[i].g);
        sk6812_send_byte(leds[i].r);
        sk6812_send_byte(leds[i].b);
        sk6812_send_byte(leds[i].w);
    }
    sei();

    // Reset/latch time (SK6812 typically >=80us)
    _delay_us(120);
}

static void fill_all(rgbw_t *leds, uint8_t count, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    for (uint8_t i = 0; i < count; i++) {
        leds[i].r = r;
        leds[i].g = g;
        leds[i].b = b;
        leds[i].w = w;
    }
}

static rgbw_t wheel(uint8_t pos)
{
    rgbw_t c;
    c.w = 0;

    if (pos < 85) {
        c.r = 255 - pos * 3;
        c.g = pos * 3;
        c.b = 0;
    } else if (pos < 170) {
        pos -= 85;
        c.r = 0;
        c.g = 255 - pos * 3;
        c.b = pos * 3;
    } else {
        pos -= 170;
        c.r = pos * 3;
        c.g = 0;
        c.b = 255 - pos * 3;
    }
    return c;
}


static void clock_init_full_speed(void)
{
    // Force clock prescaler to /1 at runtime.
    // This avoids CKDIV8 fuse causing 1MHz operation, which breaks SK6812 timing.
    clock_prescale_set(clock_div_1);
}

static uint8_t switch_pressed(void)
{
    return (SW_PINR & (1 << SW_PIN)) == 0;
}

int main(void)
{
    rgbw_t leds[LED_COUNT];

    clock_init_full_speed();

    LED_DDR |= (1 << LED_PIN);
    LED_PORT &= ~(1 << LED_PIN);

    SW_DDR &= ~(1 << SW_PIN);
    SW_PORT |= (1 << SW_PIN);

    while (1) {
        if (switch_pressed()) {
            fill_all(leds, LED_COUNT, 0, 0, 0, 0);
            sk6812_show(leds, LED_COUNT);
            _delay_ms(20);
            continue;
        }

        // 1) All Red [1s]
        fill_all(leds, LED_COUNT, 255, 0, 0, 0);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);

        // 2) All Green [1s]
        fill_all(leds, LED_COUNT, 0, 255, 0, 0);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);

        // 3) All Blue [1s]
        fill_all(leds, LED_COUNT, 0, 0, 255, 0);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);

        // 4) All White [1s] - use dedicated W channel for RGBW LED
        fill_all(leds, LED_COUNT, 0, 0, 0, 255);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);

        // 5) All Off [1s]
        fill_all(leds, LED_COUNT, 0, 0, 0, 0);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);

        // 6) Animated rainbow [3s]
        for (uint8_t frame = 0; frame < 60; frame++) {
            for (uint8_t i = 0; i < LED_COUNT; i++) {
                uint8_t p = (uint8_t)((i * 256 / LED_COUNT) + (frame * 256 / 60));
                leds[i] = wheel(p);
            }
            sk6812_show(leds, LED_COUNT);
            _delay_ms(50);

            if (switch_pressed()) {
                break;
            }
        }

        // 7) All Off [1s]
        fill_all(leds, LED_COUNT, 0, 0, 0, 0);
        sk6812_show(leds, LED_COUNT);
        _delay_ms(1000);
    }
}
