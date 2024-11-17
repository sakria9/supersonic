## Cable Connection

Executable `proj1.exe`

Config:

```json
{
    "saudio_option": {
        "input_port": "Microphone (USB Audio Device)",
        "output_port": "Speakers (USB Audio Device)",
        "ringbuffer_size": 128000
    },
    "ofdm_option": {
        "symbol_freq": 1000,
        "channels": [1,2,3,4,5,6,7,8,9,10],
        "cp_samples": 0
    },
    "sphy_option": {
        "bin_payload_size": 2048,
        "frame_gap_size": 48,
        "magic_factor": 1.0,
        "preamble_threshold": 0.05,
        "max_payload_size": 2048
    },
    "project1_option": {
        "payload_size": 50000
    }
}
```