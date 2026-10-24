#include <TinyFaultNvs.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible<TinyFaultNvsStorage>::value, "NVS handle is owned");
static_assert(!std::is_copy_assignable<TinyFaultNvsStorage>::value, "NVS handle is owned");
static_assert(!std::is_move_constructible<TinyFaultNvsStorage>::value, "Storage must not move");
static_assert(!std::is_copy_constructible<TinyFaultRecorder<4>>::value,
              "Recorder buffer must not be aliased by copying");
static_assert(!std::is_move_constructible<TinyFaultRecorder<4>>::value,
              "Recorder buffer must not be aliased by moving");

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            std::abort();                                                                          \
    } while (0)

struct FakeNvs {
    std::map<std::string, std::map<std::string, std::vector<uint8_t>>> committed;
    std::map<nvs_handle_t, std::string> handles;
    std::map<nvs_handle_t, std::map<std::string, std::vector<uint8_t>>> staged;
    nvs_handle_t next_handle = 1;
    esp_err_t open_error = ESP_OK;
    esp_err_t read_error = ESP_OK;
    esp_err_t write_error = ESP_OK;
    esp_err_t commit_error = ESP_OK;
    bool apply_before_commit_error = false;
    unsigned open_calls = 0;
    unsigned close_calls = 0;
} fake;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle) {
    ++fake.open_calls;
    if (fake.open_error != ESP_OK)
        return fake.open_error;
    CHECK(mode == NVS_READWRITE);
    *handle = fake.next_handle++;
    fake.handles[*handle] = name;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    CHECK(fake.handles.erase(handle) == 1u);
    fake.staged.erase(handle);
    ++fake.close_calls;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length) {
    if (fake.read_error != ESP_OK)
        return fake.read_error;
    const auto found_handle = fake.handles.find(handle);
    if (found_handle == fake.handles.end())
        return ESP_ERR_INVALID_STATE;
    const auto found = fake.committed[found_handle->second].find(key);
    if (found == fake.committed[found_handle->second].end())
        return ESP_ERR_NVS_NOT_FOUND;
    if (*length < found->second.size()) {
        *length = found->second.size();
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    *length = found->second.size();
    std::memcpy(out, found->second.data(), *length);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length) {
    if (fake.write_error != ESP_OK)
        return fake.write_error;
    if (fake.handles.find(handle) == fake.handles.end())
        return ESP_ERR_INVALID_STATE;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    fake.staged[handle][key] = std::vector<uint8_t>(bytes, bytes + length);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    const auto found = fake.handles.find(handle);
    if (found == fake.handles.end())
        return ESP_ERR_INVALID_STATE;
    if (fake.commit_error == ESP_OK || fake.apply_before_commit_error) {
        for (const auto &entry : fake.staged[handle])
            fake.committed[found->second][entry.first] = entry.second;
        fake.staged[handle].clear();
    }
    return fake.commit_error;
}

static void resetFake() { fake = FakeNvs{}; }

// The callbacks used by the pre-M3 ESP32 examples, retained here for compatibility testing.
static int oldReadSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
    nvs_handle_t *handle = static_cast<nvs_handle_t *>(context);
    char key[8];
    std::snprintf(key, sizeof(key), "s%u", static_cast<unsigned>(slot));
    size_t length = TF_RECORD_SIZE;
    esp_err_t result = nvs_get_blob(*handle, key, out, &length);
    if (result == ESP_ERR_NVS_NOT_FOUND)
        return 0;
    return result == ESP_OK && length == TF_RECORD_SIZE ? 1 : -1;
}

static int oldWriteSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
    nvs_handle_t *handle = static_cast<nvs_handle_t *>(context);
    char key[8];
    std::snprintf(key, sizeof(key), "s%u", static_cast<unsigned>(slot));
    if (nvs_set_blob(*handle, key, data, TF_RECORD_SIZE) != ESP_OK)
        return -1;
    return nvs_commit(*handle) == ESP_OK ? 1 : -1;
}

static void testNamesAndOpen() {
    resetFake();
    TinyFaultNvsStorage storage;
    CHECK(storage.open(nullptr) == ESP_ERR_NVS_INVALID_NAME);
    CHECK(storage.open("") == ESP_ERR_NVS_INVALID_NAME);
    CHECK(storage.open("1234567890123456") == ESP_ERR_NVS_INVALID_NAME);
    CHECK(fake.open_calls == 0u);
    fake.open_error = ESP_ERR_NVS_TEST_IO;
    CHECK(storage.open("events") == ESP_ERR_NVS_TEST_IO);
    CHECK(!storage.isOpen());
    fake.open_error = ESP_OK;
    CHECK(storage.open("events") == ESP_OK);
    CHECK(storage.open("again") == ESP_ERR_INVALID_STATE);
    storage.close();
    CHECK(fake.close_calls == 1u);
    CHECK(storage.open("123456789012345") == ESP_OK);
}

static void testRecoveryAndLifetime() {
    resetFake();
    TinyFaultNvsStorage storage;
    TinyFaultRecorder<4> recorder;
    CHECK(storage.begin(recorder) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_INVALID_STATE);
    CHECK(storage.open("events") == ESP_OK);
    CHECK(storage.begin(recorder) == TF_OK);
    CHECK(recorder.count() == 0u);
    CHECK(storage.lastError() == ESP_OK);
    CHECK(recorder.record(7, 123, 42) == TF_OK);
    CHECK(fake.committed["events"]["s1"].size() == TF_RECORD_SIZE);
    storage.close();
    CHECK(storage.open("other") == ESP_ERR_INVALID_STATE);
    CHECK(recorder.record(8, 456, 43) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_INVALID_STATE);
    CHECK(recorder.count() == 1u);
    CHECK(storage.open("events") == ESP_ERR_INVALID_STATE);
    CHECK(fake.committed["other"].empty());
    TinyFaultNvsStorage reopened;
    CHECK(reopened.open("events") == ESP_OK);
    TinyFaultRecorder<4> recovered;
    CHECK(reopened.begin(recovered) == TF_OK);
    CHECK(recovered.count() == 1u);
    tf_event event;
    CHECK(recovered.get(0, event));
    CHECK(event.sequence == 1u && event.id == 7u && event.value == 123u);
    CHECK(reopened.open("other") == ESP_ERR_INVALID_STATE);
}

static void testPreviousBackendAndCapacity() {
    resetFake();
    nvs_handle_t previous_handle = 0;
    CHECK(nvs_open("tinyfault-m2", NVS_READWRITE, &previous_handle) == ESP_OK);
    tf_event previous_buffer[4];
    tf_recorder previous_recorder;
    tf_storage previous_storage = {&previous_handle, oldReadSlot, oldWriteSlot};
    CHECK(tf_recorder_init(&previous_recorder, previous_buffer, 4, &previous_storage) == TF_OK);
    CHECK(tf_record(&previous_recorder, 21, 99, 1234) == TF_OK);
    nvs_close(previous_handle);
    TinyFaultNvsStorage storage;
    CHECK(storage.open("tinyfault-m2") == ESP_OK);
    TinyFaultRecorder<4> recorder;
    CHECK(storage.begin(recorder) == TF_OK);
    tf_event event;
    CHECK(recorder.get(0, event) && event.value == 99u);
    for (unsigned i = 2; i <= 4; ++i)
        CHECK(recorder.record(21, i, i) == TF_OK);
    TinyFaultRecorder<2> changed_capacity;
    CHECK(storage.begin(changed_capacity) == TF_OK);
    CHECK(changed_capacity.count() == 1u);
    CHECK(changed_capacity.get(0, event) && event.sequence == 4u);
    storage.close();
    TinyFaultNvsStorage separate_storage;
    CHECK(separate_storage.open("different") == ESP_OK);
    TinyFaultRecorder<4> separate;
    CHECK(separate_storage.begin(separate) == TF_OK && separate.count() == 0u);
}

static void testFailures() {
    resetFake();
    TinyFaultNvsStorage storage;
    CHECK(storage.open("failures") == ESP_OK);
    TinyFaultRecorder<4> recorder;
    fake.read_error = ESP_ERR_NVS_TEST_IO;
    CHECK(storage.begin(recorder) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_NVS_TEST_IO);
    fake.read_error = ESP_OK;
    fake.committed["failures"]["s1"] = {1u, 2u, 3u};
    CHECK(storage.begin(recorder) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_NVS_INVALID_LENGTH);
    fake.committed["failures"].clear();
    CHECK(storage.begin(recorder) == TF_OK);
    fake.write_error = ESP_ERR_NVS_TEST_IO;
    CHECK(recorder.record(1, 2, 3) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_NVS_TEST_IO && recorder.count() == 0u);
    fake.write_error = ESP_OK;
    fake.commit_error = ESP_ERR_NVS_TEST_IO;
    CHECK(recorder.record(1, 2, 3) == TF_STORAGE_ERROR);
    CHECK(storage.lastError() == ESP_ERR_NVS_TEST_IO && recorder.count() == 0u);
    fake.apply_before_commit_error = true;
    CHECK(recorder.record(1, 2, 3) == TF_STORAGE_ERROR);
    CHECK(recorder.count() == 0u);
    TinyFaultRecorder<4> recovered;
    CHECK(storage.begin(recovered) == TF_OK && recovered.count() == 1u);
}

static void testCallbackLease() {
    resetFake();
    TinyFaultNvsStorage storage;
    CHECK(storage.open("lease") == ESP_OK);
    tf_storage callbacks = storage.callbacks();
    storage.close();
    CHECK(storage.open("other") == ESP_ERR_INVALID_STATE);
    uint8_t bytes[TF_RECORD_SIZE] = {};
    CHECK(callbacks.write(callbacks.context, 0, bytes) < 0);
    CHECK(fake.committed["other"].empty());
}

int main() {
    testNamesAndOpen();
    testRecoveryAndLifetime();
    testPreviousBackendAndCapacity();
    testFailures();
    testCallbackLease();
}
