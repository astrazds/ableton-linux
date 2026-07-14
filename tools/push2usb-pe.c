/*
 * Safe PE probe for the Wine builtin libusb-1.0 bridge.
 *
 * This program dynamically resolves the exact API used by Ableton's
 * Push2DisplayProcess.  It never detaches a driver, changes configuration,
 * resets the device, or writes to the display OUT endpoint.
 */

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define PUSH2_VENDOR_ID 0x2982
#define PUSH2_PRODUCT_ID 0x1967
#define PUSH2_INTERFACE 0
#define PUSH2_IN_ENDPOINT 0x81
#define PUSH2_TRANSFER_BULK 2
#define PUSH2_PACKET_SIZE 512

#define LIBUSB_TRANSFER_CANCELLED 3

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

struct libusb_device_descriptor
{
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
};

struct libusb_iso_packet_descriptor
{
    uint32_t length;
    uint32_t actual_length;
    int status;
};

struct libusb_transfer;
typedef void (WINAPI *libusb_transfer_cb_fn)(struct libusb_transfer *transfer);

struct libusb_transfer
{
    struct libusb_device_handle *dev_handle;
    uint8_t flags;
    uint8_t endpoint;
    uint8_t type;
    uint32_t timeout;
    int status;
    int length;
    int actual_length;
    libusb_transfer_cb_fn callback;
    void *user_data;
    uint8_t *buffer;
    int num_iso_packets;
    struct libusb_iso_packet_descriptor iso_packet_desc[0];
};

struct libusb_timeval
{
    int32_t tv_sec;
    int32_t tv_usec;
};

typedef struct libusb_transfer *(WINAPI *alloc_transfer_fn)(int);
typedef int (WINAPI *cancel_transfer_fn)(struct libusb_transfer *);
typedef int (WINAPI *claim_interface_fn)(struct libusb_device_handle *, int);
typedef void (WINAPI *close_fn)(struct libusb_device_handle *);
typedef const char *(WINAPI *error_name_fn)(int);
typedef void (WINAPI *exit_fn)(struct libusb_context *);
typedef void (WINAPI *free_device_list_fn)(struct libusb_device **, int);
typedef void (WINAPI *free_transfer_fn)(struct libusb_transfer *);
typedef int (WINAPI *get_device_descriptor_fn)(struct libusb_device *, struct libusb_device_descriptor *);
typedef SSIZE_T (WINAPI *get_device_list_fn)(struct libusb_context *, struct libusb_device ***);
typedef int (WINAPI *handle_events_timeout_fn)(struct libusb_context *, const struct libusb_timeval *);
typedef int (WINAPI *init_fn)(struct libusb_context **);
typedef int (WINAPI *open_fn)(struct libusb_device *, struct libusb_device_handle **);
typedef int (WINAPI *release_interface_fn)(struct libusb_device_handle *, int);
typedef int (WINAPIV *set_option_fn)(struct libusb_context *, int, ...);
typedef int (WINAPI *submit_transfer_fn)(struct libusb_transfer *);

static alloc_transfer_fn p_alloc_transfer;
static cancel_transfer_fn p_cancel_transfer;
static claim_interface_fn p_claim_interface;
static close_fn p_close;
static error_name_fn p_error_name;
static exit_fn p_exit;
static free_device_list_fn p_free_device_list;
static free_transfer_fn p_free_transfer;
static get_device_descriptor_fn p_get_device_descriptor;
static get_device_list_fn p_get_device_list;
static handle_events_timeout_fn p_handle_events_timeout;
static init_fn p_init;
static open_fn p_open;
static release_interface_fn p_release_interface;
static set_option_fn p_set_option;
static submit_transfer_fn p_submit_transfer;

static volatile LONG callback_count;
static volatile LONG callback_status;
static volatile LONG callback_actual_length;
static uint8_t input_buffer[PUSH2_PACKET_SIZE];

_Static_assert(sizeof(struct libusb_device_descriptor) == 18, "device descriptor ABI");
_Static_assert(offsetof(struct libusb_device_descriptor, bDeviceClass) == 4, "device class ABI");
_Static_assert(offsetof(struct libusb_device_descriptor, idVendor) == 8, "vendor ABI");
_Static_assert(offsetof(struct libusb_device_descriptor, idProduct) == 10, "product ABI");
_Static_assert(sizeof(struct libusb_transfer) == 64, "transfer ABI");
_Static_assert(offsetof(struct libusb_transfer, callback) == 32, "callback ABI");
_Static_assert(offsetof(struct libusb_transfer, buffer) == 48, "buffer ABI");
_Static_assert(offsetof(struct libusb_transfer, num_iso_packets) == 56, "iso count ABI");
_Static_assert(sizeof(struct libusb_timeval) == 8, "timeval ABI");

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

static void write_text(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)text_length(text), &written, NULL);
}

static void write_error(const char *stage, int error)
{
    write_text("push2-probe=fail stage=");
    write_text(stage);
    write_text(" error=");
    if (p_error_name) write_text(p_error_name(error));
    else write_text("unavailable");
    write_text("\r\n");
}

static int contains(const char *text, const char *needle)
{
    size_t i, j;

    for (i = 0; text[i]; ++i)
    {
        for (j = 0; needle[j] && text[i + j] == needle[j]; ++j) {}
        if (!needle[j]) return 1;
    }
    return 0;
}

static FARPROC load_proc(HMODULE module, const char *name, WORD ordinal)
{
    FARPROC by_name = GetProcAddress(module, name);
    FARPROC by_ordinal = GetProcAddress(module, (LPCSTR)(uintptr_t)ordinal);

    if (!by_name)
    {
        write_text("push2-probe=fail missing-export=");
        write_text(name);
        write_text("\r\n");
    }
    else if (by_name != by_ordinal)
    {
        write_text("push2-probe=fail ordinal-mismatch=");
        write_text(name);
        write_text("\r\n");
        by_name = NULL;
    }
    return by_name;
}

#define LOAD_PROC(field, name, ordinal) do { \
    p_##field = (field##_fn)load_proc(module, name, ordinal); \
    if (!p_##field) goto done; \
} while (0)

static int load_api(HMODULE module)
{
    LOAD_PROC(alloc_transfer, "libusb_alloc_transfer", 4);
    LOAD_PROC(cancel_transfer, "libusb_cancel_transfer", 10);
    LOAD_PROC(claim_interface, "libusb_claim_interface", 12);
    LOAD_PROC(close, "libusb_close", 16);
    LOAD_PROC(error_name, "libusb_error_name", 26);
    LOAD_PROC(exit, "libusb_exit", 32);
    LOAD_PROC(free_device_list, "libusb_free_device_list", 40);
    LOAD_PROC(free_transfer, "libusb_free_transfer", 50);
    LOAD_PROC(get_device_descriptor, "libusb_get_device_descriptor", 72);
    LOAD_PROC(get_device_list, "libusb_get_device_list", 74);
    LOAD_PROC(handle_events_timeout, "libusb_handle_events_timeout", 110);
    LOAD_PROC(init, "libusb_init", 120);
    LOAD_PROC(open, "libusb_open", 132);
    LOAD_PROC(release_interface, "libusb_release_interface", 140);
    LOAD_PROC(set_option, "libusb_set_option", 154);
    LOAD_PROC(submit_transfer, "libusb_submit_transfer", 161);
    return 1;

done:
    return 0;
}

static void WINAPI transfer_callback(struct libusb_transfer *transfer)
{
    callback_status = transfer->status;
    callback_actual_length = transfer->actual_length;
    InterlockedIncrement(&callback_count);
}

static int claim_once(struct libusb_device *device)
{
    struct libusb_device_handle *handle = NULL;
    int ret;

    if ((ret = p_open(device, &handle)) < 0)
    {
        write_error("open", ret);
        return 0;
    }
    if ((ret = p_claim_interface(handle, PUSH2_INTERFACE)) < 0)
    {
        write_error("claim", ret);
        p_close(handle);
        return 0;
    }
    if ((ret = p_release_interface(handle, PUSH2_INTERFACE)) < 0)
    {
        write_error("release", ret);
        p_close(handle);
        return 0;
    }
    p_close(handle);
    return 1;
}

static int run_probe(void)
{
    struct libusb_device_descriptor descriptor;
    struct libusb_device_handle *handle = NULL;
    struct libusb_transfer *transfer = NULL;
    struct libusb_device **list = NULL;
    struct libusb_device *target = NULL;
    struct libusb_timeval timeout = {0, 1000000};
    struct libusb_timeval drain = {0, 0};
    HMODULE module = NULL;
    char module_path[MAX_PATH];
    const char *command_line = GetCommandLineA();
    SSIZE_T count, i;
    int mode = -1, ret = 1, api_ret, cancel_ret = -1, event_ret = 0;
    int initialized = 0, claimed = 0, active = 0, iteration;

    if (contains(command_line, "--cancel-in")) mode = 2;
    else if (contains(command_line, "--claim")) mode = 1;
    else if (contains(command_line, "--enumerate")) mode = 0;

    if (!(module = LoadLibraryA("libusb-1.0.dll")))
    {
        write_text("push2-probe=fail stage=load-library\r\n");
        return 1;
    }
    if (!load_api(module)) goto done;

    if (GetModuleFileNameA(module, module_path, sizeof(module_path)))
    {
        write_text("libusb-module=");
        write_text(module_path);
        write_text("\r\n");
    }

    if (mode == -1)
    {
        if (!contains(p_error_name(-2), "LIBUSB_ERROR_INVALID_PARAM"))
        {
            write_text("push2-probe=fail stage=error-name-abi\r\n");
            goto done;
        }
        write_text("push2-abi=ok exports=16 name-ordinal-pairs=16\r\n");
        ret = 0;
        goto done;
    }

    if ((api_ret = p_init(NULL)) < 0)
    {
        write_error("init", api_ret);
        goto done;
    }
    initialized = 1;

    if ((api_ret = p_set_option(NULL, 0, 1)) < 0)
    {
        write_error("set-option", api_ret);
        goto done;
    }

    count = p_get_device_list(NULL, &list);
    if (count < 0)
    {
        write_error("get-device-list", (int)count);
        goto done;
    }

    for (i = 0; i < count && list[i]; ++i)
    {
        if (p_get_device_descriptor(list[i], &descriptor) < 0) continue;
        if (!descriptor.bDeviceClass && descriptor.idVendor == PUSH2_VENDOR_ID &&
            descriptor.idProduct == PUSH2_PRODUCT_ID)
        {
            target = list[i];
            break;
        }
    }
    if (!target)
    {
        write_text("push2-probe=fail stage=find-device\r\n");
        goto done;
    }

    write_text("push2-enumeration=ok 2982:1967\r\n");
    if (!mode)
    {
        ret = 0;
        goto done;
    }

    if (mode == 1)
    {
        if (!claim_once(target) || !claim_once(target)) goto done;
        write_text("push2-claim=ok repetitions=2\r\n");
        ret = 0;
        goto done;
    }

    if ((api_ret = p_open(target, &handle)) < 0)
    {
        write_error("open", api_ret);
        goto done;
    }
    if ((api_ret = p_claim_interface(handle, PUSH2_INTERFACE)) < 0)
    {
        write_error("claim", api_ret);
        goto done;
    }
    claimed = 1;

    p_free_device_list(list, 1);
    list = NULL;
    target = NULL;

    if (!(transfer = p_alloc_transfer(0)))
    {
        write_text("push2-probe=fail stage=alloc-transfer\r\n");
        goto done;
    }

    transfer->dev_handle = handle;
    transfer->endpoint = PUSH2_IN_ENDPOINT;
    transfer->type = PUSH2_TRANSFER_BULK;
    transfer->timeout = 1000;
    transfer->length = sizeof(input_buffer);
    transfer->callback = transfer_callback;
    transfer->buffer = input_buffer;

    callback_count = 0;
    callback_status = -1;
    callback_actual_length = -1;

    if ((api_ret = p_submit_transfer(transfer)) < 0)
    {
        write_error("submit", api_ret);
        goto done;
    }
    active = 1;
    cancel_ret = p_cancel_transfer(transfer);

    for (iteration = 0; iteration < 5 && !callback_count; ++iteration)
    {
        event_ret = p_handle_events_timeout(NULL, &timeout);
        if (event_ret < 0) write_error("handle-events", event_ret);
    }

    if (!callback_count)
    {
        write_text("push2-probe=fail stage=terminal-callback\r\n");
        goto done;
    }
    active = 0;

    event_ret = p_handle_events_timeout(NULL, &drain);
    if (event_ret < 0)
    {
        write_error("drain-events", event_ret);
        goto done;
    }
    if (cancel_ret < 0)
    {
        write_error("cancel", cancel_ret);
        goto done;
    }
    if (callback_count != 1 || callback_status != LIBUSB_TRANSFER_CANCELLED || callback_actual_length != 0)
    {
        write_text("push2-probe=fail stage=cancel-result\r\n");
        goto done;
    }

    write_text("push2-cancel=ok callbacks=1 status=cancelled actual_length=0\r\n");
    ret = 0;

done:
    if (active)
    {
        write_text("push2-probe=abandon-active-transfer\r\n");
        return 1;
    }
    if (transfer) p_free_transfer(transfer);
    if (list) p_free_device_list(list, 1);
    if (claimed) p_release_interface(handle, PUSH2_INTERFACE);
    if (handle) p_close(handle);
    if (initialized) p_exit(NULL);
    if (module) FreeLibrary(module);
    return ret;
}

void mainCRTStartup(void)
{
    ExitProcess((UINT)run_probe());
}
