# Análise do Sistema de Monitoramento de Caixa d'Água

Este documento detalha a análise da aplicação `water_tank` e sua integração com os componentes `core` e `sensor_floatswitch`. A arquitetura geral demonstra uma clara separação de responsabilidades, resultando em um sistema robusto, eficiente e otimizado para operação de baixo consumo de energia.

## 1. `apps/water_tank`: Orquestração e Lógica da Aplicação

A aplicação `water_tank` atua como o cérebro do sistema, orquestrando as operações de alto nível. Suas principais responsabilidades são:

- **Monitoramento de Nível:** Utiliza um sensor ultrassônico para medir a distância até a água e converte essa medida em um nível percentual (permil).
- **Máquina de Estados:** A lógica central gira em torno de uma máquina de estados que infere o estado de enchimento do tanque (`FILLING`, `DRAINING`, `STABLE`, `UNKNOWN`). Este estado é determinado comparando a leitura atual com a anterior, que é persistida na memória RTC para sobreviver aos ciclos de sono profundo.
- **Gerenciamento de Energia:** A estratégia de economia de energia é um pilar do design. Com base no `FillState` atual, a aplicação ajusta dinamicamente o intervalo de tempo para o próximo despertar (`deep sleep`). Períodos mais curtos são usados durante o enchimento ou esvaziamento para um monitoramento mais frequente, e períodos mais longos quando o nível está estável, economizando energia.
- **Integração de Componentes:** Não interage diretamente com o hardware de baixo nível ou com o sistema de arquivos. Em vez disso, consome os serviços fornecidos pelos componentes `core` (para persistência) e `sensor_floatswitch` (para leitura da boia e configuração de interrupções de hardware).

## 2. `components/core`: Persistência e Resiliência

O componente `core` serve como a memória de longo prazo do dispositivo e um registro de diagnóstico.

- **Abstração de Armazenamento:** Ele abstrai as complexidades do sistema de armazenamento não volátil (NVS) do ESP-IDF. A classe `NvsCore` gerencia um único "blob" de dados (`CoreStorage`) onde informações críticas são armazenadas.
- **Dados Gerenciados:** A `CoreStorage` armazena informações vitais sobre o ciclo de vida do dispositivo, como contagem de inicializações (`boot_count`), contagem de falhas (`crash_count`) e a causa do último despertar (`WakeSource`).
- **Resiliência do Sistema:** Ao inicializar, a `WaterTankApp` consulta o `NvsCore` para entender como foi despertada (e.g., por um temporizador ou por uma falha). Isso permite que a aplicação se comporte de maneira mais inteligente e robusta, por exemplo, registrando um crash se o boot não foi resultado de um ciclo de sono normal.

## 3. `components/sensor_floatswitch`: Interface de Hardware e Despertar por Evento

Este componente é um driver especializado para o sensor de boia, lidando com todas as interações de baixo nível.

- **Abstração de Hardware:** A classe `FloatSwitch` encapsula a complexidade da leitura de um pino GPIO, incluindo a configuração de resistores de pull-up/down. A aplicação simplesmente chama `read()` para obter um estado booleano (`true` para presença de água).
- **Debouncing de Sinal:** Sensores mecânicos são suscetíveis a "vibrações de contato". O componente implementa um algoritmo de debouncing de software robusto, garantindo que a aplicação receba apenas leituras estáveis e evitando falsos acionamentos.
- **Capacidade de Despertar (Wakeup):** A característica mais crítica para uma aplicação de baixo consumo. O componente detecta se o pino GPIO configurado pode ser usado como uma fonte de despertar do sono profundo (RTC-capable). Ele fornece à aplicação as informações necessárias (`WakeupInfo`) para configurar uma interrupção de hardware que pode acordar o dispositivo, permitindo uma resposta imediata a eventos críticos (como o tanque atingindo o nível máximo).

## 4. Integração Geral e Fluxo de Trabalho

A integração dos três componentes resulta em um sistema coeso e altamente eficiente:

1.  **Despertar:** O dispositivo acorda por um **temporizador** (monitoramento periódico) ou por uma **interrupção de GPIO** (evento crítico da boia).
2.  **Consultar Memória:** A `WaterTankApp` inicializa e carrega os dados do `NvsCore` para determinar a causa do despertar e verificar a saúde do sistema.
3.  **Analisar Sensores:** Realiza a leitura do sensor ultrassônico e consulta o `FloatSwitch` para obter uma imagem completa do estado do tanque.
4.  **Tomar Decisão:** Infere o `FillState` (enchendo, esvaziando, etc.).
5.  **Configurar Sono:** Com base no estado, calcula o próximo intervalo de sono e configura o temporizador. Além disso, consulta o `FloatSwitch` para configurar a interrupção de GPIO como um gatilho de despertar secundário.
6.  **Persistir Estado:** Antes de dormir, utiliza o `NvsCore` para salvar as informações atualizadas do ciclo de vida.
7.  **Dormir:** O dispositivo entra em `deep sleep`, consumindo o mínimo de energia até o próximo evento de despertar.

Esta arquitetura não só atende aos requisitos funcionais, mas o faz de uma maneira que maximiza a vida útil da bateria, garante a resiliência contra falhas e mantém o código organizado e modular.
