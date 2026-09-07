#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// 解码线程与音频回调之间的环形缓冲区。
//
// 单生产者（解码线程）单消费者（SDL 音频回调）。用互斥量而不是无锁实现：
// 临界区只有 memcpy，持锁时间极短，而无锁环形缓冲的内存序很容易写错。
//
// 这个类不依赖 SDL、libnx 或 libFLAC，因此可以在开发机上单独测试 ——
// 环形缓冲的回绕边界是最容易出差一错误的地方，值得单独覆盖。
class CAudioRingBuffer
{
public:
    explicit CAudioRingBuffer(size_t capacity_bytes = 0);

    // 重新分配容量并清空内容
    void Reset(size_t capacity_bytes);

    // 写入最多 bytes 字节，返回实际写入量（剩余空间不足时会少写）
    size_t Write(const uint8_t* data, size_t bytes);
    // 读出最多 bytes 字节，返回实际读出量（数据不足时会少读）
    size_t Read(uint8_t* out, size_t bytes);
    // 丢弃最多 bytes 字节，返回实际丢弃量
    size_t Discard(size_t bytes);

    size_t Available() const;       // 可读字节数
    size_t Space() const;           // 可写字节数
    size_t Capacity() const;
    bool IsEmpty() const;
    bool IsFull() const;

    void Clear();

private:
    size_t AvailableLocked() const { return m_size; }

    mutable std::mutex m_mutex;
    std::vector<uint8_t> m_data;
    size_t m_read_pos{};
    size_t m_write_pos{};
    size_t m_size{};                // 当前已用字节数，用它区分"满"和"空"
};
