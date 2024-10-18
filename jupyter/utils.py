import scipy.io.wavfile as wav
import numpy as np

import matplotlib.pyplot as plt

import sounddevice as sd

from scipy.signal import butter, filtfilt

def read_wave_file(file_path) -> tuple[int, np.ndarray]:
    rate, data = wav.read(file_path)
    if data.ndim == 2:
        data = data[:, 0]
    # convert to float
    if data.dtype == 'int16':
        data = data / 32768.0
    elif data.dtype == 'float32':
        pass
    else:
        raise ValueError("Unknown data type")
    return rate, data

def save_wave_file(file_path, rate, data):
    # Ensure data is in the correct format
    if data.dtype != 'float32':
        data = data.astype('float32')

    # Convert float data to int16 for saving
    data_int16 = (data * 32767).astype('int16')

    # Write the data to a wav file
    wav.write(file_path, rate, data_int16)

def plot_audio(data, rate, seconds=1):
    # If seconds is None, plot the entire audio data
    if seconds is None:
        num_samples = len(data)
        seconds = num_samples / rate
    else:
        num_samples = min(int(seconds * rate), len(data))
        seconds = min(seconds, len(data) / rate)

    # Create a time axis based on the sample rate and the specified duration
    time = np.linspace(0., seconds, num_samples)

    # Plot the audio data for the specified duration
    plt.figure()
    plt.plot(time, data[:num_samples])
    plt.xlabel('Time (s)')
    plt.ylabel('Amplitude')
    plt.title(f'Audio Data (First {seconds} seconds)' if seconds is not None else 'Audio Data (Full)')
    plt.show()

def plot_fft(data, rate, freq_range=(0, 24000)):
    fft_data = np.fft.rfft(data)
    freqs = np.fft.rfftfreq(len(data), 1 / rate)
    plt.figure()
    plt.plot(freqs, np.abs(fft_data), 'o')
    plt.plot(freqs, np.abs(fft_data), '--')
    plt.xlim(freq_range)
    plt.xlabel('Frequency (Hz)')
    plt.ylabel('Magnitude')
    plt.title(f'FFT of Audio Data, {rate/data.size:.2f} Hz resolution')
    plt.show()

def plot_wav_file(file_path, seconds=1):
    rate, data = read_wave_file(file_path)
    plot_audio(data, rate, seconds)
    plot_fft(data, rate, seconds)

def low_pass_filter(data, rate, freq):
    nyquist = 0.5 * rate
    normal_cutoff = freq / nyquist
    b, a = butter(5, normal_cutoff, btype='low', analog=False)
    filtered_data = filtfilt(b, a, data)
    return filtered_data

def play_audio(audio: np.array, rate=48000):
    save_wave_file("../build/temp.wav", rate, audio)
    sd.play(audio, rate)
    sd.wait()

def audio_by_function(func: callable, rate=48000, duration=1):
    timepoints = np.linspace(0, duration, int(rate * duration))
    return func(timepoints)

def play_audio_by_function(func: callable, rate=48000, duration=1):
    timepoints = np.linspace(0, duration, int(rate * duration))
    audio = audio_by_function(func, rate, duration)
    play_audio(audio, rate)

def sine_wave(freq_hz, phase=0):
    return lambda time_in_second: np.sin(2 * np.pi * freq_hz * time_in_second + phase)

def cosine_wave(freq_hz, phase=0):
    return lambda time_in_second: np.sin(2 * np.pi * freq_hz * time_in_second + phase)

sine_440hz = sine_wave(440)
sine_880hz = sine_wave(880)

def square_wave(freq):
    return lambda time_in_second: np.sign(np.sin(2 * np.pi * freq * time_in_second))

def cosine_440hz(time_in_second):
    return np.cos(2 * np.pi * 440 * time_in_second)

def multiply_amplitude(amplitude: float, func: callable):
    def wrapper(*args, **kwargs):
        return amplitude * func(*args, **kwargs)
    return wrapper

def sine_wave_samples(freq, cycle, phase, amplitude, rate=48000):
    duration = cycle / freq
    timepoints = np.linspace(0, duration, int(rate * duration))
    return multiply_amplitude(amplitude, sine_wave(freq, phase))(timepoints)

def play_sine_waves(sine_waves: tuple[int, float, float], rate=48000):
    audio = np.zeros(0)
    for freq, cycle, amplitude in sine_waves:
        sine_wave_audio = sine_wave_samples(freq, cycle, 0, amplitude, rate)
        audio = np.concatenate((audio, sine_wave_audio))
    play_audio(audio, rate)

# play_sine_waves([(4800, 4, 0.5), (4800, 4, 1), (2400, 4, 1)] * 500)

# play_audio_by_function(sine_wave(440), duration=10)
# play_audio_by_function(sine_wave(4800), duration=3)
# play_audio_by_function(lambda t: (sine_wave(440)(t) + sine_wave()(t))/2, duration=2)

def play_and_record(data, rate, discard=True):
    save_wave_file("../build/temp.wav", rate, data)

    cmd = "../build/supersonic/play_and_record -f ../build/temp.wav -i 'UGREEN CM564 USB Audio  Mono:capture_MONO' -o 'USB2.0 Device Analog Stereo:playback_FL'"
    # cmd = "..\\out\\build\\x64-debug\\supersonic\\play_and_record.exe -f ../build/temp.wav"
    import subprocess
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.wait()
    if p.returncode != 0:
        print(p.stdout.read())
        print(p.stderr.read())
        raise ValueError("play_and_record failed")
    
    rate, record_wave = read_wave_file("raw_input.wav")
    rate, play_wave = read_wave_file("raw_output.wav")
    if discard:
        # discard first 0.5s
        record_wave = record_wave[int(0.5 * rate):]
        play_wave = play_wave[int(0.5 * rate):]
    return record_wave, play_wave, rate

def gen_chirp(f_0, c, duration, rate):
    f_1 = c * duration + f_0
    print(f"chrip freq {f_0}, {f_1}")

    def phi(t):
        return 2 * np.pi * (c / 2 * t * t + f_0 * t)

    times = np.linspace(0, duration, int(rate * duration)+1)[:-1]
    chirp = np.sin(phi(times))
    chirp_rev = -np.flip(chirp)
    chirp = np.concatenate((chirp, chirp_rev))
    chirp = np.concatenate((chirp, np.zeros(48)))
    return chirp


chirp1 = gen_chirp(5000, 5000000, 0.001, 48000)
chirp2 = gen_chirp(7000, 400000, 0.01, 48000)

def locate_chirp(data, chirp):
    corr = np.correlate(data / np.max(np.abs(data)), chirp, mode='full') / len(chirp)
    loc = np.argmax(np.abs(corr) > 0.1)
    print("max corr", np.max(np.abs(corr)))
    assert np.max(np.abs(corr)) > 0.1, f"Chirp not found, max corr {np.max(np.abs(corr))}"
    return loc

def play_and_record_precise(data, rate):
    # get sine wave with 440Hz for 0.2s
    sine_wav = sine_wave_samples(440, 1 * 440, 0, 1, rate)
    # empty for 0.2s
    empty = np.zeros(int(0.2 * rate))
    audio = np.concatenate((sine_wav, empty, chirp1, data, chirp2, np.zeros(int(0.5*rate))))

    record_wave, play_wave, rate = play_and_record(audio, rate, discard=False)

    magic_correction = 0

    chirp1_loc = locate_chirp(record_wave, chirp1) + magic_correction
    chirp2_loc = locate_chirp(record_wave, chirp2) + magic_correction
    chirp2_loc = min(chirp2_loc, len(record_wave))

    start = chirp1_loc + 1
    end = chirp2_loc - len(chirp2) + 1

    return record_wave[start:end], rate

def play_and_record_precise2(data, rate):
    # get sine wave with 440Hz for 0.2s
    sine_wav = sine_wave_samples(440, 1 * 440, 0, 1, rate)
    # empty for 0.2s
    empty = np.zeros(int(0.2 * rate))
    audio = np.concatenate((sine_wav, empty, chirp1, data, chirp2, np.zeros(int(0.5*rate))))

    record_wave, play_wave, rate = play_and_record(audio, rate, discard=False)

    magic_correction = 0

    chirp1_loc = locate_chirp(record_wave, chirp1) + magic_correction
    chirp2_loc = locate_chirp(record_wave, chirp2) + magic_correction
    chirp2_loc = min(chirp2_loc, len(record_wave))

    start = chirp1_loc + 1
    end = chirp2_loc - len(chirp2) + 1
    print(start, end)

    return record_wave[start:end], rate, record_wave