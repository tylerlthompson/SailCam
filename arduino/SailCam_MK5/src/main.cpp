/*
  Author: Tyler Thompson
  Date: Aug 21, 2021
  Description: SailCam MK5 Firmware
*/

#include <Arduino.h>
#include <hardware/drivers.h>
#include <web_server.h>
#include <settings/system_configuration.h>
#include <util/command_parser.h>

int loop_counter = 0;

WebServer* web_server;
CommandParser* command_parser;

DateTime* timestamp;

HardwareDrivers* hardware_drivers;


void setup() 
{
    hardware_drivers = new HardwareDrivers();

    // initialize LED
    hardware_drivers->status_led = new StatusLED();

    // initalize serial terminal
    hardware_drivers->serial_term = new SerialTerminal(initial_hardware_serial_baud_rate);
    hardware_drivers->serial_term->debug_printf("Firmware Version: %s\r\n", firmware_version);

    // initalize camera
    //hardware_drivers->camera = new Camera();
    //hardware_drivers->serial_term->debug_println(hardware_drivers->camera->run_self_test());
    //hardware_drivers->camera->init_cam();
    //hardware_drivers->old_display->write((char*)hardware_drivers->camera->run_self_test());
    //hardware_drivers->old_display->update();

    // initialize OLED display
    //delay(200);
    hardware_drivers->old_display = new OledDisplay();
    hardware_drivers->old_display->clear();
    hardware_drivers->old_display->writef("Firmware: %s", firmware_version);
    hardware_drivers->old_display->update();

    // initialize RTC
    timestamp = new DateTime();
    hardware_drivers->system_clock = new Clock();
    *timestamp = hardware_drivers->system_clock->get_time();

    // initalize battery measurement devic
    hardware_drivers->battery_management = new BatteryManagement();

    // initialize SD card and read config.ini
    hardware_drivers->storage_controller = new Storage(timestamp);
    hardware_drivers->storage_controller->read_settings_file(hardware_drivers->serial_term);
    hardware_drivers->serial_term->reinitialize
    (
        hardware_drivers->storage_controller->get_system_configuration()->get_setting("baud_rate")->get_value_int()
    );
    hardware_drivers->serial_term->debug_println("Serial Terminal Reinitialized.");
    hardware_drivers->storage_controller->print_configuration(hardware_drivers->serial_term);

    hardware_drivers->old_display->writef("SD Card: %s", hardware_drivers->storage_controller->is_card_connected() ? "True" : "False");
    hardware_drivers->old_display->update();
    
    hardware_drivers->serial_term->debug_printf
    (
        "System Time: %02d/%02d/%04d %02d:%02d:%02d\r\n", 
        timestamp->month(),
        timestamp->day(),  
        timestamp->year(), 
        timestamp->hour(), 
        timestamp->minute(), 
        timestamp->second()
    );

    // write timestamp to oled
    hardware_drivers->old_display->write(hardware_drivers->storage_controller->get_formatted_timestamp());
    hardware_drivers->old_display->update();

    // initalize wifi
    hardware_drivers->wifi_radio = new Wifi
    (
        hardware_drivers->storage_controller->get_system_configuration()->get_setting("wifi_ssid")->get_value_str(),
        hardware_drivers->storage_controller->get_system_configuration()->get_setting("wifi_psk")->get_value_str(),
        hardware_drivers->storage_controller->get_system_configuration()->get_setting("tcp_port")->get_value_int(),
        (WIFI_MODE) hardware_drivers->storage_controller->get_system_configuration()->get_setting("wifi_mode")->get_value_int()
    );

    // initialize API command parser
    command_parser = new CommandParser(hardware_drivers);

    // initalize web server
    web_server = new WebServer(hardware_drivers, command_parser);

    hardware_drivers->serial_term->debug_println("Startup Complete!");
    hardware_drivers->old_display->write("Startup Complete!");
    hardware_drivers->old_display->update();
    hardware_drivers->status_led->blink(2, 500);

    // If in wifi client mode, wait for wifi to connect. Blink led based on successful/unsuccessful connection
    if (hardware_drivers->wifi_radio->get_mode() == WIFI_CLIENT) {
        int timeout = 0;
        hardware_drivers->serial_term->debug_print("Connecting to WiFi...");
        hardware_drivers->old_display->write("Connecting to WiFi...");
        hardware_drivers->old_display->update();
        while (hardware_drivers->wifi_radio->get_connection_status() != WL_CONNECTED && timeout < wifi_client_connect_timeout) {
            delay(500);
            timeout += 500;
            hardware_drivers->serial_term->debug_print(".");
        }
        if (hardware_drivers->wifi_radio->get_connection_status() == WL_CONNECTED) {
            hardware_drivers->status_led->info();
            hardware_drivers->serial_term->debug_println(" WiFi Connected.");
            hardware_drivers->old_display->write("WiFi Connected.");
            hardware_drivers->old_display->update();
        } else {
            hardware_drivers->status_led->error();
            hardware_drivers->serial_term->debug_println(" WiFi Failed to Connect.");
            hardware_drivers->old_display->write("WiFi Failed to Connect.");
            hardware_drivers->old_display->update();
        }
    }
    hardware_drivers->old_display->write(hardware_drivers->wifi_radio->get_ip_address().toString().c_str());
    hardware_drivers->old_display->update();
    delay(2000);
    //hardware_drivers->old_display->clear();
}  // end setup()

void loop() 
{
    hardware_drivers->old_display->update();

    // handle web server clients
    web_server->handle_client();

    // process API data on the TCP port
    hardware_drivers->wifi_radio->read_server_data();
    if (hardware_drivers->wifi_radio->new_data_available()) {
        command_parser->process_tcp_api();
    }

    // read data from the serial terminal, process API commands if needed
    hardware_drivers->serial_term->read_data();
    if (hardware_drivers->serial_term->new_data_available()) {
        hardware_drivers->serial_term->debug_printf(">%s\r\n", hardware_drivers->serial_term->get_data());
        command_parser->process_serial_terminal();
    }

    // blink an error on the status LED if WiFi disconnects, every couple seconds
    if (hardware_drivers->wifi_radio->get_mode() == WIFI_CLIENT && hardware_drivers->wifi_radio->get_connection_status() != WL_CONNECTED && loop_counter % 100000 == 0) {
        hardware_drivers->serial_term->debug_println("WARNING: WiFi Not Connected!");
        hardware_drivers->old_display->clear();
        hardware_drivers->old_display->write("WiFi Not Connected!");
        hardware_drivers->status_led->error();
    }

    loop_counter++;
}  // end loop()
