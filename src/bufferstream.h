#ifndef BUFFERSTREAM_H
#define BUFFERSTREAM_H

#ifdef ARDUINO
#include <Stream.h>
#endif
#include <string.h>

// Minimal Stream wrapper around a pre-allocated buffer.
// Used by httpGet() with HTTPClient::writeToStream() to receive
// HTTP response data with built-in capacity bounds checking.
class BufferStream : public Stream {
public:
    BufferStream(uint8_t* buf, size_t capacity)
        : _buf(buf), _cap(capacity), _pos(0) {}

    size_t write(uint8_t b) override {
        if (_pos >= _cap) return 0;
        _buf[_pos++] = b;
        return 1;
    }

    size_t write(const uint8_t* buf, size_t size) override {
        size_t n = size < (_cap - _pos) ? size : (_cap - _pos);
        memcpy(_buf + _pos, buf, n);
        _pos += n;
        return n;
    }

    size_t bytesWritten() const { return _pos; }

    // Required by Stream interface but unused by writeToStream()
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

private:
    uint8_t* _buf;
    size_t _cap;
    size_t _pos;
};

#endif
