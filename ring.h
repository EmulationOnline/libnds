// Copyright EmulationOnline 2025
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <pthread.h>
#include <stdint.h>

#ifndef PICO_RING_CAPACITY
#define PICO_RING_CAPACITY (44100 / 4)
#endif

typedef int16_t RING_T;

struct ring_i16 {
    RING_T buffer_[PICO_RING_CAPACITY];
    // each ptr is written only by a single fn.
    pthread_mutex_t mut_;
    // ptrs guarded by mut_
    size_t first;
    size_t len;
};

void ring_init(struct ring_i16* ring) {
    pthread_mutex_init(&ring->mut_, NULL);
    ring->first = 0;
    ring->len = 0;
}

void ring_unlock(struct ring_i16* ring) {
    pthread_mutex_unlock(&ring->mut_);
}
void ring_lock(struct ring_i16* ring) {
    pthread_mutex_lock(&ring->mut_);
}
// Attempts to enqueue 'count' items from 'src'.
// Returns the number of values successfully written.
size_t ring_push(struct ring_i16* ring, const RING_T* src, size_t count) {
    ring_lock(ring);
    size_t written = 0;
    size_t first = ring->first;
    size_t next = first + ring->len;
    for (size_t i = 0; i < count; i++) {
        if (ring->len == PICO_RING_CAPACITY) break;
        ring->buffer_[(next + i) % PICO_RING_CAPACITY] = src[i];
        // std::cout << "wrote: " << src[i] << std::endl;
        ring->len++;
        written++;
    }
    ring_unlock(ring);
    return written;
}

// Attempts to dequeue 'count' items from 'src'.
// Returns the number of values successfully read.
size_t ring_pull(struct ring_i16* ring, RING_T* dest, size_t count) {
    ring_lock(ring);
    size_t saved = 0;
    for (size_t i = 0; i < count; i++) {
        if (ring->len == 0) break;
        dest[i] = ring->buffer_[ring->first];
        // std::cout << "read: " << dest[i] << std::endl;
        ring->first = (ring->first+1) % PICO_RING_CAPACITY;
        ring->len--;
        saved++;
    }
    ring_unlock(ring);
    return saved;
}
