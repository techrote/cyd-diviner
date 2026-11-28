# Temperature range interpretation — pinned source review

This note qualifies the range wording in the closed reports. It changes no
recorded reading, firmware image or physical result.

The S3 probe requests 10–80 °C when installing the temperature driver. In the
pinned ESP-IDF source, that selects an initial sensor range; it is **not an
absolute 80 °C measurement ceiling**. The shared read path can change the DAC
range and wait before rereading. The S3 attribute table includes ranges above
80 °C, and the public Celsius API checks its result against the wider software
measurement bounds. Therefore a successful 98.9 °C indication must not be
discarded merely because it exceeds the initial 80 °C request.

Primary pinned sources:

- [Driver range selection and Celsius conversion](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/esp_driver_tsens/src/temperature_sensor.c).
- [Shared raw read, range cache and automatic switching](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/esp_hw_support/sar_periph_ctrl_common.c).
- [S3 range attributes](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/soc/esp32s3/temperature_sensor_periph.c).
- [S3 conversion constants and software validity bounds](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/hal/esp32s3/include/hal/temperature_sensor_ll.h).

The driver also attempts an eFuse correction. The captured sensor lifecycle
statuses do not independently attest that correction or external accuracy.
The probe did not log the raw ADC value, active DAC range or range-change flag.
Consequently the repeated 98.9 °C indications do not establish an exact physical
temperature plateau, and the record cannot resolve possible clipping/range-state
effects versus actual heating.

One source-level uncertainty merits a future controlled check: each explicit
probe reading installs/uninstalls the driver. Installation synchronizes the
active range index, while the shared read path maintains its own recorded range
bounds. The inspected index synchronization function does not reset those cached
bounds. This is a **possible interaction inferred from code**, not an observed
failure or a diagnosis of these boards. No new firmware or test was introduced
to resolve it during the quick fan repeat.

The defensible observation is that the same TX image reported 98.9 °C in the
earlier matched six-window subset and 29.9–55.9 °C in the later fan/moving-board
repeat. The initial range request alone invalidates neither set. Their accuracy
and physical cause remain externally unverified; airflow, geometry, workload
history and initialization also changed. The new decoding outcome therefore
does not isolate temperature as its cause.
