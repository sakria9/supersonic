import numpy as np

def read_frames(file):
    with open(file, 'r') as f:
        lines = f.readlines()
        lines = [line.strip() for line in lines]
        lines = [np.array([1 if x == '1' else 0 for x in line]) for line in lines]
        return lines

def rs_simulate(truth_bits, recv_bits):
    if len(truth_bits) != len(recv_bits):
        print("length mismatch")
        return

    permu = np.random.permutation(len(truth_bits))
    truth_bits = truth_bits[permu]
    recv_bits = recv_bits[permu]

    num_bytes = len(truth_bits) // 8
    truth_bits = truth_bits[:num_bytes*8]
    recv_bits = recv_bits[:num_bytes*8]

    truth_bytes = np.zeros(num_bytes, dtype=np.uint8)
    recv_bytes = np.zeros(num_bytes, dtype=np.uint8)
    for i in range(num_bytes):
        for j in range(8):
            truth_bytes[i] |= truth_bits[i*8+j] << (7-j)
            recv_bytes[i] |= recv_bits[i*8+j] << (7-j)

    bytes_error_count = np.sum(truth_bytes != recv_bytes)
    bytes_error_rate = bytes_error_count / num_bytes
    # print(f"{bytes_error_count}/{num_bytes} bytes ({bytes_error_rate*100:.2f}%) are incorrect")
    print(1-bytes_error_rate)

    # rs(255, 155)
    MAX_ERROR = (255 - 155) // 2

    num_rs_blocks = len(truth_bytes) // 255
    truth_bytes = truth_bytes[:num_rs_blocks*255]
    recv_bytes = recv_bytes[:num_rs_blocks*255]

    truth_rs_blocks = [truth_bytes[i*255:(i+1)*255] for i in range(num_rs_blocks)]
    recv_rs_blocks = [recv_bytes[i*255:(i+1)*255] for i in range(num_rs_blocks)]

    for i in range(num_rs_blocks):
        truth_block = truth_rs_blocks[i]
        recv_block = recv_rs_blocks[i]
        diff = np.sum(truth_block != recv_block)
        if diff > MAX_ERROR:
            print(f"error")

def diff_frames(input, output):
    if len(input) != len(output):
        print(f"length mismatch, input: {len(input)}, output: {len(output)}")
        return

    truth_bits = np.concatenate(input)
    recv_bits = np.concatenate(output)

    max_cont_diff = 0
    similarities = []
    total_bits = 0
    correct_bits = 0
    one_to_zero_flips = 0
    zero_to_one_flips = 0
    frame_errors = 0
    frame_corrects = 0
    for frame1, frame2 in zip(input, output):
        total_bits += len(frame1)
        correct_bits += np.sum(frame1 == frame2)
        similarity = np.sum(frame1 == frame2) / len(frame1)
        similarities.append(similarity)
        cont_diff = 0
        for x, y in zip(frame1, frame2):
            if x != y:
                cont_diff += 1
            else:
                max_cont_diff = max(max_cont_diff, cont_diff)
                cont_diff = 0
            if x == 1 and y == 0:
                one_to_zero_flips += 1
            if x == 0 and y == 1:
                zero_to_one_flips += 1
        max_cont_diff = max(max_cont_diff, cont_diff)
        is_frame_correct = np.sum(frame1 == frame2) == len(frame1)
        if is_frame_correct:
            frame_corrects += 1
        else:
            frame_errors += 1

    similarities = np.array(similarities)
    # print("max continuous diff:", max_cont_diff)
    # print("mean:", np.mean(similarities), "median:", np.median(similarities))
    # print("max:", np.max(similarities), "min:", np.min(similarities))
    # print("std:", np.std(similarities))
    # print("percentile 25:", np.percentile(similarities, 25))
    # print("percentile 75:", np.percentile(similarities, 75))
    # print("percentile 90:", np.percentile(similarities, 90))
    # print(f"{correct_bits}/{total_bits} bits ({correct_bits/total_bits*100:2f}%) are correct")
    # print(f"min frame idx: {np.argmin(similarities)}, max frame idx: {np.argmax(similarities)}")
    # print(f"1=>0 flips: {one_to_zero_flips}, 0=>1 flips: {zero_to_one_flips}")
    # print(f"frames correct rate: {frame_corrects/(frame_corrects+frame_errors)*100:.2f}%")

    rs_simulate(truth_bits, recv_bits)
try:
    input = read_frames("/home/ryo/code/supersonic/build/input.txt")
    output = read_frames("/home/ryo/code/supersonic/build/output.txt")
    diff_frames(input, output)
except:
    print(0)
    print('error')
