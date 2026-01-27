import time
import pytest
from pytest_embedded import Dut

@pytest.mark.esp32
@pytest.mark.esp32s3
@pytest.mark.esp32c3
@pytest.mark.qemu
@pytest.mark.linux
def test_gpio_validator(dut: Dut) -> None:
    # Wait for the application to reach app_main
    dut.expect_exact('Calling app_main()', timeout=30)
    
    # Wait for the menu prompt
    dut.expect_exact('Press ENTER to see the list of tests.', timeout=30)
    
    # Retry loop to trigger menu (QEMU can be flaky with input)
    for _ in range(5):
        dut.write('\r\n')
        time.sleep(1)
        try:
            # We look for a unique part of the menu prompt
            # dut.expect_exact('Enter test name or number', timeout=5)
            dut.expect_exact('the test menu, pick your combo', timeout=5)
            break
        except Exception:
            continue
    else:
        # Final attempt
        dut.write('\n')
        dut.expect_exact('Enter test name or number', timeout=10)
    
    # Run all tests (selection '*')
    dut.write('*')
    
    # Unity output pattern for test summary: "X Tests Y Failures Z Ignored"
    # Example: "6 Tests 0 Failures 0 Ignored"
    match = dut.expect(r'(\d+) Tests (\d+) Failures (\d+) Ignored', timeout=30)
    
    tests_run = int(match.group(1))
    failures = int(match.group(2))
    ignored = int(match.group(3))
    
    print(f'Unity Results: {tests_run} Tests, {failures} Failures, {ignored} Ignored')
    
    assert tests_run > 0, 'No tests were executed!'
    assert failures == 0, f'Unity tests failed with {failures} failures'

if __name__ == '__main__':
    pytest.main([__file__])
