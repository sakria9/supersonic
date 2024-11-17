## ACK

Executable `proj2.exe`

Node 0 config

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
        "bin_payload_size": 40,
        "frame_gap_size": 0,
        "magic_factor": 1.0,
        "preamble_threshold": 0.05,
        "max_payload_size": 2048
    },
    "smac_option": {
        "mac_addr": 0,
        "timeout_ms": 250,
        "busy_power_threshold": 1.0,
        "backoff_ms": 400
    },
    "project1_option": {
        "payload_size": 10000
    },
    "project2_option": {
        "task": 1,
        "payload_size": 2000
    }
}
```

Node 1 config

```json
{
    "saudio_option": {
        "input_port": "Microphone (2- USB Audio Device)",
        "output_port": "Speakers (2- USB Audio Device)",
        "ringbuffer_size": 128000
    },
    "ofdm_option": {
        "symbol_freq": 1000,
        "channels": [1,2,3,4,5,6,7,8,9,10],
        "cp_samples": 0
    },
    "sphy_option": {
        "bin_payload_size": 40,
        "frame_gap_size": 0,
        "magic_factor": 1.0,
        "preamble_threshold": 0.05,
        "max_payload_size": 2048
    },
    "smac_option": {
        "mac_addr": 1,
        "timeout_ms": 200,
        "busy_power_threshold": 0.01,
        "backoff_ms": 400
    },
    "project1_option": {
        "payload_size": 10000
    },
    "project2_option": {
        "task": 2,
        "payload_size": 2000
    }
}
```