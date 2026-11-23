#pragma once

#include <stdint.h>

namespace cyd_pcm_serial {

bool begin(uint8_t ledPin);
void expire(uint32_t now);
bool handleCommand(const char *line, uint32_t now);

// The I2S worker uses a session token so an in-flight block cannot spill into
// a capture that started after that block was rendered.
uint32_t session();
void submit(uint32_t session, const int16_t *samples);

} // namespace cyd_pcm_serial
