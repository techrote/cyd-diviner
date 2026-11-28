/* One 64 KiB hardware aperture; both bus aliases must remain unused. */
ASSERT(_iram_end <= 0x403c0000, "S3 snapshot aperture overlaps IRAM")
ASSERT(_data_end <= 0x3fcd0000, "S3 snapshot aperture overlaps data")
ASSERT(_bss_end <= 0x3fcd0000, "S3 snapshot aperture overlaps BSS")
