#ifndef OPENMW_MP_PACKETSTREAM_HPP
#define OPENMW_MP_PACKETSTREAM_HPP

#include <memory>
#include <cstddef>

namespace mwmp
{
    class PacketStreamImpl;

    class PacketStream
    {
    public:
        PacketStream();
        PacketStream(unsigned char* data, unsigned int lengthInBytes);
        ~PacketStream();

        PacketStream(PacketStream&&) noexcept;
        PacketStream& operator=(PacketStream&&) noexcept;

        PacketStream(const PacketStream&) = delete;
        PacketStream& operator=(const PacketStream&) = delete;

        template <class T>
        void Write(const T& data)
        {
            WriteRaw(&data, sizeof(T));
        }

        void Write(bool data);

        template <class T>
        bool Read(T& data)
        {
            return ReadRaw(&data, sizeof(T));
        }

        bool Read(bool& data);

        void Write(const char* data, unsigned int numberOfBytes);
        void Write(char* data, unsigned int numberOfBytes);
        bool Read(char* data, unsigned int numberOfBytes);

        template <class T>
        void Write(const T& data, unsigned int numberOfBytes)
        {
            Write(reinterpret_cast<const char*>(&data), numberOfBytes);
        }

        template <class T>
        bool Read(T& data, unsigned int numberOfBytes)
        {
            return Read(reinterpret_cast<char*>(&data), numberOfBytes);
        }

        template <class T>
        void WriteCompressed(const T& data)
        {
            WriteCompressedRaw(&data, sizeof(T));
        }

        void WriteCompressed(bool data);
        void WriteCompressed(float data);
        void WriteCompressed(double data);

        template <class T>
        bool ReadCompressed(T& data)
        {
            return ReadCompressedRaw(&data, sizeof(T));
        }

        bool ReadCompressed(bool& data);
        bool ReadCompressed(float& data);
        bool ReadCompressed(double& data);

        void Reset();
        void ResetReadPointer();
        void ResetWritePointer();
        void IgnoreBytes(unsigned int numberOfBytes);

        unsigned char* data();
        const unsigned char* data() const;
        std::size_t size() const;

    private:
        void WriteRaw(const void* data, std::size_t size);
        bool ReadRaw(void* data, std::size_t size);
        void WriteCompressedRaw(const void* data, std::size_t size);
        bool ReadCompressedRaw(void* data, std::size_t size);

        std::unique_ptr<PacketStreamImpl> mImpl;
    };
}

#endif
