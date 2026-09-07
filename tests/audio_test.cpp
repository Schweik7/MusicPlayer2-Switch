// 音频层中平台无关部分的主机端测试。
// 目前是环形缓冲区：解码线程与音频回调之间的交接点，回绕边界最容易写错。

#include "TestFramework.h"

#include "../source/audio/AudioRingBuffer.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::vector<uint8_t> MakePattern(size_t n, uint8_t start = 0)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = static_cast<uint8_t>((start + i) & 0xFF);
    return v;
}

static void TestRingBufferBasics()
{
    std::printf("CAudioRingBuffer 基本操作\n");

    CAudioRingBuffer ring(16);
    CHECK_EQ_INT(ring.Capacity(), 16);
    CHECK_EQ_INT(ring.Available(), 0);
    CHECK_EQ_INT(ring.Space(), 16);
    CHECK(ring.IsEmpty());
    CHECK(!ring.IsFull());

    std::vector<uint8_t> in = MakePattern(8);
    CHECK_EQ_INT(ring.Write(in.data(), in.size()), 8);
    CHECK_EQ_INT(ring.Available(), 8);
    CHECK_EQ_INT(ring.Space(), 8);
    CHECK(!ring.IsEmpty());

    std::vector<uint8_t> out(8, 0xFF);
    CHECK_EQ_INT(ring.Read(out.data(), out.size()), 8);
    CHECK(std::memcmp(in.data(), out.data(), 8) == 0);
    CHECK(ring.IsEmpty());

    // 读空缓冲区应当返回 0 且不写坏 out
    out.assign(8, 0xAB);
    CHECK_EQ_INT(ring.Read(out.data(), out.size()), 0);
    CHECK_EQ_INT(out[0], 0xAB);
}

static void TestRingBufferWrap()
{
    std::printf("CAudioRingBuffer 回绕\n");

    CAudioRingBuffer ring(10);

    // 先写 8 读 6，把读写指针推到中间，制造回绕条件
    std::vector<uint8_t> first = MakePattern(8, 0);
    CHECK_EQ_INT(ring.Write(first.data(), 8), 8);
    std::vector<uint8_t> sink(6);
    CHECK_EQ_INT(ring.Read(sink.data(), 6), 6);
    CHECK_EQ_INT(ring.Available(), 2);
    CHECK_EQ_INT(ring.Space(), 8);

    // 这一写必定跨越缓冲区末尾
    std::vector<uint8_t> second = MakePattern(8, 100);
    CHECK_EQ_INT(ring.Write(second.data(), 8), 8);
    CHECK_EQ_INT(ring.Available(), 10);
    CHECK(ring.IsFull());

    // 读回来的顺序必须是：first 剩下的 2 字节，然后是 second 的 8 字节
    std::vector<uint8_t> out(10, 0);
    CHECK_EQ_INT(ring.Read(out.data(), 10), 10);
    CHECK_EQ_INT(out[0], first[6]);
    CHECK_EQ_INT(out[1], first[7]);
    for (int i = 0; i < 8; ++i)
        CHECK_EQ_INT(out[2 + i], second[i]);
    CHECK(ring.IsEmpty());
}

static void TestRingBufferOverflowUnderflow()
{
    std::printf("CAudioRingBuffer 溢出与不足\n");

    CAudioRingBuffer ring(8);

    // 写入超过容量时只写下能放下的部分
    std::vector<uint8_t> big = MakePattern(20);
    CHECK_EQ_INT(ring.Write(big.data(), big.size()), 8);
    CHECK(ring.IsFull());
    // 满了之后再写返回 0
    CHECK_EQ_INT(ring.Write(big.data(), 1), 0);

    // 读多于现有数据时只读出现有部分
    std::vector<uint8_t> out(20, 0);
    CHECK_EQ_INT(ring.Read(out.data(), 20), 8);
    CHECK(ring.IsEmpty());
    for (int i = 0; i < 8; ++i)
        CHECK_EQ_INT(out[i], big[i]);

    // 边界参数不能崩
    CHECK_EQ_INT(ring.Write(nullptr, 4), 0);
    CHECK_EQ_INT(ring.Read(nullptr, 4), 0);
    CHECK_EQ_INT(ring.Write(big.data(), 0), 0);
    CHECK_EQ_INT(ring.Read(out.data(), 0), 0);

    // 容量为 0 的缓冲区所有操作都应安全返回 0
    CAudioRingBuffer empty(0);
    CHECK_EQ_INT(empty.Capacity(), 0);
    CHECK_EQ_INT(empty.Write(big.data(), 4), 0);
    CHECK_EQ_INT(empty.Read(out.data(), 4), 0);
    CHECK_EQ_INT(empty.Discard(4), 0);
    CHECK(empty.IsEmpty());
    CHECK(empty.IsFull());              // 容量 0 时既空又满
}

static void TestRingBufferDiscardAndClear()
{
    std::printf("CAudioRingBuffer 丢弃与清空\n");

    CAudioRingBuffer ring(16);
    std::vector<uint8_t> in = MakePattern(12);
    ring.Write(in.data(), 12);

    CHECK_EQ_INT(ring.Discard(5), 5);
    CHECK_EQ_INT(ring.Available(), 7);

    std::vector<uint8_t> out(7);
    CHECK_EQ_INT(ring.Read(out.data(), 7), 7);
    CHECK_EQ_INT(out[0], in[5]);        // 丢弃后应从第 6 个字节继续

    // 丢弃超过现有量只丢现有的
    ring.Write(in.data(), 10);
    CHECK_EQ_INT(ring.Discard(100), 10);
    CHECK(ring.IsEmpty());

    // Clear 之后可写空间恢复为满容量（seek 时要靠它丢掉旧数据）
    ring.Write(in.data(), 12);
    ring.Clear();
    CHECK(ring.IsEmpty());
    CHECK_EQ_INT(ring.Space(), 16);

    // Reset 换容量
    ring.Reset(32);
    CHECK_EQ_INT(ring.Capacity(), 32);
    CHECK(ring.IsEmpty());
}

// 连续写入 / 读出大量数据，验证多轮回绕后数据仍然按顺序完整
static void TestRingBufferStreaming()
{
    std::printf("CAudioRingBuffer 连续流\n");

    const size_t kCapacity = 100;
    const size_t kTotal = 10000;
    CAudioRingBuffer ring(kCapacity);

    size_t written = 0;
    size_t read = 0;
    uint8_t next_expected = 0;
    // 用互质的块大小，保证读写边界会错开，充分制造回绕
    const size_t write_chunk = 37;
    const size_t read_chunk = 23;

    std::vector<uint8_t> out(read_chunk);
    while (read < kTotal)
    {
        if (written < kTotal)
        {
            size_t want = std::min(write_chunk, kTotal - written);
            std::vector<uint8_t> chunk(want);
            for (size_t i = 0; i < want; ++i)
                chunk[i] = static_cast<uint8_t>((written + i) & 0xFF);
            written += ring.Write(chunk.data(), want);
        }

        size_t got = ring.Read(out.data(), read_chunk);
        for (size_t i = 0; i < got; ++i)
        {
            if (out[i] != next_expected)
            {
                CHECK_EQ_INT(out[i], next_expected);
                return;                 // 一处错位后续必然连锁，及时停止
            }
            next_expected = static_cast<uint8_t>((next_expected + 1) & 0xFF);
        }
        read += got;

        if (got == 0 && written >= kTotal && ring.IsEmpty())
            break;
    }
    CHECK_EQ_INT(read, kTotal);
}

// 真正开两个线程跑，配合 TSan/ASan 时能暴露竞争；即使不开也能验证不会死锁或丢数据
static void TestRingBufferThreaded()
{
    std::printf("CAudioRingBuffer 双线程\n");

    const size_t kTotal = 200000;
    CAudioRingBuffer ring(4096);
    std::atomic<bool> producer_done{ false };

    std::thread producer([&]() {
        size_t written = 0;
        std::vector<uint8_t> chunk(512);
        while (written < kTotal)
        {
            size_t want = std::min(chunk.size(), kTotal - written);
            for (size_t i = 0; i < want; ++i)
                chunk[i] = static_cast<uint8_t>((written + i) & 0xFF);
            size_t n = ring.Write(chunk.data(), want);
            written += n;
            if (n == 0)
                std::this_thread::yield();
        }
        producer_done.store(true);
    });

    size_t read = 0;
    bool order_ok = true;
    uint8_t expected = 0;
    std::vector<uint8_t> out(333);
    while (read < kTotal)
    {
        size_t n = ring.Read(out.data(), out.size());
        for (size_t i = 0; i < n; ++i)
        {
            if (out[i] != expected)
                order_ok = false;
            expected = static_cast<uint8_t>((expected + 1) & 0xFF);
        }
        read += n;
        if (n == 0)
        {
            if (producer_done.load() && ring.IsEmpty())
                break;
            std::this_thread::yield();
        }
    }
    producer.join();

    CHECK_EQ_INT(read, kTotal);
    CHECK(order_ok);
}

void RunAudioTests()
{
    TestRingBufferBasics();
    TestRingBufferWrap();
    TestRingBufferOverflowUnderflow();
    TestRingBufferDiscardAndClear();
    TestRingBufferStreaming();
    TestRingBufferThreaded();
}
