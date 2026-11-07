#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "uart_transport.h"
#include "usb_host.h"

int main(void)
{
    set_sys_clock_khz(120000, true);
    uart_transport_init();
    usb_host_diagnostics_init();

    tud_init(0);
    tud_cdc_write_str("TORII: XIAO RP2040 HID-to-UART, protocol v1\r\n");
    tud_cdc_write_str("CDC diagnostics: USB enumeration and HID reports\r\n");
    tud_cdc_write_flush();
    uart_transport_service();

    multicore_launch_core1(usb_host_task);

    while (true) {
        tud_task();
        usb_host_diagnostics_service();
        tud_cdc_write_flush();
        uart_transport_service();
        tight_loop_contents();
    }
}
