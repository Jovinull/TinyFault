#ifndef TINYFAULT_NVS_H
#define TINYFAULT_NVS_H

#if defined(ARDUINO_ARCH_ESP32)

#include "TinyFault.h"

#include <nvs.h>
#include <stdio.h>
#include <string.h>

// Keep this object alive and open for as long as the recorder uses it.
class TinyFaultNvsStorage {
    nvs_handle_t handle_{};
    bool open_ = false;
    esp_err_t last_error_ = ESP_OK;

    static void slotKey(size_t slot, char key[16]) {
        snprintf(key, 16, "s%u", static_cast<unsigned>(slot));
    }

    static int readSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
        TinyFaultNvsStorage *self = static_cast<TinyFaultNvsStorage *>(context);
        if (!self->open_) {
            self->last_error_ = ESP_ERR_INVALID_STATE;
            return -1;
        }
        char key[16];
        slotKey(slot, key);
        size_t length = TF_RECORD_SIZE;
        self->last_error_ = nvs_get_blob(self->handle_, key, out, &length);
        if (self->last_error_ == ESP_ERR_NVS_NOT_FOUND) {
            self->last_error_ = ESP_OK;
            return 0;
        }
        if (self->last_error_ != ESP_OK)
            return -1;
        if (length != TF_RECORD_SIZE) {
            self->last_error_ = ESP_ERR_NVS_INVALID_LENGTH;
            return -1;
        }
        return 1;
    }

    static int writeSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
        TinyFaultNvsStorage *self = static_cast<TinyFaultNvsStorage *>(context);
        if (!self->open_) {
            self->last_error_ = ESP_ERR_INVALID_STATE;
            return -1;
        }
        char key[16];
        slotKey(slot, key);
        self->last_error_ = nvs_set_blob(self->handle_, key, data, TF_RECORD_SIZE);
        if (self->last_error_ != ESP_OK)
            return -1;
        self->last_error_ = nvs_commit(self->handle_);
        return self->last_error_ == ESP_OK ? 1 : -1;
    }

  public:
    TinyFaultNvsStorage() = default;
    ~TinyFaultNvsStorage() { close(); }
    TinyFaultNvsStorage(const TinyFaultNvsStorage &) = delete;
    TinyFaultNvsStorage &operator=(const TinyFaultNvsStorage &) = delete;

    esp_err_t open(const char *name) {
        if (open_)
            return last_error_ = ESP_ERR_INVALID_STATE;
        if (!name || !name[0] || strlen(name) > 15u)
            return last_error_ = ESP_ERR_NVS_INVALID_NAME;
        nvs_handle_t handle{};
        last_error_ = nvs_open(name, NVS_READWRITE, &handle);
        if (last_error_ == ESP_OK) {
            handle_ = handle;
            open_ = true;
        }
        return last_error_;
    }

    void close() {
        if (open_)
            nvs_close(handle_);
        open_ = false;
    }

    bool isOpen() const { return open_; }
    esp_err_t lastError() const { return last_error_; }

    tf_storage callbacks() {
        const tf_storage storage = {this, readSlot, writeSlot};
        return storage;
    }

    template <size_t Capacity> tf_status begin(TinyFaultRecorder<Capacity> &recorder) {
        if (!open_) {
            last_error_ = ESP_ERR_INVALID_STATE;
            return TF_STORAGE_ERROR;
        }
        const tf_storage storage = callbacks();
        return recorder.begin(&storage);
    }
};

#endif // ARDUINO_ARCH_ESP32
#endif // TINYFAULT_NVS_H
