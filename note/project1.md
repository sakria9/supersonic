## Details

There is time shift in the speaker or microphone.
So it's hard to use PSK.
Therefore we use FSK to modulate binary data to wave


Use RS code.

## Run

commit 4277948d3223464a834715cb53329b54c058b2a0

Executable `supersonic/proj1`

Need a `config.json` file.

Hint:
1. `ringbuffer_size` has to be large enough on Windows.
2. Program reads from `input.txt`. `payload_size` should exactly be the number of 0/1 in the file.
3. Program selects the first device that has substring of `input_port`. Same for `output_port`.
4. FSK frequency is `symbol_freq` * `channel`. For FSK, `symbol_bits` should always be 1, and `len(channels)` should always be 2.

```json
{
    "saudio_option": {
        "input_port": "Unitek Y-247A",
        "output_port": "Unitek Y-247A",
        "ringbuffer_size": 1280
    },
    "ofdm_option": {
        "symbol_freq": 4000,
        "symbol_bits": 1,
        "channels": [
            1,
            2
        ],
        "shift_samples": 0
    },
    "sphy_option": {
        "bin_payload_size": 40,
        "frame_gap_size": 48
    },
    "project1_option": {
        "payload_size": 10000
    }
}
```