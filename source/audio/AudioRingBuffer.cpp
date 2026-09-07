#include "AudioRingBuffer.h"

#include <algorithm>
#include <cstring>

CAudioRingBuffer::CAudioRingBuffer(size_t capacity_bytes)
{
    if (capacity_bytes > 0)
        m_data.resize(capacity_bytes);
}

void CAudioRingBuffer::Reset(size_t capacity_bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.assign(capacity_bytes, 0);
    m_read_pos = 0;
    m_write_pos = 0;
    m_size = 0;
}

size_t CAudioRingBuffer::Write(const uint8_t* data, size_t bytes)
{
    if (data == nullptr || bytes == 0)
        return 0;

    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t capacity = m_data.size();
    if (capacity == 0)
        return 0;

    size_t to_write = std::min(bytes, capacity - m_size);
    if (to_write == 0)
        return 0;

    // 可能跨越缓冲区末尾，分两段拷贝
    size_t first = std::min(to_write, capacity - m_write_pos);
    std::memcpy(m_data.data() + m_write_pos, data, first);
    if (to_write > first)
        std::memcpy(m_data.data(), data + first, to_write - first);

    m_write_pos = (m_write_pos + to_write) % capacity;
    m_size += to_write;
    return to_write;
}

size_t CAudioRingBuffer::Read(uint8_t* out, size_t bytes)
{
    if (out == nullptr || bytes == 0)
        return 0;

    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t capacity = m_data.size();
    if (capacity == 0)
        return 0;

    size_t to_read = std::min(bytes, m_size);
    if (to_read == 0)
        return 0;

    size_t first = std::min(to_read, capacity - m_read_pos);
    std::memcpy(out, m_data.data() + m_read_pos, first);
    if (to_read > first)
        std::memcpy(out + first, m_data.data(), to_read - first);

    m_read_pos = (m_read_pos + to_read) % capacity;
    m_size -= to_read;
    return to_read;
}

size_t CAudioRingBuffer::Discard(size_t bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t capacity = m_data.size();
    if (capacity == 0)
        return 0;

    size_t to_discard = std::min(bytes, m_size);
    m_read_pos = (m_read_pos + to_discard) % capacity;
    m_size -= to_discard;
    return to_discard;
}

size_t CAudioRingBuffer::Available() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size;
}

size_t CAudioRingBuffer::Space() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size() - m_size;
}

size_t CAudioRingBuffer::Capacity() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.size();
}

bool CAudioRingBuffer::IsEmpty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size == 0;
}

bool CAudioRingBuffer::IsFull() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_size == m_data.size();
}

void CAudioRingBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_read_pos = 0;
    m_write_pos = 0;
    m_size = 0;
}
