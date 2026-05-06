Import("env")

# Override the maximum upload size to match OTA0 partition.
# PlatformIO defaults to the first app partition (factory, 1MB — reserved for the
# Nakomis bootloader) but the main app actually flashes to OTA0, which is much
# larger.
#
# Partition sizes:
# - ESP32-CAM (4MB):  OTA0 = 0x370000 = 3,604,480 bytes (3.44MB)
# - ESP32-S3 (16MB):  OTA0 = 0x700000 = 7,340,032 bytes (7MB)

def before_build(source, target, env):
    board = env.BoardConfig()
    board_name = board.get("name", "")

    if "s3" in board_name.lower() or "s3" in env.get("PIOENV", "").lower():
        max_size = 7340032   # ESP32-S3 16MB: OTA0 = 7MB
    else:
        max_size = 3604480   # ESP32 4MB: OTA0 = 3.44MB

    board.update("upload.maximum_size", max_size)

env.AddPreAction("checkprogsize", before_build)
