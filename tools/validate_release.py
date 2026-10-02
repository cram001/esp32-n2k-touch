"""Validate actual built partition/image artifacts before publishing firmware."""
from pathlib import Path
import argparse
import hashlib
import re
import struct
import unittest

FLASH_SIZE = 0x1000000
SLOT_SIZE = 0x600000
NETWORK_PROFILE = {
    'SPIRAM_USE_MALLOC': 1, 'SPIRAM_TRY_ALLOCATE_WIFI_LWIP': 1,
    'SPIRAM_MALLOC_ALWAYSINTERNAL': 1024, 'LWIP_MAX_SOCKETS': 10,
    'ESP_WIFI_STATIC_RX_BUFFER_NUM': 6, 'ESP_WIFI_DYNAMIC_RX_BUFFER_NUM': 16,
    'ESP_WIFI_RX_BA_WIN': 6,
    'ESP_WIFI_STATIC_TX_BUFFER': 1, 'ESP_WIFI_STATIC_TX_BUFFER_NUM': 6,
    'ESP_WIFI_CACHE_TX_BUFFER_NUM': 8, 'LWIP_TCP_SND_BUF_DEFAULT': 2880,
    'LWIP_TCP_WND_DEFAULT': 2880, 'LWIP_TCP_OOSEQ_MAX_PBUFS': 2,
}

def validate_network_profile(config):
    for name, expected in NETWORK_PROFILE.items():
        match = re.search(r'^#define CONFIG_' + name + r' (\d+)$', config, re.M)
        if not match or int(match.group(1)) != expected:
            raise ValueError(f'Stale network memory profile: CONFIG_{name}; regenerate sdkconfig')

def validate_startup_stack(config):
    stack = re.search(r'^#define CONFIG_ESP_MAIN_TASK_STACK_SIZE (\d+)$', config, re.M)
    if not stack or int(stack.group(1)) < 8192:
        raise ValueError("Startup stack below 8192 bytes; clean/reconfigure before flashing")

def validate_partitions(data):
    partitions = {}
    intervals = []
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            raise ValueError("Truncated partition table")
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xffff:
            break
        if magic == 0xebeb:
            if hashlib.md5(data[:offset]).digest() != entry[16:32]:
                raise ValueError("Partition table MD5 mismatch")
            break
        if magic != 0x50aa:
            raise ValueError("Invalid partition magic")
        _, kind, subtype, address, size, name, flags = struct.unpack('<HBBII16sI', entry)
        name = name.split(b'\0', 1)[0].decode('ascii')
        if not size or address < 0x9000 or address + size > FLASH_SIZE:
            raise ValueError("Partition outside flash")
        if kind == 0 and address % 0x10000:
            raise ValueError("Unaligned application partition")
        if any(address < end and address + size > start for start, end in intervals):
            raise ValueError("Overlapping partitions")
        intervals.append((address, address + size))
        partitions[name] = (kind, subtype, address, size)
    expected = {
        'nvs': (1, 2, 0x9000, 0x6000),
        'otadata': (1, 0, 0xf000, 0x2000),
        'ota_0': (0, 0x10, 0x20000, SLOT_SIZE),
        'ota_1': (0, 0x11, 0x620000, SLOT_SIZE),
    }
    for name, values in expected.items():
        if partitions.get(name) != values:
            raise ValueError(f"Unexpected {name} partition: {partitions.get(name)}")
    return partitions

def validate_image(data, expected_project='esp32_n2k_touch'):
    if len(data) <= 288 or len(data) > SLOT_SIZE:
        raise ValueError("Firmware does not fit an OTA slot")
    if data[0] != 0xe9 or not 1 <= data[1] <= 16:
        raise ValueError("Invalid ESP application header")
    if struct.unpack_from('<H', data, 12)[0] != 9:
        raise ValueError("Firmware is not for ESP32-S3")
    if data[3] >> 4 != 4:
        raise ValueError("Image is not configured for 16 MB flash")
    if struct.unpack_from('<I', data, 32)[0] != 0xabcd5432:
        raise ValueError("Application descriptor missing; use firmware.bin")
    project = data[80:112].split(b'\0', 1)[0].decode('ascii')
    version = data[48:80].split(b'\0', 1)[0].decode('ascii')
    if project != expected_project:
        raise ValueError(f"Wrong firmware project: {project}")
    position = 24
    checksum = 0xef
    for _ in range(data[1]):
        if position + 8 > len(data):
            raise ValueError("Truncated segment header")
        _, length = struct.unpack_from('<II', data, position)
        position += 8
        if position + length > len(data):
            raise ValueError("Truncated segment")
        for value in data[position:position + length]:
            checksum ^= value
        position += length
    checksum_position = position // 16 * 16 + 15
    if checksum_position >= len(data) or data[checksum_position] != checksum:
        raise ValueError("Image checksum mismatch")
    if data[23] != 1:
        raise ValueError("Release image must contain SHA-256")
    hash_position = checksum_position + 1
    if len(data) != hash_position + 32 or hashlib.sha256(data[:hash_position]).digest() != data[hash_position:]:
        raise ValueError("Image SHA-256 mismatch")
    build_time = data[112:128].split(b'\0', 1)[0].decode('ascii')
    build_date = data[128:144].split(b'\0', 1)[0].decode('ascii')
    return {'project': project, 'version': version, 'build_date': build_date, 'build_time': build_time,
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def validate_build(build):
    build = Path(build)
    validate_partitions((build / 'partitions.bin').read_bytes())
    for config in ('config/sdkconfig.h', 'bootloader/config/sdkconfig.h'):
        if '#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1' not in (build / config).read_text():
            raise ValueError(f"Rollback missing from generated {config}")
    validate_startup_stack((build / 'config/sdkconfig.h').read_text())
    validate_network_profile((build / 'config/sdkconfig.h').read_text())
    return validate_image((build / 'firmware.bin').read_bytes())

class ArtifactNegativeTests(unittest.TestCase):
    image = b''
    table = b''
    def test_valid(self):
        validate_image(self.image)
        validate_partitions(self.table)
    def test_corrupt_payload(self):
        corrupted = bytearray(self.image); corrupted[400] ^= 1
        with self.assertRaises(ValueError): validate_image(corrupted)
    def test_corrupt_hash(self):
        corrupted = bytearray(self.image); corrupted[-1] ^= 1
        with self.assertRaises(ValueError): validate_image(corrupted)
    def test_truncated(self):
        with self.assertRaises(ValueError): validate_image(self.image[:-256])
    def test_wrong_chip(self):
        corrupted = bytearray(self.image); corrupted[12] = 0
        with self.assertRaises(ValueError): validate_image(corrupted)
    def test_wrong_project(self):
        with self.assertRaises(ValueError): validate_image(self.image, 'another_project')
    def test_oversized(self):
        with self.assertRaises(ValueError): validate_image(bytes(SLOT_SIZE + 1))
    def test_partition_corruption(self):
        corrupted = bytearray(self.table); corrupted[8] ^= 1
        with self.assertRaises(ValueError): validate_partitions(corrupted)
    def test_stale_startup_stack(self):
        with self.assertRaises(ValueError):
            validate_startup_stack('#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 3584\n')
        validate_startup_stack('#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 8192\n')
    def test_missing_startup_stack(self):
        with self.assertRaises(ValueError): validate_startup_stack('')
    def test_network_profile(self):
        config = ''.join(f'#define CONFIG_{name} {value}\n' for name, value in NETWORK_PROFILE.items())
        validate_network_profile(config)
        for name in NETWORK_PROFILE:
            with self.assertRaises(ValueError):
                validate_network_profile(re.sub(r'^#define CONFIG_' + name + r' \d+\n', '', config, flags=re.M))
        with self.assertRaises(ValueError):
            validate_network_profile(config.replace('CONFIG_LWIP_MAX_SOCKETS 10', 'CONFIG_LWIP_MAX_SOCKETS 6'))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='.pio/build/waveshare-touch-4')
    parser.add_argument('--test', action='store_true')
    args = parser.parse_args()
    result = validate_build(args.build)
    print(f"PASS: dual 6 MB slots, NVS/OTA offsets, rollback in application and bootloader, image checksum and SHA-256; {result}")
    if args.test:
        ArtifactNegativeTests.image = (Path(args.build) / 'firmware.bin').read_bytes()
        ArtifactNegativeTests.table = (Path(args.build) / 'partitions.bin').read_bytes()
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ArtifactNegativeTests)
        if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
            raise SystemExit(1)
