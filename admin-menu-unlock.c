#define efi_main readonly_diagnostic_entry
#include "admin-gate-readonly.c"
#undef efi_main

typedef struct {
  RUNTIME_SERVICES reader;
  void *get_next_variable;
  EFI_STATUS (*set_variable)(const CHAR16 *, const EFI_GUID *, uint32_t,
                             UINTN, const void *);
} WRITE_RUNTIME;

_Static_assert(offsetof(WRITE_RUNTIME, set_variable) == 0x58, "SetVar offset");

static EFI_STATUS unlock_once(WRITE_RUNTIME *runtime, TEXT_OUTPUT *out) {
  static const EFI_GUID guid = {
      0x77d054d6, 0x53c6, 0x4215, {0xb9, 0xc3, 0x66, 0x63, 0x7f, 0xca, 0xb7, 0xce}};
  const CHAR16 *name = TEXT("FjSetupHiddenAdminPage");
  UINTN size = 1;
  uint32_t attributes = 0;
  uint8_t value = 0xff, desired = 0;
  EFI_STATUS status = runtime->reader.get_variable(
      name, &guid, &attributes, &size, &value);
  line(out, TEXT("Precheck GetVariable: "), status);
  if (status != 0)
    return status;
  line(out, TEXT("Precheck bytes:       "), size);
  line(out, TEXT("Precheck attributes:  "), attributes);
  line(out, TEXT("Precheck value:       "), value);
  if (size != 1 || attributes != 2 || value > 1) {
    print(out, TEXT("ABORT: unexpected variable. NO WRITE.\r\n"));
    return EFI_COMPROMISED_DATA;
  }
  if (value == 0) {
    print(out, TEXT("Already zero. NO WRITE necessary.\r\n"));
    return 0;
  }
  status = print(out, TEXT("Writing ONLY this variable: one byte 01 -> 00, attributes 02.\r\n"));
  if (status != 0)
    return status;
  EFI_STATUS write_status = runtime->set_variable(name, &guid, attributes, 1, &desired);
  size = 1;
  attributes = 0;
  value = 0xff;
  EFI_STATUS read_status = runtime->reader.get_variable(
      name, &guid, &attributes, &size, &value);
  line(out, TEXT("SetVariable status:   "), write_status);
  line(out, TEXT("Readback status:      "), read_status);
  if (read_status == 0) {
    line(out, TEXT("Readback bytes:       "), size);
    line(out, TEXT("Readback attributes:  "), attributes);
    line(out, TEXT("Readback value:       "), value);
  }
  if (write_status != 0)
    return write_status;
  if (read_status != 0)
    return read_status;
  if (size != 1 || attributes != 2 || value != 0)
    return EFI_COMPROMISED_DATA;
  return 0;
}

EFI_STATUS efi_main(EFI_HANDLE image, SYSTEM_TABLE *system) {
  (void)image;
  if (!system || system->signature != UINT64_C(0x5453595320494249) ||
      system->header_size < sizeof(SYSTEM_TABLE) || !system->console_out ||
      !system->console_out->output_string || !system->runtime ||
      !system->runtime->get_variable || !system->boot ||
      !system->boot->wait_for_event || !system->console_in ||
      !system->console_in->wait_for_key || !system->console_in->read_key)
    return EFI_UNSUPPORTED;
  WRITE_RUNTIME *runtime = (WRITE_RUNTIME *)system->runtime;
  TEXT_OUTPUT *out = system->console_out;
  if (!runtime->set_variable)
    return EFI_UNSUPPORTED;
  EFI_STATUS status = print(out, TEXT(
      "\r\nFUJITSU ADMIN MENU - GUARDED VOLATILE WRITE v1\r\n"
      "ONLY FjSetupHiddenAdminPage; no flash, GPIO writes or reset.\r\n"));
  if (status != 0)
    return status;
  EFI_STATUS result = unlock_once(runtime, out);
  line(out, TEXT("Operation status:     "), result);
  print(out, result == 0
      ? TEXT("Verified: hidden flag is zero. Menu visibility not yet verified.\r\n")
      : TEXT("ERROR: operation did not verify successfully. No retries.\r\n"));
  print(out, TEXT(
      "PHOTOGRAPH THIS SCREEN. Do not reboot: the flag is volatile.\r\n"
      "Program stays here; Escape returns and may start the default OS.\r\n"));
  for (;;) {
    EFI_EVENT event = system->console_in->wait_for_key;
    UINTN index = 0;
    struct { uint16_t scan_code; CHAR16 unicode; } key;
    status = system->boot->wait_for_event(1, &event, &index);
    if (status != 0) {
      line(out, TEXT("Keyboard wait error:  "), status);
      return status;
    }
    status = system->console_in->read_key(system->console_in, &key);
    if (status != 0) {
      line(out, TEXT("Keyboard read error:  "), status);
      return status;
    }
    if (key.scan_code == 0x17)
      return result;
  }
}
