/*
 * Atividade 6 — Threads: ADC + Botao com Interrupcao + Acelerometro
 * FRDM-KL25Z: uma thread le o ADC (PTB0 / ADC0_SE8) a cada 500ms e
 * outra le o acelerometro onboard MMA8451Q a cada 1000ms. O botao
 * (PTA16, pull-up interno, requer fio/botao externo para GND — a
 * FRDM-KL25Z nao tem um SW0 fisico) alterna, via interrupcao, entre
 * "Modo ADC" (so ADC) e "Modo Completo" (ADC + acelerometro).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <stdlib.h>

/* ADC — PTB0 / ADC0_SE8 (mesma fiacao de teste usada na Atividade 4) */
#define ADC_NODE            DT_NODELABEL(adc0)
#define ADC_CHANNEL_ID      8
#define ADC_RESOLUTION      12
#define ADC_VREF_MV         3300

/* Botao em PTA16 (alias "sw0" no devicetree, sem botao fisico onboard —
 * requer fio/botao externo para GND) e acelerometro onboard (MMA8451Q, I2C0) */
#define BUTTON_NODE         DT_NODELABEL(user_button_0)
#define ACCEL_NODE          DT_NODELABEL(mma8451q)

/* Prioridades das threads — alterar aqui para o experimento do item 7 */
#define PRIO_THREAD_ADC     5
#define PRIO_THREAD_ACCEL   5

static const struct device *adc_dev   = DEVICE_DT_GET(ADC_NODE);
static const struct device *accel_dev = DEVICE_DT_GET(ACCEL_NODE);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;

static volatile bool modo_completo;

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    modo_completo = !modo_completo;
    printk("\n[Botao] Modo: %s\n", modo_completo ? "COMPLETO (ADC + Acelerometro)" : "ADC");
}

/* ----------------------------------------------------
 * Thread ADC — le o canal 8 (PTB0) a cada 500ms
 * ---------------------------------------------------- */
void thread_adc(void *p1, void *p2, void *p3)
{
    int16_t sample_buffer;
    struct adc_channel_cfg channel_cfg = {
        .gain             = ADC_GAIN_1,
        .reference        = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,
        .channel_id       = ADC_CHANNEL_ID,
        .differential     = 0,
    };
    struct adc_sequence sequence = {
        .channels    = BIT(ADC_CHANNEL_ID),
        .buffer      = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution  = ADC_RESOLUTION,
    };

    if (!device_is_ready(adc_dev) || adc_channel_setup(adc_dev, &channel_cfg) != 0) {
        printk("Erro ao inicializar o ADC\n");
        return;
    }

    while (1) {
        if (adc_read(adc_dev, &sequence) == 0) {
            int32_t mv = sample_buffer;

            adc_raw_to_millivolts(ADC_VREF_MV, ADC_GAIN_1, ADC_RESOLUTION, &mv);
            printk("[ADC] %d (raw) -> %d mV\n", sample_buffer, mv);
        }
        k_msleep(500);
    }
}

/* ----------------------------------------------------
 * Thread Acelerometro — le X/Y/Z a cada 1000ms; so exibe
 * quando o modo atual e "Completo"
 * ---------------------------------------------------- */
void thread_accel(void *p1, void *p2, void *p3)
{
    struct sensor_value ax, ay, az;

    if (!device_is_ready(accel_dev)) {
        printk("Erro: acelerometro MMA8451Q nao disponivel\n");
        return;
    }

    while (1) {
        if (modo_completo) {
            if (sensor_sample_fetch(accel_dev) == 0) {
                sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_X, &ax);
                sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Y, &ay);
                sensor_channel_get(accel_dev, SENSOR_CHAN_ACCEL_Z, &az);
                printk("[Acel] X=%d.%06d Y=%d.%06d Z=%d.%06d (m/s^2)\n",
                       ax.val1, abs(ax.val2), ay.val1, abs(ay.val2), az.val1, abs(az.val2));
            }
        }
        k_msleep(1000);
    }
}

K_THREAD_DEFINE(adc_tid,   1024, thread_adc,   NULL, NULL, NULL, PRIO_THREAD_ADC,   0, 0);
K_THREAD_DEFINE(accel_tid, 1024, thread_accel, NULL, NULL, NULL, PRIO_THREAD_ACCEL, 0, 0);

int main(void)
{
    if (!gpio_is_ready_dt(&button)) {
        printk("Erro: botao nao disponivel\n");
        return -1;
    }

    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    printk("=== Atividade 6 - ADC + Botao + Acelerometro ===\n");
    printk("Modo inicial: ADC (conecte PTA16 a GND para alternar)\n");

    return 0;
}
