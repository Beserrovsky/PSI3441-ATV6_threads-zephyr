# Relatório — Threads: ADC, Botão com Interrupção e Acelerômetro

## Nome
Felipe Beserra de Oliveira

---

## Número USP
13683702

---

## Respostas, comentários e análises

### Descrição da Atividade

O enunciado pede a integração de três periféricos em um único projeto multi-thread no Zephyr RTOS:

1. **ADC** — leitura analógica em PTB0 (`ADC0_SE8`), a mesma fiação de teste usada na Atividade 4, lida agora via driver `adc.h` do Zephyr (canal configurado via `adc_channel_setup`/`adc_read`), em vez de registradores diretos.
2. **Botão com interrupção** — o botão onboard `SW0` (`user_button_0`, PTA16) configurado com pull-up e interrupção na borda de descida.
3. **Acelerômetro MMA8451Q** — sensor onboard da FRDM-KL25Z, acessado via I2C0 (PTE24/PTE25) através da API genérica de sensores do Zephyr (`sensor_sample_fetch`/`sensor_channel_get`). O node `mma8451q` (alias `accel0`) já vem habilitado por padrão na definição da placa, sem necessidade de overlay.

### Threads

- **`thread_adc`** (prioridade 5): lê o canal ADC a cada 500ms e imprime o valor bruto e em mV.
- **`thread_accel`** (prioridade 5): lê os eixos X/Y/Z do acelerômetro a cada 1000ms, mas só imprime o resultado quando o modo atual é "Completo" — evitando leituras I2C desnecessárias quando o usuário só quer ver o ADC.

### Botão e troca de modo

O botão é configurado com interrupção (`GPIO_INT_EDGE_FALLING`, conforme o tutorial de referência). A cada pressionamento, a ISR `button_isr` inverte a flag `modo_completo` e imprime a mudança de modo:

- **Modo ADC** (estado inicial): só os valores do ADC aparecem na serial.
- **Modo Completo**: ADC e acelerômetro aparecem juntos.

### Item 6/7 — Prioridades iguais vs. diferentes

O código inicial usa as duas threads com a **mesma prioridade** (`PRIO_THREAD_ADC = PRIO_THREAD_ACCEL = 5`), conforme pedido no item 6. Para o experimento do item 7, basta alterar uma das macros no topo do `main.c` (por exemplo, `PRIO_THREAD_ADC` para 3, deixando-a mais prioritária) e reobservar a saída serial.

**Comportamento esperado:**

- **Prioridades iguais:** como as duas threads bloqueiam periodicamente em `k_msleep()`, o escalonador as intercala de forma cooperativa por ordem de "despertar" — não há disputa real, já que nenhuma delas monopoliza a CPU. As leituras de ADC (500ms) aparecem com o dobro da frequência das do acelerômetro (1000ms), entrelaçadas na serial.
- **ADC com prioridade mais alta:** se a leitura do acelerômetro (transação I2C, mais lenta) estiver em andamento quando a thread do ADC "desperta", o escalonador preempta a thread do acelerômetro imediatamente; a leitura do ADC nunca é atrasada pelo I2C, ao custo de um pequeno atraso na thread do acelerômetro.
- **Acelerômetro com prioridade mais alta:** o inverso — a leitura do ADC pode ser postergada caso a transação I2C do acelerômetro esteja em andamento no instante em que a thread do ADC deveria rodar.

Como ambas as threads passam a maior parte do tempo dormindo (`k_msleep`), a diferença de prioridade só importa nos instantes raros em que as duas "despertam" quase simultaneamente — diferente do clássico exemplo de starvation (thread sem `k_sleep()`), aqui não há monopolização de CPU em nenhum dos casos.

**Validação física pendente:** a confirmação empírica desse comportamento (timestamps reais na serial com cada combinação de prioridade, e o teste do botão alternando os modos) depende de flashar o binário na placa e observar a saída — não exige nenhuma modificação de hardware, apenas executar e registrar os logs.

---

## Código (main.c)

```c
/*
 * Atividade 6 — Threads: ADC + Botao com Interrupcao + Acelerometro
 * FRDM-KL25Z: uma thread le o ADC (PTB0 / ADC0_SE8) a cada 500ms e
 * outra le o acelerometro onboard MMA8451Q a cada 1000ms. O botao
 * onboard (SW0) alterna, via interrupcao, entre "Modo ADC" (so ADC)
 * e "Modo Completo" (ADC + acelerometro).
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

/* Botao onboard (SW0/PTA16) e acelerometro onboard (MMA8451Q, I2C0) */
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
    printk("Modo inicial: ADC (pressione o botao SW0 para alternar)\n");

    return 0;
}
```

---

## Repositório

```text
https://github.com/Beserrovsky/PSI3441-ATV6_threads-zephyr
```
