# Relatório — Threads e Prioridades no Zephyr RTOS

## Nome
Felipe Beserra de Oliveira

---

## Número USP
13683702

---

## Respostas, comentários e análises

### Descrição da Atividade

O experimento demonstra o comportamento do escalonador preemptivo do Zephyr RTOS por meio de duas threads com prioridades diferentes, cada uma controlando um LED da placa FRDM-KL25Z. Thread A (prioridade 5, alta) pisca o LED verde; Thread B (prioridade 7, baixa) pisca o LED vermelho.

---

### Pergunta 1 — Diagrama temporal de execução

Com as configurações padrão (`PRIO_A=5, PRIO_B=7, TEMPO_A=1500 ms, TEMPO_B=1000 ms`):

```
Tempo (ms):  0    200  1500 1700 2700 3200 3400 4400...
Thread A:    [RUN].....[SLP 1500ms][RUN].....[SLP]...
Thread B:         [RUN (longo)...        ][RUN...]
LED verde:   ON   OFF              ON   OFF
LED vermelho:     ON         OFF              ON ...
```

- Thread A inicia, executa seu loop rápido (~200 000 iterações ≈ alguns ms), desliga o LED verde e dorme 1500 ms.
- Thread B entra em execução quando A dorme, executa seu loop longo (~10 000 000 iterações ≈ alguns centenas de ms), desliga o LED vermelho e dorme 1000 ms.
- Quando Thread A acorda após 1500 ms, ela **preempta** Thread B imediatamente (mesmo que B ainda esteja em execução), pois tem prioridade maior.

**Caso com prioridades iguais (ambas = 5):**  
As threads dividem o tempo por *time-slicing* (se habilitado) ou por ordem de criação. Sem `k_sleep()`, a thread que chegar primeiro ao escalonador monopoliza a CPU até dormir.

**Caso com Thread B dormindo muito menos (e.g. 100 ms):**  
B acorda mais frequentemente, mas A ainda preempta toda vez que acorda, independentemente do estado de B.

---

### Pergunta 2 — Sem `k_sleep()` e threads cooperativas

**Sem `k_sleep()` em Thread B:**  
Thread B nunca libera voluntariamente a CPU. Se A tem prioridade maior, cada vez que A acorda ela preempta B (comportamento correto com preempção). Se tiverem **prioridade igual**, B nunca retorna ao escalonador — Thread A nunca executa.

**Prioridades iguais:**  
O Zephyr usa escalonamento FIFO por padrão dentro de um mesmo nível de prioridade. A primeira thread a ser escalonada roda até bloquear ou ceder explicitamente (`k_yield()`). Sem `k_sleep()` ou `k_yield()`, a outra thread não executa — isso é chamado de **starvation** por ausência de preempção no mesmo nível.

**Threads cooperativas (prioridade -1 a -NUM_COOP_PRIORITIES):**  
No Zephyr, prioridades negativas são *cooperative*: o escalonador **não preempta** essas threads. Elas só cedem a CPU quando chamam explicitamente `k_yield()`, `k_sleep()`, ou bloqueiam num semáforo/mutex. São úteis para tarefas críticas que não devem ser interrompidas, mas exigem disciplina do programador.

---

### Pergunta 3 — Starvation (inanição)

Sim, é possível que uma thread **nunca execute**. Isso ocorre quando:
- Uma thread de prioridade mais alta **nunca dorme** (`k_sleep()` nunca é chamado)
- E o sistema usa escalonamento preemptivo

Combinações que causam starvation:
- Thread A: `PRIO=5`, sem `k_sleep()` (loop infinito sem bloqueio)
- Thread B: `PRIO=7`

Thread B nunca é escalonada porque Thread A nunca libera a CPU.

Esse fenômeno é conhecido como **starvation** (inanição de threads). Mecanismos de mitigação incluem:
- **Priority inheritance** (herança de prioridade) — usado em mutexes
- **Round-robin scheduling** — fatia de tempo garantida a cada nível
- Design cuidadoso: todas as threads devem chamar `k_sleep()` ou equivalente periodicamente

---

### Pergunta 4 — Interrupções de hardware (ISR)

Interrupções de hardware têm **prioridade máxima** no sistema — acima de qualquer thread, incluindo as de prioridade mais alta. Quando uma ISR é acionada, a thread corrente é pausada imediatamente, a ISR executa, e então o escalonador decide qual thread retoma.

**Implementação do botão com interrupção:**

```c
#define BTN_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(BTN_NODE, gpios);
static struct gpio_callback btn_cb;

static void btn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* ISR — executa com máxima prioridade, preempta qualquer thread */
    printk("Botao pressionado (ISR)!\n");
}

/* No main(): */
gpio_pin_configure_dt(&btn, GPIO_INPUT);
gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
gpio_init_callback(&btn_cb, btn_isr, BIT(btn.pin));
gpio_add_callback(btn.port, &btn_cb);
```

O LED pisca normalmente pelas threads, mas sempre que o botão é pressionado, a ISR interrompe a execução (mesmo dentro do loop longo de Thread B), imprime a mensagem e retorna. Isso demonstra que a ISR tem prioridade sobre qualquer thread.

---

### Pergunta 5 — Logging vs. printk vs. printf vs. debug

| Mecanismo | Descrição |
|---|---|
| `printk()` | Síncrono, bloqueante, mínima dependência de configuração. Funciona mesmo com pouca memória. Sem formatação de ponto flutuante. |
| `printf()` | Depende da libc (newlib/picolibc). Mais lento e pesado. Suporta `%f`, mas consome mais ROM/RAM. |
| `LOG_*()` (Logging) | Assíncrono (se `CONFIG_LOG_MODE_DEFERRED`): a mensagem é colocada numa fila e processada por uma thread dedicada. Não bloqueia a thread chamadora. Adiciona timestamp, nível (DBG/INF/WRN/ERR), nome do módulo. Overhead maior, mas ideal para diagnóstico em produção. |
| `CONFIG_DEBUG` | Flag de compilação que habilita assertions e símbolos de debug no binário. Não é um mecanismo de *output* por si só. |

**Recomendação para esta atividade:**  
Durante desenvolvimento, `printk()` é prático por ser simples e imediato. Em código de produção ou quando o timing é crítico, `LOG_INF()` com modo *deferred* é preferível pois não atrasa as threads de alta prioridade.

---

## Código (main.c)

```c
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>

#define LED_A_NODE DT_ALIAS(led0)
#define LED_B_NODE DT_ALIAS(led2)

static const struct gpio_dt_spec ledA = GPIO_DT_SPEC_GET(LED_A_NODE, gpios);
static const struct gpio_dt_spec ledB = GPIO_DT_SPEC_GET(LED_B_NODE, gpios);

#define PRIO_THREAD_A   5
#define PRIO_THREAD_B   7
#define TEMPO_A_MS   1500
#define TEMPO_B_MS   1000

void thread_A(void *p1, void *p2, void *p3)
{
    while (1) {
        gpio_pin_set_dt(&ledA, 1);
        for (volatile int i = 0; i < 200000; i++) {}
        gpio_pin_set_dt(&ledA, 0);
        k_msleep(TEMPO_A_MS);
    }
}

void thread_B(void *p1, void *p2, void *p3)
{
    while (1) {
        gpio_pin_set_dt(&ledB, 1);
        for (volatile int i = 0; i < 10000000; i++) {}
        gpio_pin_set_dt(&ledB, 0);
        k_msleep(TEMPO_B_MS);
    }
}

K_THREAD_DEFINE(a_tid, 512, thread_A, NULL, NULL, NULL, PRIO_THREAD_A, 0, 0);
K_THREAD_DEFINE(b_tid, 512, thread_B, NULL, NULL, NULL, PRIO_THREAD_B, 0, 0);

int main(void)
{
    gpio_pin_configure_dt(&ledA, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ledB, GPIO_OUTPUT_INACTIVE);
    printk("=== Demonstracao de Preempcao com Threads ===\n");
    k_sleep(K_FOREVER);
    return 0;
}
```

---

## Repositório

```text
https://github.com/Beserrovsky/Atividade-6
```
