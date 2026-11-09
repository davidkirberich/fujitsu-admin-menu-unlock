import hashlib
import io
from pathlib import Path
import struct
import sys

from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS
import pycdlib

root = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).parent / "build"
program = root / "admin-menu-unlock.efi"
image = root / "admin-menu-unlock.img"
iso_path = root / "admin-menu-unlock.iso"
data = program.read_bytes()
pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
    raise ValueError("Not a PE executable")
if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
    raise ValueError("Not x64")
optional = pe + 24
if struct.unpack_from("<H", data, optional + 68)[0] != 10:
    raise ValueError("Not an EFI application")
import_rva, import_size = struct.unpack_from("<II", data, optional + 120)
if import_rva or import_size:
    count = struct.unpack_from("<H", data, pe + 6)[0]
    headers = optional + struct.unpack_from("<H", data, pe + 20)[0]
    import_data = None
    for index in range(count):
        section = headers + 40 * index
        rva, raw_size, raw_offset = struct.unpack_from("<III", data, section + 12)
        if rva <= import_rva and import_rva + import_size <= rva + raw_size:
            offset = raw_offset + import_rva - rva
            import_data = data[offset:offset + import_size]
            break
    if import_data is None or any(import_data):
        raise ValueError("Unexpected Windows DLL imports")

for target in (image, iso_path):
    if target.exists():
        raise FileExistsError(f"Refusing to overwrite {target}")
image.touch(exist_ok=False)
fat = PyFat()
fat.mkfs(str(image), fat_type=PyFat.FAT_TYPE_FAT12, size=1440 * 1024,
         label="ADMINREAD")
fat.close()
with PyFatFS(str(image)) as fs:
    fs.makedirs("/EFI/BOOT")
    fs.writebytes("/EFI/BOOT/BOOTX64.EFI", data)
with PyFatFS(str(image), read_only=True) as fs:
    if fs.readbytes("/EFI/BOOT/BOOTX64.EFI") != data:
        raise ValueError("EFI image readback mismatch")

iso = pycdlib.PyCdlib()
iso.new(interchange_level=1, vol_ident="ADMIN_GATE_READONLY")
iso.add_file(str(image), iso_path="/EFIBOOT.IMG;1")
iso.add_file(str(program), iso_path="/ADMIN.EFI;1")
iso.add_eltorito("/EFIBOOT.IMG;1", platform_id=0xEF,
                 boot_load_size=image.stat().st_size // 512)
iso.write(str(iso_path))
iso.close()
check = pycdlib.PyCdlib()
check.open(str(iso_path))
readback = io.BytesIO()
check.get_file_from_iso_fp(readback, iso_path="/EFIBOOT.IMG;1")
if readback.getvalue() != image.read_bytes():
    raise ValueError("ISO boot image readback mismatch")
readback = io.BytesIO()
check.get_file_from_iso_fp(readback, iso_path="/ADMIN.EFI;1")
check.close()
if readback.getvalue() != data:
    raise ValueError("ISO EFI readback mismatch")
for path in (program, image, iso_path):
    print(path.name, path.stat().st_size, hashlib.sha256(path.read_bytes()).hexdigest())
