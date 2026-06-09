#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>

/* LED configuration via DeviceTree aliases */
#define LED_A_NODE DT_ALIAS(led0)   /* Green LED */
#define LED_B_NODE DT_ALIAS(led2)   /* Red   LED */

static const struct gpio_dt_spec ledA = GPIO_DT_SPEC_GET(LED_A_NODE, gpios);
static const struct gpio_dt_spec ledB = GPIO_DT_SPEC_GET(LED_B_NODE, gpios);

/* Thread priorities and sleep durations */
#define PRIO_THREAD_A   5       /* higher priority (lower number) */
#define PRIO_THREAD_B   7       /* lower priority                 */
#define TEMPO_A_MS   1500
#define TEMPO_B_MS   1000

/* ----------------------------------------------------
 * Thread A — short task, high priority
 * ---------------------------------------------------- */
void thread_A(void *p1, void *p2, void *p3)
{
    while (1) {
        gpio_pin_set_dt(&ledA, 1);

        /* Simulate short computation */
        for (volatile int i = 0; i < 200000; i++) {}

        gpio_pin_set_dt(&ledA, 0);
        k_msleep(TEMPO_A_MS);
    }
}

/* ----------------------------------------------------
 * Thread B — long task, lower priority
 * ---------------------------------------------------- */
void thread_B(void *p1, void *p2, void *p3)
{
    while (1) {
        gpio_pin_set_dt(&ledB, 1);

        /* Simulate longer computation */
        for (volatile int i = 0; i < 10000000; i++) {}

        gpio_pin_set_dt(&ledB, 0);
        k_msleep(TEMPO_B_MS);
    }
}

/* Static thread definitions */
K_THREAD_DEFINE(a_tid, 512, thread_A, NULL, NULL, NULL, PRIO_THREAD_A, 0, 0);
K_THREAD_DEFINE(b_tid, 512, thread_B, NULL, NULL, NULL, PRIO_THREAD_B, 0, 0);

int main(void)
{
    if (!device_is_ready(ledA.port) || !device_is_ready(ledB.port)) {
        return -1;
    }

    gpio_pin_configure_dt(&ledA, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ledB, GPIO_OUTPUT_INACTIVE);

    printk("=== Demonstracao de Preempcao com Threads ===\n");
    printk("Thread A (verde): prioridade %d, dorme %d ms\n", PRIO_THREAD_A, TEMPO_A_MS);
    printk("Thread B (vermelho): prioridade %d, dorme %d ms\n", PRIO_THREAD_B, TEMPO_B_MS);

    k_sleep(K_FOREVER);
    return 0;
}
