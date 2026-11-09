#include <stddef.h>
#include <stdint.h>

typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;

typedef struct {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} EFI_GUID;

typedef struct TEXT_OUTPUT TEXT_OUTPUT;
struct TEXT_OUTPUT {
  void *reset;
  EFI_STATUS (*output_string)(TEXT_OUTPUT *, const CHAR16 *);
  void *test_string;
  void *query_mode;
  void *set_mode;
  EFI_STATUS (*set_attribute)(TEXT_OUTPUT *, UINTN);
  EFI_STATUS (*clear_screen)(TEXT_OUTPUT *);
};

typedef struct TEXT_INPUT TEXT_INPUT;
struct TEXT_INPUT {
  void *reset;
  EFI_STATUS (*read_key)(TEXT_INPUT *, void *);
  EFI_EVENT wait_for_key;
};

typedef struct {
  uint8_t prefix[0x60];
  EFI_STATUS (*wait_for_event)(UINTN, EFI_EVENT *, UINTN *);
  uint8_t middle[0x140 - 0x68];
  EFI_STATUS (*locate_protocol)(const EFI_GUID *, void *, void **);
} BOOT_SERVICES;

typedef struct {
  uint8_t prefix[0x48];
  EFI_STATUS (*get_variable)(const CHAR16 *, const EFI_GUID *, uint32_t *,
                             UINTN *, void *);
} RUNTIME_SERVICES;

typedef struct {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t crc32;
  uint32_t reserved;
  CHAR16 *firmware_vendor;
  uint32_t firmware_revision;
  EFI_HANDLE console_in_handle;
  TEXT_INPUT *console_in;
  EFI_HANDLE console_out_handle;
  TEXT_OUTPUT *console_out;
  EFI_HANDLE console_error_handle;
  TEXT_OUTPUT *console_error;
  RUNTIME_SERVICES *runtime;
  BOOT_SERVICES *boot;
} SYSTEM_TABLE;

typedef struct {
  EFI_STATUS (*read_input)(uint32_t, uint32_t, uint32_t *);
} FUJITSU_GPIO_READ_PROTOCOL;

typedef struct CONSOLE_CONTROL CONSOLE_CONTROL;
struct CONSOLE_CONTROL {
  void *get_mode;
  EFI_STATUS (*set_mode)(CONSOLE_CONTROL *, uint32_t);
};

_Static_assert(sizeof(void *) == 8, "Requires x64");
_Static_assert(sizeof(wchar_t) == 2, "Requires UTF16 wchar_t");
_Static_assert(offsetof(SYSTEM_TABLE, console_out) == 0x40, "ConOut offset");
_Static_assert(offsetof(SYSTEM_TABLE, runtime) == 0x58, "Runtime offset");
_Static_assert(offsetof(SYSTEM_TABLE, boot) == 0x60, "Boot services offset");
_Static_assert(offsetof(BOOT_SERVICES, wait_for_event) == 0x60, "Wait offset");
_Static_assert(offsetof(BOOT_SERVICES, locate_protocol) == 0x140, "Locate offset");
_Static_assert(offsetof(RUNTIME_SERVICES, get_variable) == 0x48, "GetVar offset");

#define ERROR_BIT UINT64_C(0x8000000000000000)
#define EFI_COMPROMISED_DATA (ERROR_BIT | 33)
#define EFI_UNSUPPORTED (ERROR_BIT | 3)
#define TEXT(s) ((const CHAR16 *)L##s)

static EFI_STATUS print(TEXT_OUTPUT *out, const CHAR16 *text) {
  return out->output_string(out, text);
}

static EFI_STATUS print_hex(TEXT_OUTPUT *out, uint64_t value) {
  CHAR16 buffer[19] = {'0', 'x'};
  static const CHAR16 digits[] = L"0123456789ABCDEF";
  for (unsigned i = 0; i < 16; ++i)
    buffer[2 + i] = digits[(value >> (60 - 4 * i)) & 15];
  buffer[18] = 0;
  return print(out, buffer);
}

static EFI_STATUS line(TEXT_OUTPUT *out, const CHAR16 *label, uint64_t value) {
  EFI_STATUS status = print(out, label);
  if (status & ERROR_BIT)
    return status;
  status = print_hex(out, value);
  if (status & ERROR_BIT)
    return status;
  return print(out, TEXT("\r\n"));
}

#ifndef ADMIN_GATE_NO_MAIN
EFI_STATUS efi_main(EFI_HANDLE image, SYSTEM_TABLE *system) {
  static const EFI_GUID hidden_guid = {
      0x77d054d6, 0x53c6, 0x4215, {0xb9, 0xc3, 0x66, 0x63, 0x7f, 0xca, 0xb7, 0xce}};
  static const EFI_GUID gpio_guid = {
      0xc0afb9bf, 0x6c4c, 0x4376, {0xb1, 0xcb, 0x29, 0x3a, 0x0d, 0x84, 0x3e, 0x64}};
  static const EFI_GUID console_guid = {
      0xf42f7782, 0x012e, 0x4c12, {0x99, 0x56, 0x49, 0xf9, 0x43, 0x04, 0xf7, 0x21}};
  TEXT_OUTPUT *out;
  EFI_STATUS status, console_status, result = 0;
  UINTN size = 1, index = 0;
  uint32_t attributes = 0, input = UINT32_MAX;
  uint8_t hidden = 0xff;
  FUJITSU_GPIO_READ_PROTOCOL *gpio = NULL;
  CONSOLE_CONTROL *console = NULL;
  EFI_EVENT event;
  struct {
    uint16_t scan_code;
    CHAR16 unicode;
  } key;

  (void)image;
  if (!system || system->signature != UINT64_C(0x5453595320494249) ||
      system->header_size < sizeof(SYSTEM_TABLE) ||
      !system->console_out || !system->console_out->output_string ||
      !system->runtime || !system->runtime->get_variable || !system->boot ||
      !system->boot->locate_protocol)
    return EFI_UNSUPPORTED;
  out = system->console_out;
  console_status = system->boot->locate_protocol(&console_guid, NULL,
                                                 (void **)&console);
  if (!(console_status & ERROR_BIT)) {
    if (!console || !console->set_mode)
      return EFI_COMPROMISED_DATA;
    console_status = console->set_mode(console, 0);
    if (console_status & ERROR_BIT)
      return console_status;
  }
  if (!out->set_attribute || !out->clear_screen)
    return EFI_UNSUPPORTED;
  status = out->set_attribute(out, 7);
  if (status & ERROR_BIT)
    return status;
  status = out->clear_screen(out);
  if (status & ERROR_BIT)
    return status;
  status = print(out, TEXT("\r\nFUJITSU ADMIN GATE - READ ONLY v2\r\n"
                           "No SetVariable, flash, GPIO writes or reset.\r\n\r\n"));
  if (status & ERROR_BIT)
    return status;
  line(out, TEXT("ConsoleControl status:"), console_status);

  status = system->runtime->get_variable(TEXT("FjSetupHiddenAdminPage"),
                                         &hidden_guid, &attributes, &size, &hidden);
  line(out, TEXT("GetVariable status: "), status);
  if (status & ERROR_BIT) {
    result = status;
  } else {
    line(out, TEXT("Variable bytes:     "), size);
    line(out, TEXT("Variable attributes:"), attributes);
    line(out, TEXT("Hidden Admin value: "), hidden);
    if (size != 1) {
      print(out, TEXT("ERROR: unexpected variable size.\r\n"));
      result = EFI_COMPROMISED_DATA;
    }
  }

  status = system->boot->locate_protocol(&gpio_guid, NULL, (void **)&gpio);
  line(out, TEXT("Locate GPIO status: "), status);
  if (status & ERROR_BIT) {
    result = status;
  } else if (!gpio || !gpio->read_input) {
    print(out, TEXT("ERROR: GPIO protocol has no read callback.\r\n"));
    result = EFI_COMPROMISED_DATA;
  } else {
    status = gpio->read_input(0x102, 8, &input);
    line(out, TEXT("GPIO read status:   "), status);
    if (status & ERROR_BIT) {
      result = status;
    } else {
      line(out, TEXT("GPIO selector:      "), 0x102);
      line(out, TEXT("GPIO pin:           "), 8);
      line(out, TEXT("Raw GPIO input:     "), input);
      if (input > 1) {
        print(out, TEXT("ERROR: unexpected input; no gate interpretation.\r\n"));
        result = EFI_COMPROMISED_DATA;
      } else {
        line(out, TEXT("Admin gate (raw^1): "), input ^ 1);
        print(out, input == 0
                       ? TEXT("Hardware gate PERMITS the hotkey path.\r\n")
                       : TEXT("Hardware gate BLOCKS the hotkey path.\r\n"));
      }
    }
  }
  line(out, TEXT("Diagnostic status:  "), result);
  print(out, TEXT("\r\nNo settings changed. Photograph this screen.\r\n"
                  "Press a key to return; this program never resets the server.\r\n"));
  if (!system->console_in || !system->console_in->wait_for_key ||
      !system->console_in->read_key || !system->boot->wait_for_event) {
    print(out, TEXT("ERROR: keyboard wait unavailable; returning.\r\n"));
    return result ? result : EFI_UNSUPPORTED;
  }
  event = system->console_in->wait_for_key;
  status = system->boot->wait_for_event(1, &event, &index);
  if (status & ERROR_BIT) {
    line(out, TEXT("Keyboard wait error:"), status);
    return status;
  }
  status = system->console_in->read_key(system->console_in, &key);
  if (status & ERROR_BIT) {
    line(out, TEXT("Keyboard read error:"), status);
    return status;
  }
  return result;
}
#endif
