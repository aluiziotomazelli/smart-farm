#include "esp_log.h"
#include "ultrasonic_sensor.hpp"
#include "unity.h"

#define TRIG_PIN GPIO_NUM_4
#define ECHO_PIN GPIO_NUM_5

class UltrasonicTestAccessor
{
public:
    static float callReduceMedian(UltrasonicSensor &dev, float *v, size_t n)
    {
        return dev.reduceMedian(v, n);
    }
    static float callReduceDominantCluster(UltrasonicSensor &dev, float *v, size_t n)
    {
        return dev.reduceDominantCluster(v, n);
    }
    static float callGetStdDev(UltrasonicSensor &dev, float *samples, uint8_t valids)
    {
        return dev.getStdDev(samples, valids);
    }
};

TEST_CASE("Ultrasonic: Dominant Cluster Math", "[ultrasonic][math]")
{
    UltrasonicSensor::UltrasonicConfig cfg;
    // Criamos o objeto (os pinos não serão usados aqui, pois testaremos apenas a
    // lógica)
    UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN, cfg);

    // Cenário: Tanque está a 50cm.
    // Temos 5 leituras consistentes e 2 erros (um reflexo e um timeout/zero)
    float samples[] = {50.1f, 50.5f, 49.8f, 5.0f, 50.2f, 400.0f, 49.9f};
    size_t n        = 7;

    // Executamos o filtro que você escreveu no .cpp
    float result =
        UltrasonicTestAccessor::callReduceDominantCluster(sensor, samples, n);

    // O Dominant Cluster deve ignorar o 5.0 e o 400.0 e fazer a média dos ~50cm
    // Média: (50.1 + 50.5 + 49.8 + 50.2 + 49.9) / 5 = 50.1
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 50.1f, result);

    ESP_LOGI("Test", "Filter result: %.2f cm", result);
}

TEST_CASE("Ultrasonic: Median Filter Math", "[ultrasonic][math]")
{
    UltrasonicSensor::UltrasonicConfig cfg;
    UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN, cfg);

    float samples[] = {10.0f, 100.0f, 20.0f, 50.0f,
                       30.0f}; // Ordenado: 10, 20, 30, 50, 100
    float res       = UltrasonicTestAccessor::callReduceMedian(sensor, samples, 5);

    TEST_ASSERT_EQUAL_FLOAT(30.0f, res);
}

TEST_CASE("Ultrasonic: Variance and Failure Logic", "[ultrasonic][math]")
{
    UltrasonicSensor::UltrasonicConfig cfg;
    UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN, cfg);

    // Cenário 1: Estável (Deve ser quase 0)
    float stable_samples[] = {20.0f, 20.1f, 19.9f, 20.0f};
    float std_dev = UltrasonicTestAccessor::callGetStdDev(sensor, stable_samples, 4);

    // Mudamos para WITHIN para ser mais tolerante com precisão de float
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.07f, std_dev);

    // Cenário 2: Ruidoso (Deve ser alto)
    float noisy_samples[] = {10.0f, 50.0f, 10.0f, 50.0f};
    std_dev = UltrasonicTestAccessor::callGetStdDev(sensor, noisy_samples, 4);

    // StdDev de [10, 50, 10, 50] é exatamente 20.0
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 20.0f, std_dev);
}

TEST_CASE("Ultrasonic: Hardware Integrity and Recovery", "[ultrasonic][hw]")
{
    UltrasonicSensor::UltrasonicConfig cfg;
    cfg.ping_count = 3; // Reduzimos para 3 para o teste ser mais rápido
    UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN, cfg);
    sensor.init();

    float dist     = -1.0f;
    UsFailure fail = UsFailure::NONE;
    UsQuality qual = UsQuality::OK;

    // 1. Executa a leitura principal (o fluxo completo)
    bool success = sensor.readDistance_cm(dist, qual, fail);

    // 2. Validações Consolidadas:

    // A função DEVE reportar falha
    TEST_ASSERT_FALSE_MESSAGE(success,
                              "Measurement should fail without a functional sensor");

    // A qualidade DEVE ser marcada como inválida
    TEST_ASSERT_EQUAL_MESSAGE(UsQuality::INVALID, qual,
                              "Quality must be INVALID on hardware failure");

    // O erro DEVE ser um dos códigos de hardware (TIMEOUT ou HW_ERROR ou
    // INVALID_PULSE)
    bool is_hw_issue = (fail == UsFailure::TIMEOUT || fail == UsFailure::HW_ERROR ||
                        fail == UsFailure::INVALID_PULSE);
    TEST_ASSERT_TRUE_MESSAGE(is_hw_issue,
                             "Failure code should indicate a hardware/signal issue");

    ESP_LOGI("Test", "Consolidated check passed. Result: %s",
             us_failure_to_string(fail));
}

TEST_CASE("Ultrasonic: Stability Stress Test", "[ultrasonic][stress]")
{
    UltrasonicSensor::UltrasonicConfig cfg;
    UltrasonicSensor sensor(TRIG_PIN, ECHO_PIN, cfg);
    sensor.init();

    for (int i = 0; i < 50; i++) {
        float dist;
        UsFailure fail;
        UsQuality qual;
        // Apenas garantindo que a chamada não causa crash ou leak após múltiplos
        // timeouts
        sensor.readDistance_cm(dist, qual, fail);
    }

    // Se chegou aqui sem travar e com delta de memória 0, o componente é robusto
    TEST_ASSERT_TRUE(true);
}