# PowerControl Component Unit Tests

This directory contains the unit tests for the `power_control` component, following the Espressif test standard (running on real hardware, without mocks).

## Structure


```
test/
├── CMakeLists.txt              # Configuração do projeto de teste
├── main/
│   ├── CMakeLists.txt          # Configuração do componente de teste
│   └── power_control_test.cpp # Implementação dos testes
└── README.md                   # Este arquivo
```


## Test Cases

### 1. Initialization Tests (5 cases)
- ✅ Initialization with valid GPIO (GPIO 2 - LED)
- ✅ Initialization with invalid GPIOs - flash pins (6, 7, 8, 11)
- ✅ Initialization with invalid GPIOs - input-only pins (34, 35, 36, 39)
- ✅ Initialization with initial state ON
- ✅ Initialization with initial state OFF

### 2. Control Tests (4 cases)
- ✅ Turn ON and OFF (turnOn/turnOff)
- ✅ Toggle state (toggle)
- ✅ Inverted logic (inverted_logic)
- ✅ Blink pattern (visual test)

### 3. State Tests (3 cases)
- ✅ Operations without initialization (should fail)
- ✅ Deinitialization
- ✅ Multiple deinit calls (idempotency)

### 4. Multiple Instances Tests (2 cases)
- ✅ Multiple instances with different GPIOs
- ✅ LED with multiple simultaneous operations

**Total: 15 test cases**

## GPIOs Used

- **GPIO 2**: Board's built-in LED (visual tests)
- **GPIO 4, 5**: Valid GPIOs for multiple instance tests
- **GPIO 6, 7, 8, 11**: Flash pins (negative validation tests)
- **GPIO 34, 35, 36, 39**: Input-only pins (negative validation tests)

## How to Compile and Run

### 1. Navigate to the test directory
```bash
cd components/power_control/test
```

### 2. Configure the project (first time)
```bash
idf.py set-target esp32
idf.py menuconfig  # Optional: adjust configurations
```

### 3. Compile
```bash
idf.py build
```

### 4. Flash and monitor
```bash
idf.py flash monitor
```

### 5. Exit monitor
Press `Ctrl+]`

## Expected Output

The tests use the Unity framework and produce formatted output:

```
===========================================
  PowerControl Component Unit Tests
===========================================

Running tests on hardware (no mocks)
Visual tests will use GPIO 2 (built-in LED)

Running PowerControl: Init with valid GPIO...
[PASS]

Running PowerControl: Init with invalid GPIO - Flash pins...
[PASS]

...

-----------------------
15 Tests 0 Failures 0 Ignored 
OK

===========================================
  All tests completed!
===========================================
```

## Visual Tests

Some tests include delays to allow visual verification of the LED:
- **Init with initial state ON/OFF**: LED turns on/off for 1 second
- **Turn ON and OFF**: LED alterna com delay de 1 segundo
- **Toggle functionality**: LED pisca rapidamente (500ms)
- **Blink pattern**: LED pisca 5 vezes (200ms ON, 200ms OFF)
- **Inverted logic**: Verifica que lógica invertida funciona corretamente

## Execute Specific Tests

The Unity framework allows filtering tests by tags. To run only a group:

```bash
# After starting the tests
# Digite o número do teste ou use filtros por tag
```

Tags available:
- `[power_control]` - All tests
- `[init]` - Initialization tests
- `[control]` - Control tests
- `[state]` - State tests
- `[multi]` - Multiple instances tests
- `[negative]` - Negative test cases (should fail)
- `[visual]` - Tests that require visual verification

## Important Notes

1. **Hardware Real**: These tests run on real hardware, not in simulator
2. **No Mocks**: We do not use mocks - we test real integration with the GPIO driver
3. **Visual Verification**: Some tests show the LED blinking for visual verification
4. **Safe Pins**: The tests use only safe pins (do not damage the hardware)
5. **Automatic Cleanup**: Each test performs cleanup (deinit) at the end

## Troubleshooting

### Error: "GPIO XX is not a valid output GPIO"
- **Expected** for negative tests (flash pins and input-only pins)
- **Unexpected** for GPIO 2, 4, 5 - check the hardware

### LED does not blink during visual tests
- Verify if you are using a board with an LED on GPIO 2
- Some boards may have LED on another pin
- Adjust `TEST_GPIO_LED` in the code if necessary

### Tests fail in multiple instances
- Verify if GPIO 4 and 5 are available on your board
- Adjust `TEST_GPIO_VALID_1` and `TEST_GPIO_VALID_2` if necessary

## Contributing

To add new tests:

1. Add a new `TEST_CASE()` in `power_control_test.cpp`
2. Use appropriate tags for categorization
3. Add visual delays if necessary (`visual_delay()`)
4. Always perform cleanup with `deinit()` at the end
5. Update this README with the new test case
