#include <unity.h>
#include <stdint.h>
#include <stddef.h>

// Minimal Stream stub for native builds (Arduino's Stream.h is not available)
class Stream {
public:
    virtual ~Stream() {}
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t* buf, size_t size) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
};

#include "bufferstream.h"

void setUp(void) {}
void tearDown(void) {}

void test_write_single_byte(void) {
    uint8_t buf[16] = {0};
    BufferStream bs(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(1, bs.write((uint8_t)'A'));
    TEST_ASSERT_EQUAL(1, bs.bytesWritten());
    TEST_ASSERT_EQUAL('A', buf[0]);
}

void test_write_buffer(void) {
    uint8_t buf[16] = {0};
    BufferStream bs(buf, sizeof(buf));

    const uint8_t data[] = "Hello";
    TEST_ASSERT_EQUAL(5, bs.write(data, 5));
    TEST_ASSERT_EQUAL(5, bs.bytesWritten());
    TEST_ASSERT_EQUAL_MEMORY("Hello", buf, 5);
}

void test_write_clamps_at_capacity(void) {
    uint8_t buf[4] = {0};
    BufferStream bs(buf, sizeof(buf));

    const uint8_t data[] = "HelloWorld";
    TEST_ASSERT_EQUAL(4, bs.write(data, 10));
    TEST_ASSERT_EQUAL(4, bs.bytesWritten());
    TEST_ASSERT_EQUAL_MEMORY("Hell", buf, 4);
}

void test_single_byte_at_capacity(void) {
    uint8_t buf[2] = {0};
    BufferStream bs(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(1, bs.write((uint8_t)'A'));
    TEST_ASSERT_EQUAL(1, bs.write((uint8_t)'B'));
    TEST_ASSERT_EQUAL(0, bs.write((uint8_t)'C'));
    TEST_ASSERT_EQUAL(2, bs.bytesWritten());
}

void test_multiple_writes(void) {
    uint8_t buf[16] = {0};
    BufferStream bs(buf, sizeof(buf));

    const uint8_t d1[] = "Hi";
    const uint8_t d2[] = "There";
    TEST_ASSERT_EQUAL(2, bs.write(d1, 2));
    TEST_ASSERT_EQUAL(5, bs.write(d2, 5));
    TEST_ASSERT_EQUAL(7, bs.bytesWritten());
    TEST_ASSERT_EQUAL_MEMORY("HiThere", buf, 7);
}

void test_partial_write_near_capacity(void) {
    uint8_t buf[8] = {0};
    BufferStream bs(buf, sizeof(buf));

    const uint8_t d1[] = "123456";
    TEST_ASSERT_EQUAL(6, bs.write(d1, 6));
    const uint8_t d2[] = "ABCD";
    TEST_ASSERT_EQUAL(2, bs.write(d2, 4));
    TEST_ASSERT_EQUAL(8, bs.bytesWritten());
    TEST_ASSERT_EQUAL_MEMORY("123456AB", buf, 8);
}

void test_zero_capacity(void) {
    uint8_t buf[1] = {0};
    BufferStream bs(buf, 0);

    TEST_ASSERT_EQUAL(0, bs.write((uint8_t)'A'));
    TEST_ASSERT_EQUAL(0, bs.bytesWritten());
}

void test_stream_interface(void) {
    uint8_t buf[4] = {0};
    BufferStream bs(buf, sizeof(buf));

    TEST_ASSERT_EQUAL(0, bs.available());
    TEST_ASSERT_EQUAL(-1, bs.read());
    TEST_ASSERT_EQUAL(-1, bs.peek());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_write_single_byte);
    RUN_TEST(test_write_buffer);
    RUN_TEST(test_write_clamps_at_capacity);
    RUN_TEST(test_single_byte_at_capacity);
    RUN_TEST(test_multiple_writes);
    RUN_TEST(test_partial_write_near_capacity);
    RUN_TEST(test_zero_capacity);
    RUN_TEST(test_stream_interface);

    UNITY_END();
}
