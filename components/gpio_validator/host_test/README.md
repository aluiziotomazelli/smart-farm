# Teste de Host: gpio_validator

Este diretório contém um exemplo de como configurar testes unitários para serem executados no seu computador (Linux/macOS), sem necessidade de um ESP32 físico.

## Pré-requisitos
*   Ruby instalado (`sudo apt install ruby`)
*   libbsd instalado (`sudo apt install libbsd-dev`)

## Como Executar
1.  Abra o terminal na raiz do projeto.
2.  Exporte o ambiente do ESP-IDF:
    ```bash
    . $IDF_PATH/export.sh
    ```
3.  Navegue até esta pasta:
    ```bash
    cd components/gpio_validator/host_test
    ```
4.  Configure o target para Linux:
    ```bash
    idf.py --preview set-target linux
    ```
5.  Compile e execute:
    ```bash
    idf.py build
    ./build/test_gpio_validator_host.elf
    ```

## O que este teste valida?
Ele valida a lógica de `gpio_validator.cpp` simulando diferentes modelos de chip (`CHIP_ESP32`, `CHIP_ESP32S3`, etc) e verificando se a função `validate` bloqueia corretamente os pinos reservados de cada arquitetura.
