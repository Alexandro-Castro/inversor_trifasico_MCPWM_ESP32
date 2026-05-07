# Inversor de Frequência Trifásico com ESP32 e MCPWM

Firmware desenvolvido em **ESP-IDF** para geração de sinais PWM trifásicos aplicados ao controle em malha aberta de um **motor de indução trifásico** por meio de um inversor de frequência. O projeto utiliza o periférico **MCPWM do ESP32** para produzir sinais complementares para os braços do inversor, com suporte a modulação **SPWM** e **SVM/SVPWM**, ajuste de frequência e partida suave.

Este repositório reúne o código-fonte utilizado no desenvolvimento do Trabalho de Conclusão de Curso em Engenharia Elétrica: **Inversor de frequência trifásico para controle em malha aberta de motor de indução trifásico**.

---

## Visão geral

O objetivo do projeto é implementar, em um microcontrolador ESP32, a lógica de controle necessária para acionar uma ponte inversora trifásica. O firmware gera três referências defasadas de 120° entre si e atualiza os valores de comparação do MCPWM para formar as tensões de fase do inversor.

A proposta é voltada para estudo, prototipagem e validação experimental de conceitos de acionamentos elétricos, incluindo:

* geração digital de PWM trifásico;
* modulação senoidal por largura de pulso, SPWM;
* modulação vetorial espacial, SVM/SVPWM;
* controle escalar em malha aberta do tipo V/f;
* partida suave com aumento gradual da frequência;
* interface local com display LCD I2C e botões;
* ajuste de frequência por potenciômetro via ADC.

---

## Funcionalidades implementadas

### Geração PWM trifásica

O firmware configura o MCPWM do ESP32 para gerar seis sinais de comando, correspondentes aos interruptores superiores e inferiores dos três braços do inversor:

| Fase | Chave superior | Chave inferior |
| ---- | -------------: | -------------: |
| A    |        GPIO 32 |        GPIO 33 |
| B    |        GPIO 14 |        GPIO 13 |
| C    |        GPIO 12 |  GPIO 10 / SD3 |

> **Atenção:** o GPIO 10 pode não estar disponível em alguns módulos ESP32 por estar associado à memória flash. Antes de montar o hardware, confirme a pinagem real da placa utilizada.

### Modos de modulação

O menu permite selecionar entre duas estratégias de modulação:

* **SPWM**: geração de três senóides defasadas de 120°;
* **SVM/SVPWM**: modulação vetorial espacial baseada nas componentes `Ualpha` e `Ubeta`.

A atualização dos valores de duty cycle é feita periodicamente por uma rotina associada ao temporizador, usando registradores do MCPWM para alterar os comparadores das três fases.

### Controle V/f em malha aberta

O projeto não implementa controle de velocidade em malha fechada. A estratégia utilizada é escalar, em malha aberta, ajustando a frequência elétrica de saída e o índice de modulação.

Para frequências abaixo da frequência nominal, o índice de modulação é ajustado proporcionalmente à frequência:

```text
m = f / f_nominal
```

Assim, o firmware busca manter aproximadamente constante a relação tensão/frequência do motor durante a operação abaixo da frequência nominal.

### Partida suave

O firmware inclui uma rotina de partida suave que inicia a saída em baixa frequência e incrementa gradualmente a referência até atingir o valor desejado. Essa estratégia reduz o impacto de partida quando comparada à aplicação direta da frequência nominal.

### Ajuste por potenciômetro

A frequência de referência pode ser obtida por leitura analógica. O ADC realiza a leitura de um potenciômetro e aplica uma média móvel simples para reduzir oscilações. No código atual, a faixa de frequência estimada fica aproximadamente entre **6 Hz e 120 Hz**, dependendo da leitura ADC.

### Interface com LCD e botões

O sistema possui interface local com:

* display LCD 16x2 via I2C;
* botões para navegação no menu;
* seleção do tipo de modulação;
* seleção do modo de referência;
* exibição da frequência e do índice de modulação.

---

## Arquitetura do projeto

Estrutura principal do firmware:

```text
.
├── CMakeLists.txt
├── README.md
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    ├── gerador_senoide/
    │   ├── CMakeLists.txt
    │   ├── gerador_senoide.c
    │   └── include/
    │       └── gerador_senoide.h
    └── HD44780/
        ├── CMakeLists.txt
        ├── HD44780.c
        └── include/
            └── HD44780.h
```

### `main/main.c`

Arquivo principal do projeto. Contém:

* configuração inicial do MCPWM;
* definição dos pinos de saída;
* tarefas FreeRTOS;
* leitura dos botões;
* leitura do ADC;
* controle de frequência;
* rotina de partida suave;
* menu de operação;
* atualização periódica dos duty cycles.

### `components/gerador_senoide`

Componente responsável pelas funções de modulação:

* `spwm_gen(...)`: calcula os valores de comparação das três fases usando senóides defasadas de 120°;
* `svm_run(...)`: calcula as funções de chaveamento para modulação vetorial espacial.

### `components/HD44780`

Componente para comunicação com display LCD baseado no controlador HD44780 utilizando interface I2C.

---

## Parâmetros principais do firmware

| Parâmetro                   | Valor no código | Descrição                                 |
| --------------------------- | --------------: | ----------------------------------------- |
| Frequência de modulação     |          20 kHz | Frequência da portadora PWM               |
| Clock do MCPWM              |          80 MHz | Resolução base do temporizador            |
| Frequência nominal          |           60 Hz | Referência nominal da senóide             |
| Frequência inicial          |            6 Hz | Frequência inicial usada na partida       |
| Índice de modulação inicial |             0,1 | Valor inicial antes do ajuste V/f         |
| Tempo morto                 |        80 ticks | Atraso aplicado aos sinais complementares |
| Display                     |        16x2 I2C | Interface de operação local               |
| I2C SDA                     |         GPIO 21 | Linha de dados do LCD                     |
| I2C SCL                     |         GPIO 22 | Linha de clock do LCD                     |

---

## Hardware previsto

O projeto considera uma montagem experimental composta por:

* ESP32;
* driver de porta para chaves semicondutoras;
* ponte inversora trifásica;
* motor de indução trifásico;
* fonte/barramento CC compatível com o estágio de potência;
* display LCD 16x2 com módulo I2C;
* potenciômetro para referência analógica de frequência;
* botões para navegação e seleção no menu.

> **Importante:** as saídas do ESP32 não devem ser conectadas diretamente às chaves de potência do inversor. É necessário utilizar drivers apropriados, isolamento quando aplicável, proteção contra shoot-through, dimensionamento correto de dead time, proteção de sobrecorrente e procedimentos seguros de teste.

---

## Como compilar e gravar

### Pré-requisitos

* ESP-IDF instalado;
* Python e ferramentas do ESP-IDF configuradas;
* placa ESP32 compatível;
* cabo USB para gravação;
* VS Code com extensão ESP-IDF, opcional.

### Clonar o repositório

```bash
git clone https://github.com/Alexandro-Castro/inversor_trifasico_MCPWM_ESP32.git
cd inversor_trifasico_MCPWM_ESP32
```

### Configurar o alvo

```bash
idf.py set-target esp32
```

### Compilar

```bash
idf.py build
```

### Gravar no ESP32

No Linux:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

No Windows, ajuste a porta conforme o Gerenciador de Dispositivos:

```bash
idf.py -p COM4 flash monitor
```

---

## Fluxo básico de operação

1. Energizar apenas a parte lógica inicialmente.
2. Gravar o firmware no ESP32.
3. Verificar a inicialização do LCD.
4. Selecionar o tipo de modulação: **SPWM** ou **SVM**.
5. Selecionar o modo de referência de frequência.
6. Iniciar a partida suave.
7. Monitorar a frequência exibida no LCD.
8. Somente após validação dos sinais PWM em baixa tensão, conectar o estágio de potência conforme as proteções necessárias.

---

## Limitações atuais

* Controle em malha aberta, sem realimentação de velocidade ou corrente.
* O modo discreto aparece na estrutura de menu, mas ainda precisa de implementação funcional completa.
* A proteção de corrente, tensão de barramento e falhas do inversor não está consolidada no firmware atual.
* O ajuste V/f é simplificado e não considera compensação de queda de tensão em baixa frequência.
* A pinagem deve ser revisada conforme a placa ESP32 usada, especialmente o GPIO 10.

---

## Possíveis melhorias futuras

* Implementar controle em malha fechada de velocidade com encoder ou sensor Hall.
* Adicionar medição de corrente de fase.
* Implementar proteção de sobrecorrente e subtensão/sobretensão do barramento CC.
* Criar tabela de parâmetros configuráveis via menu.
* Salvar parâmetros em NVS.
* Melhorar a estratégia V/f com compensação em baixa frequência.
* Implementar rampa de aceleração e desaceleração configurável.
* Adicionar registro de falhas.
* Implementar comunicação serial, Wi-Fi ou interface web para parametrização.
* Substituir escrita direta em registradores por chamadas de API quando possível, aumentando portabilidade e legibilidade.

---

## Referência acadêmica

Este firmware foi desenvolvido como parte de um Trabalho de Conclusão de Curso em Engenharia Elétrica sobre o desenvolvimento de um inversor de frequência trifásico para acionamento em malha aberta de motor de indução trifásico.

O PDF do TCC disponível no repositório apresenta a fundamentação teórica, os conceitos de modulação, a estrutura do inversor e a validação experimental do projeto.

---

## Autor

**Alexandro Castro**
Engenharia Elétrica

---

## Licença

Este repositório ainda não possui uma licença definida. Caso o objetivo seja permitir reutilização acadêmica ou técnica do código, recomenda-se adicionar uma licença, como MIT, BSD-3-Clause ou GPL-3.0, conforme o nível de abertura desejado.
