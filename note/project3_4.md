## Details

Project 3 implements TUN. So there is almost nothing to do in Project 4.

There are bugs in the program, but we have no time to debug.

Compared to project 2, we delete the retransmission in the link layer.

## Config

TUN has IP `172.18.25.x`, where x can be 1 or 2.

`allow_ips` is a whitelist. The program will drop packet whose destination is not in the list.
This is to prevent Windows and many background application overwhelming the link.

### Node 1

```json
{
    "saudio_option": {
        "input_port": "Microphone (3- USB Audio Device)",
        "output_port": "Speakers (3- USB Audio Device)",
        "ringbuffer_size": 128000
    },
    "sphy_option": {
        "bin_payload_size": 40,
        "frame_gap_size": 0,
        "magic_factor": 1.0,
        "preamble_threshold": 0.05,
        "max_payload_size": 4635
    },
    "smac_option":  {
        "mac_addr": 0,
        "timeout_ms": 200,
        "busy_power_threshold": 0.01,
        "backoff_ms": 200,
        "max_backoff_ms": 2000,
        "max_retries": 50
    },
    "tun_option": {
        "ip_suffix": 1,
        "allow_ips": [
            "172.18.25.2",
            "1.1.1.1"
        ]
    },
    "project1_option": {
        "payload_size": 50000
    }
}
```

## Node 2

```json
{
    "saudio_option": {
        "input_port": "Microphone (4- USB Audio Device)",
        "output_port": "Speakers (4- USB Audio Device)",
        "ringbuffer_size": 128000
    },
    "sphy_option": {
        "bin_payload_size": 40,
        "frame_gap_size": 0,
        "magic_factor": 1.0,
        "preamble_threshold": 0.05,
        "max_payload_size": 4635
    },
    "smac_option":  {
        "mac_addr": 1,
        "timeout_ms": 200,
        "busy_power_threshold": 0.01,
        "backoff_ms": 200,
        "max_backoff_ms": 2000,
        "max_retries": 50
    },
    "tun_option": {
        "ip_suffix": 1,
        "allow_ips": [
            "172.18.25.1",
            "1.1.1.1"
        ]
    },
    "project1_option": {
        "payload_size": 50000
    }
}
```