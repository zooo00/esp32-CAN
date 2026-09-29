// USB console. Only command: "stub on|off".
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "node_a.h"

static int cmd_stub(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "on") == 0) {
        stub_set_enabled(true);
    } else if (argc == 2 && strcmp(argv[1], "off") == 0) {
        stub_set_enabled(false);
    } else {
        printf("stub is %s. Usage: stub on|off\n", stub_enabled() ? "on" : "off");
    }
    return 0;
}

void console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "node_a>";
    esp_console_dev_usb_serial_jtag_config_t hw_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_cfg, &repl_cfg, &repl));

    const esp_console_cmd_t cmd = {
        .command = "stub",
        .help = "Send fake frames instead of real CAN (desk testing): stub on|off",
        .func = cmd_stub,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
