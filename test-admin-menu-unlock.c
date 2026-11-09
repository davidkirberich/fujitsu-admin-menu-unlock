#include "admin-menu-unlock.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned reads, writes;
static uint8_t initial, stored;
static uint32_t attrs;
static UINTN bytes;
static EFI_STATUS get_error, set_error;
static int mismatch;

static EFI_STATUS output(TEXT_OUTPUT *out, const CHAR16 *text) {
  (void)out; (void)text;
  return 0;
}

static void check_identity(const CHAR16 *name, const EFI_GUID *guid) {
  static const EFI_GUID expected = {
      0x77d054d6, 0x53c6, 0x4215, {0xb9, 0xc3, 0x66, 0x63, 0x7f, 0xca, 0xb7, 0xce}};
  assert(memcmp(guid, &expected, sizeof(expected)) == 0);
  assert(memcmp(name, L"FjSetupHiddenAdminPage",
                sizeof(L"FjSetupHiddenAdminPage")) == 0);
}

static EFI_STATUS get(const CHAR16 *name, const EFI_GUID *guid,
                      uint32_t *attributes, UINTN *size, void *data) {
  check_identity(name, guid);
  assert(*size == 1);
  reads++;
  if (get_error)
    return get_error;
  *attributes = attrs;
  *size = bytes;
  *(uint8_t *)data = reads == 1 ? initial : (mismatch ? 1 : stored);
  return 0;
}

static EFI_STATUS set(const CHAR16 *name, const EFI_GUID *guid,
                      uint32_t attributes, UINTN size, const void *data) {
  check_identity(name, guid);
  writes++;
  assert(writes == 1 && reads == 1);
  assert(attributes == 2 && size == 1 && *(const uint8_t *)data == 0);
  if (!set_error)
    stored = *(const uint8_t *)data;
  return set_error;
}

static void run(uint8_t value, uint32_t attributes, UINTN size,
                EFI_STATUS read_error, EFI_STATUS write_error,
                int bad_readback, EFI_STATUS expected, unsigned expected_writes) {
  WRITE_RUNTIME runtime = {{ {0}, get }, NULL, set};
  TEXT_OUTPUT out = {0};
  out.output_string = output;
  reads = writes = 0; initial = stored = value;
  attrs = attributes; bytes = size; get_error = read_error;
  set_error = write_error; mismatch = bad_readback;
  assert(unlock_once(&runtime, &out) == expected);
  assert(writes == expected_writes);
  if (writes)
    assert(reads == 2);
}

int main(void) {
  run(1, 2, 1, 0, 0, 0, 0, 1);
  run(0, 2, 1, 0, 0, 0, 0, 0);
  run(2, 2, 1, 0, 0, 0, EFI_COMPROMISED_DATA, 0);
  run(1, 3, 1, 0, 0, 0, EFI_COMPROMISED_DATA, 0);
  run(1, 6, 1, 0, 0, 0, EFI_COMPROMISED_DATA, 0);
  run(1, 2, 2, 0, 0, 0, EFI_COMPROMISED_DATA, 0);
  run(1, 2, 1, ERROR_BIT | 14, 0, 0, ERROR_BIT | 14, 0);
  run(1, 2, 1, 0, ERROR_BIT | 8, 0, ERROR_BIT | 8, 1);
  run(1, 2, 1, 0, 0, 1, EFI_COMPROMISED_DATA, 1);
  assert(efi_main(NULL, NULL) == EFI_UNSUPPORTED);
  puts("Nine guarded-write cases and invalid-table guard passed.");
}
