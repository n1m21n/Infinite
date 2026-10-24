# Next release (unreleased)

### Windows audio

- Native ASIO driver support: choose "ASIO" as the output mode in Settings > Audio, pick your interface's driver, and play at buffer sizes down to 32 frames with a round-trip latency readout. An ASIO driver is one device for input and output. If the driver is missing or will not start, Infinite keeps playing on WASAPI and says why. Thanks to Ricardo Palmieri, whose fork set the behaviour.
