#pragma once
#include "app_protocol_types.hpp"
// #include "common_types.hpp"

// Constantes específicas para a lógica de negócio do water_tank
static constexpr float LEVEL_MIN_CM = 150.0f; // caixa vazia
static constexpr float LEVEL_MAX_CM = 20.0f;  // caixa cheia

static constexpr uint16_t LEVEL_DELTA_MIN = 10; // 1.0%

static constexpr uint64_t TIMER_FILLING_US = 30ULL * 1000000ULL;       // 30 s
static constexpr uint64_t TIMER_STABLE_US = 5ULL * 60ULL * 1000000ULL; // 5 min
static constexpr uint64_t TIMER_DRAIN_US = 2ULL * 60ULL * 1000000ULL;  // 2 min
static constexpr uint64_t TIMER_UNKNOWN_US = 60ULL * 1000000ULL;       // 1 min

static constexpr float WEAK_SLEEP_FACTOR = 0.5f;
static constexpr float INVALID_SLEEP_FACTOR = 0.25f;

static constexpr uint8_t CONSECUTIVE_FAILURES_THRESHOLD = 5;
static constexpr uint64_t BACKUP_MODE_SLEEP_US = 15ULL * 1000000ULL; // 15 seconds

enum class FillState : uint8_t
{
    UNKNOWN = 0,
    STABLE,
    FILLING,
    DRAINING
};
