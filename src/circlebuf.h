#include <stdint.h>
#include <string.h>

struct circlebuf {
    void *data;
    size_t size;

    size_t head;
    size_t tail;
    size_t capacity;
};

static inline void circlebuf_init(struct circlebuf *cb, void *data, int capacity) {
    memset(data, 0, capacity);
    memset(cb, 0, sizeof(struct circlebuf));
    cb->data = data;
    cb->capacity = capacity;
}

static inline void circlebuf_free(struct circlebuf *cb) {
    memset(cb, 0, sizeof(struct circlebuf));
    cb->data = NULL;
}

static inline void circlebuf_push(struct circlebuf *cb, const void *data, size_t size) {
    size_t new_tail = cb->tail + size;
    if (new_tail > cb->capacity) {
        size_t avail_size = cb->capacity - cb->tail;
        size_t write_size = size - avail_size;

        if (avail_size) {
            memcpy(cb->data + cb->tail, data, avail_size);
        }
        memcpy(cb->data, data + avail_size, write_size);

        new_tail -= cb->capacity;
    } else {
        memcpy(cb->data + cb->tail, data, size);
    }

    cb->size += size;
    cb->tail = new_tail;
}

static inline void circlebuf_pop(struct circlebuf *cb, void *data, size_t size) {
    if(size > cb->size) {
        return;
    }

    if (data) {
        size_t head_size = cb->capacity - cb->head;

        if (head_size < size) {
            memcpy(data, cb->data + cb->head, head_size);
            memcpy(data + head_size, cb->data, size - head_size);
        } else {
            memcpy(data, cb->data + cb->head, size);
        }
    }

    cb->size -= size;
    if (!cb->size) {
        cb->head = cb->tail = 0;
        return;
    }

    cb->head += size;
    if (cb->head >= cb->capacity) {
        cb->head -= cb->capacity;
    }
}
