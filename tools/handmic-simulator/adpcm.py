"""RF Link format 2 reference: independent 160-sample / 84-byte IMA blocks.

This is neither Apple ima4 nor a WAV file. Low nibble first; initial predictor
is output sample zero. Integer shifts match the IMA reference algorithm.
"""
import struct

STEPS = (7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
         50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,
         337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,
         1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
         5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
         18500,20350,22385,24623,27086,29794,32767)
INDEX = (-1,-1,-1,-1,2,4,6,8)


def expand(code, predictor, index):
    step = STEPS[index]
    delta = (step >> 3) + (step if code & 4 else 0) + ((step >> 1) if code & 2 else 0) + ((step >> 2) if code & 1 else 0)
    predictor = max(-32768, min(32767, predictor + (-delta if code & 8 else delta)))
    return predictor, max(0, min(88, index + INDEX[code & 7]))


def decode(block):
    if len(block) != 84 or block[2] > 88 or block[3] or block[-1] & 0xf0:
        raise ValueError('Invalid RF Link IMA block')
    predictor, index, _ = struct.unpack_from('<hBB', block)
    samples = [predictor]
    for i in range(159):
        code = (block[4 + i//2] >> ((i % 2)*4)) & 15
        predictor, index = expand(code, predictor, index)
        samples.append(predictor)
    return struct.pack('<160h', *samples)


def encode(pcm):
    if len(pcm) != 320:
        raise ValueError('Expected 160 signed little-endian PCM samples')
    samples = struct.unpack('<160h', pcm)
    predictor, index = samples[0], 0
    delta = abs(samples[1] - predictor)
    while index < 88 and STEPS[index] < delta:
        index += 1
    block = bytearray(struct.pack('<hBB', predictor, index, 0) + bytes(80))
    for i, sample in enumerate(samples[1:]):
        diff, step = sample - predictor, STEPS[index]
        code = 8 if diff < 0 else 0
        diff = abs(diff)
        for bit, threshold in ((4, step), (2, step >> 1), (1, step >> 2)):
            if diff >= threshold:
                code |= bit
                diff -= threshold
        predictor, index = expand(code, predictor, index)
        block[4 + i//2] |= code << ((i % 2)*4)
    return bytes(block)
