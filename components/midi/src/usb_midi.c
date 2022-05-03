#include "usb_midi.h"
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define USB_MIDI_INVOKE_CALLBACK(callbacks, name, ...) \
    if ((callbacks)->name) { \
        (callbacks)->name(__VA_ARGS__); \
    }

static const char *TAG = "usb_midi";


static void usb_midi_sysex_reset(usb_midi_t *usb_midi) {
    // clear the buffer by resetting the length
    usb_midi->sysex_len = 0;
}

static void usb_midi_sysex_parse(usb_midi_t *usb_midi, uint8_t *data, size_t len) {
    for (size_t i = 0; i < len && usb_midi->sysex_len < USB_MIDI_SYSEX_MAX_LEN; i++, usb_midi->sysex_len++) {
        // store the incoming byte
        usb_midi->sysex_buffer[usb_midi->sysex_len] = data[i];

        // if we've reached the stop byte, invoke the callback
        if (data[i] == 0xf7) {
            USB_MIDI_INVOKE_CALLBACK(&usb_midi->config.callbacks,
                sysex,
                usb_midi->sysex_buffer,
                usb_midi->sysex_len);
            usb_midi_sysex_reset(usb_midi);
        }
    }
}

static void usb_midi_handle_data_in(usb_midi_t *usb_midi, uint8_t *data, int len) {
    usb_midi_callbacks_t *callbacks = &usb_midi->config.callbacks;

    printf("raw data in: ");
    for (int i = 0; i < len; i++) printf("%02x ", data[i]);
    printf("\n");

    // validate mininum length
    if (len < 1) return;

    // get cable number and code index number
    //uint8_t cn = data[0] >> 4;
    uint8_t cin = data[0] & 0x0F;

    // extract the midi message and length
    uint8_t *msg = data + 1;
    int msg_len = len - 1;

    switch (cin) {
        case USB_MIDI_CIN_MISC:
            ESP_LOGE(TAG, "misc messages unimplemented");

            break;
        case USB_MIDI_CIN_CABLE_EVENT:
            ESP_LOGE(TAG, "cable event messages unimplemented");

            break;
        case USB_MIDI_CIN_SYSCOM_2:
            if (msg_len < USB_MIDI_CIN_SYSCOM_2_LEN) return;

            ESP_LOGE(TAG, "syscom 2 messages unimplemented");

            break;
        case USB_MIDI_CIN_SYSCOM_3:
            if (msg_len < USB_MIDI_CIN_SYSCOM_3_LEN) return;

            ESP_LOGE(TAG, "syscom 3 messages unimplemented");

            break;
        case USB_MIDI_CIN_SYSEX_START:
            if (msg_len < USB_MIDI_CIN_SYSEX_START_LEN) return;

            usb_midi_sysex_parse(usb_midi, msg, msg_len);

            break;
        case USB_MIDI_CIN_SYSEX_END_1_SYSCOM_1:
            if (msg_len < USB_MIDI_CIN_SYSEX_END_1_SYSCOM_1_LEN) return;

            usb_midi_sysex_parse(usb_midi, msg, 1);
            usb_midi_sysex_reset(usb_midi);

            break;
        case USB_MIDI_CIN_SYSEX_END_2:
            if (msg_len < USB_MIDI_CIN_SYSEX_END_2_LEN) return;

            usb_midi_sysex_parse(usb_midi, msg, 2);
            usb_midi_sysex_reset(usb_midi);

            break;
        case USB_MIDI_CIN_SYSEX_END_3:
            if (msg_len < USB_MIDI_CIN_SYSEX_END_3_LEN) return;

            usb_midi_sysex_parse(usb_midi, msg, 3);
            usb_midi_sysex_reset(usb_midi);

            break;
        case USB_MIDI_CIN_NOTE_OFF:
            if (msg_len < USB_MIDI_CIN_NOTE_OFF_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, note_off, msg[0] & 0x0f, msg[1], msg[2]);

            break;
        case USB_MIDI_CIN_NOTE_ON:
            if (msg_len < USB_MIDI_CIN_NOTE_ON_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, note_on, msg[0] & 0x0f, msg[1], msg[2]);

            break;
        case USB_MIDI_CIN_POLY_KEY_PRESSURE:
            if (msg_len < USB_MIDI_CIN_POLY_KEY_PRESSURE_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, poly_key_pressure, msg[0] & 0x0f, msg[1], msg[2]);

            break;
        case USB_MIDI_CIN_CONTROL_CHANGE:
            if (msg_len < USB_MIDI_CIN_CONTROL_CHANGE_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, control_change, msg[0] & 0x0f, msg[1], msg[2]);

            break;
        case USB_MIDI_CIN_PROGRAM_CHANGE:
            if (msg_len < USB_MIDI_CIN_PROGRAM_CHANGE_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, program_change, msg[0] & 0x0f, msg[1]);

            break;
        case USB_MIDI_CIN_CHANNEL_PRESSURE:
            if (msg_len < USB_MIDI_CIN_CHANNEL_PRESSURE_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, channel_pressure, msg[0] & 0x0f, msg[1]);

            break;
        case USB_MIDI_CIN_PITCH_BEND:
            if (msg_len < USB_MIDI_CIN_PITCH_BEND_LEN) return;

            USB_MIDI_INVOKE_CALLBACK(callbacks, pitch_bend, msg[0] & 0x0f, (msg[1] | msg[2] << 7) - 8192);

            break;
        case USB_MIDI_CIN_BYTE:
            if (msg_len < USB_MIDI_CIN_BYTE_LEN) return;

            ESP_LOGE(TAG, "single byte messages unimplemented");

            break;
        default:

            ESP_LOGE(TAG, "unknown code index number %d", cin);

            break;
    }
}

static void usb_midi_data_in_callback(usb_transfer_t *transfer) {
    usb_midi_t *usb_midi = (usb_midi_t *) transfer->context;

    // handle the incoming data
    usb_midi_handle_data_in(usb_midi, transfer->data_buffer, transfer->actual_num_bytes);

    // continue polling if the device hasn't been closed yet
    // usb_host_transfer_submit might return an invalid state error,
    // if the device has already disconnected, we can ignore that
    if (usb_midi->device) {
        usb_host_transfer_submit(usb_midi->data_in);
    }
}

static void usb_midi_data_out_callback(usb_transfer_t *transfer) {
    //usb_midi_t *usb_midi = (usb_midi_t *) transfer->context;
    ESP_LOGI(TAG, "data out callback");
}

static esp_err_t usb_midi_open_device(usb_midi_t *usb_midi, uint8_t address) {
    esp_err_t err;
    usb_device_info_t info;
    const usb_config_desc_t *descriptor;
    const usb_standard_desc_t *d;
    int offset = 0;

    const usb_intf_desc_t *interface = NULL;
    const usb_ep_desc_t *data_in = NULL;
    const usb_ep_desc_t *data_out = NULL;

    // open the device
    usb_midi->device_address = address;
    ESP_LOGI(TAG, "opening device at 0x%02x", usb_midi->device_address);
    err = usb_host_device_open(usb_midi->client, usb_midi->device_address, &usb_midi->device);
    if (err != ESP_OK) return err;

    // get the device info
    ESP_LOGI(TAG, "getting device information");
    err = usb_host_device_info(usb_midi->device, &info);
    if (err != ESP_OK) return err;

    // get the device descriptor
    ESP_LOGI(TAG, "getting device descriptor");
    err = usb_host_get_device_descriptor(usb_midi->device, &usb_midi->device_descriptor);
    if (err != ESP_OK) return err;

    // get the configuration descriptor
    assert(usb_midi->device != NULL);
    ESP_LOGI(TAG, "getting configuration descriptor");
    usb_host_get_active_config_descriptor(usb_midi->device, &descriptor);
    if (err != ESP_OK) return err;

    // scan the configuration descriptor for interfaces and endpoints
    for (d = (usb_standard_desc_t *) descriptor; d != NULL; d = usb_parse_next_descriptor(d, descriptor->wTotalLength, &offset)) {
        switch (d->bDescriptorType) {
            case USB_W_VALUE_DT_INTERFACE:;
                const usb_intf_desc_t *i = (const usb_intf_desc_t *) d;

                // validate the device class
                if (i->bInterfaceClass != USB_CLASS_AUDIO) break;
                if (i->bInterfaceSubClass != USB_SUBCLASS_MIDISTREAMING) break;

                // mark this device as a midi device and reset the endpoints
                interface = i;
                data_in = NULL;
                data_out = NULL;

                break;
            case USB_W_VALUE_DT_ENDPOINT:;
                const usb_ep_desc_t *e = (const usb_ep_desc_t *) d;

                // only bulk transfer is supported
                if (!(e->bmAttributes & USB_TRANSFER_TYPE_BULK)) break;

                // store the endpoints
                if (e->bEndpointAddress & 0x80) {
                    data_in = e;
                } else {
                    data_out = e;
                }

                break;
            default:
                break;
        }
    }

    // check if we've got all the necessary descriptors
    if (!interface || !data_in || !data_out) {
        ESP_LOGE(TAG, "not a valid MIDI device");
        return ESP_FAIL;
    }
    
    // claim the interface
    ESP_LOGI(TAG, "claiming interface");
    err = usb_host_interface_claim(usb_midi->client,
        usb_midi->device,
        interface->bInterfaceNumber,
        interface->bAlternateSetting);
    if (err != ESP_OK) return err;

    // store the interface and create transfers for each endpoint
    usb_midi->interface = interface;

    size_t data_in_size = USB_MIDI_PACKET_MAX_LEN;
    err = usb_host_transfer_alloc(data_in_size, 0, &usb_midi->data_in);
    if (err != ESP_OK) return err;

    usb_midi->data_in->device_handle = usb_midi->device;
    usb_midi->data_in->bEndpointAddress = data_in->bEndpointAddress;
    usb_midi->data_in->callback = usb_midi_data_in_callback;
    usb_midi->data_in->context = (void *) usb_midi;
    usb_midi->data_in->num_bytes = data_in_size;
    
    size_t data_out_size = USB_MIDI_PACKET_MAX_LEN;
    err = usb_host_transfer_alloc(data_out_size, 0, &usb_midi->data_out);
    if (err != ESP_OK) return err;

    usb_midi->data_out->device_handle = usb_midi->device;
    usb_midi->data_out->bEndpointAddress = data_out->bEndpointAddress;
    usb_midi->data_out->callback = usb_midi_data_out_callback;
    usb_midi->data_out->context = (void *) usb_midi;

    // reset the sysex length
    usb_midi->sysex_len = 0;

    // invoke the connected callback
    USB_MIDI_INVOKE_CALLBACK(&usb_midi->config.callbacks, connected, usb_midi->device_descriptor);

    // start input polling
    err = usb_host_transfer_submit(usb_midi->data_in);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "midi device initialized (data in size: %d, data out size: %d)", data_in_size, data_out_size);

    return ESP_OK;
}

static void usb_midi_close_device(usb_midi_t *usb_midi) {
    assert(usb_midi->device != NULL);

    // invoke the disconnected callback if the interface was actually claimed
    if (usb_midi->interface) {
        USB_MIDI_INVOKE_CALLBACK(&usb_midi->config.callbacks, disconnected, usb_midi->device_descriptor);
    }

    // free the endpoints and transfers
    if (usb_midi->data_in) {
        ESP_ERROR_CHECK(usb_host_endpoint_halt(usb_midi->device, usb_midi->data_in->bEndpointAddress));
        ESP_ERROR_CHECK(usb_host_endpoint_flush(usb_midi->device, usb_midi->data_in->bEndpointAddress));
        ESP_ERROR_CHECK(usb_host_transfer_free(usb_midi->data_in));
        usb_midi->data_in = NULL;
    }
    if (usb_midi->data_out) {
        ESP_ERROR_CHECK(usb_host_endpoint_halt(usb_midi->device, usb_midi->data_out->bEndpointAddress));
        ESP_ERROR_CHECK(usb_host_endpoint_flush(usb_midi->device, usb_midi->data_out->bEndpointAddress));
        ESP_ERROR_CHECK(usb_host_transfer_free(usb_midi->data_out));
        usb_midi->data_out = NULL;
    }

    // release the interface
    if (usb_midi->interface) {
        ESP_LOGI(TAG, "releasing interface");
        ESP_ERROR_CHECK(usb_host_interface_release(usb_midi->client, usb_midi->device, usb_midi->interface->bInterfaceNumber));
        usb_midi->interface = NULL;
    }

    ESP_LOGI(TAG, "closing device 0x%02x", usb_midi->device_address);
    ESP_ERROR_CHECK(usb_host_device_close(usb_midi->client, usb_midi->device));

    usb_midi->device = NULL;
    usb_midi->device_address = 0;
}

static void usb_midi_client_event_callback(const usb_host_client_event_msg_t *msg, void *arg) {
    usb_midi_t *usb_midi = (usb_midi_t *) arg;

    switch (msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (usb_midi->device_address != 0) break;

            // open new device
            usb_midi_open_device(usb_midi, msg->new_dev.address);
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            if (usb_midi->device == NULL) break;

            // close device
            usb_midi_close_device(usb_midi);
            break;
        default:
            ESP_LOGE(TAG, "unknown client event %d", msg->event);
            break;
    }
}

void usb_midi_driver_task(void *arg) {
    usb_midi_t *usb_midi = (usb_midi_t *) arg;

    // create the client usb_midi
    ESP_LOGI(TAG, "registering client");
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usb_midi_client_event_callback,
            .callback_arg = (void *) usb_midi
        }
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &usb_midi->client));

    // main event loop
    while (1) {
        usb_host_client_handle_events(usb_midi->client, portMAX_DELAY);
    }

    // cleanup
    ESP_LOGI(TAG, "deregistering client");
    ESP_ERROR_CHECK(usb_host_client_deregister(usb_midi->client));

    vTaskDelete(NULL);
}

esp_err_t usb_midi_init(const usb_midi_config_t *config, usb_midi_t *usb_midi) {
    memset(usb_midi, 0, sizeof(usb_midi_t));

    // store the config
    memcpy(&usb_midi->config, config, sizeof(usb_midi_config_t));

    // link the task function and argument
    usb_midi->driver_config.task = usb_midi_driver_task;
    usb_midi->driver_config.arg = (void *) usb_midi;

    return ESP_OK;
}

static esp_err_t usb_midi_send_short_message(usb_midi_t *usb_midi, uint8_t command, uint8_t channel, uint8_t data1, uint8_t data2) {
    if (command < 0x08 || command > 0x0F) return ESP_ERR_INVALID_ARG;
    if (channel > 0x0F) return ESP_ERR_INVALID_ARG;
    if (data1 > 0x7F) return ESP_ERR_INVALID_ARG;
    if (data2 > 0x7F) return ESP_ERR_INVALID_ARG;

    // write the message
    uint8_t *buffer = usb_midi->data_out->data_buffer;
    uint8_t cn = 0; // TODO choose correct cable number
    buffer[0] = cn << 4 | command; // cable number + cid (command)
    buffer[1] = command << 4 | channel; // midi command + channel
    buffer[2] = data1;
    buffer[3] = data2;

    // submit the transfer
    usb_midi->data_out->num_bytes = 4;
    usb_midi->data_out->timeout_ms = 10000;

    /* printf("raw data out: ");
    for (int i = 0; i < usb_midi->data_out->num_bytes; i++) printf("%02x ", usb_midi->data_out->data_buffer[i]);
    printf("\n"); */

    return usb_host_transfer_submit(usb_midi->data_out);
}

esp_err_t usb_midi_send_note_off(usb_midi_t *usb_midi, uint8_t channel, uint8_t note, uint8_t velocity) {
    return usb_midi_send_short_message(usb_midi, 0x08, channel, note, velocity);
}

esp_err_t usb_midi_send_note_on(usb_midi_t *usb_midi, uint8_t channel, uint8_t note, uint8_t velocity) {
    return usb_midi_send_short_message(usb_midi, 0x09, channel, note, velocity);
}