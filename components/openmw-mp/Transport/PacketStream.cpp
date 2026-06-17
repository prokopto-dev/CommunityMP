#include "PacketStream.hpp"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <vector>

namespace mwmp
{
    namespace
    {
        std::size_t bytesForBits(std::size_t bits)
        {
            return (bits + 7) >> 3;
        }

        bool doEndianSwap()
        {
            const std::uint16_t value = 1;
            return *reinterpret_cast<const unsigned char*>(&value) == 1;
        }

        void reverseBytes(const unsigned char* input, unsigned char* output, std::size_t size)
        {
            for (std::size_t i = 0; i < size; ++i)
                output[i] = input[size - i - 1];
        }
    }

    class PacketStreamImpl
    {
    public:
        PacketStreamImpl() = default;

        PacketStreamImpl(unsigned char* data, unsigned int lengthInBytes)
        {
            if (data == nullptr || lengthInBytes == 0)
                return;

            mData.assign(data, data + lengthInBytes);
            mNumberOfBitsUsed = static_cast<std::size_t>(lengthInBytes) << 3;
        }

        void write(bool value)
        {
            ensureBits(1);

            const std::size_t bitOffset = mNumberOfBitsUsed & 7;
            if (bitOffset == 0)
                mData[mNumberOfBitsUsed >> 3] = value ? 0x80 : 0;
            else if (value)
                mData[mNumberOfBitsUsed >> 3] |= static_cast<unsigned char>(0x80 >> bitOffset);

            ++mNumberOfBitsUsed;
        }

        bool read(bool& value)
        {
            if (mReadOffset + 1 > mNumberOfBitsUsed)
                return false;

            value = (mData[mReadOffset >> 3] & (0x80 >> (mReadOffset & 7))) != 0;
            ++mReadOffset;
            return true;
        }

        void writeBytes(const char* input, unsigned int numberOfBytes)
        {
            if (numberOfBytes == 0)
                return;

            const std::size_t numberOfBits = static_cast<std::size_t>(numberOfBytes) << 3;
            if ((mNumberOfBitsUsed & 7) == 0)
            {
                ensureBits(numberOfBits);
                std::memcpy(mData.data() + bytesForBits(mNumberOfBitsUsed), input, numberOfBytes);
                mNumberOfBitsUsed += numberOfBits;
                return;
            }

            writeBits(reinterpret_cast<const unsigned char*>(input), numberOfBits, true);
        }

        bool readBytes(char* output, unsigned int numberOfBytes)
        {
            if (numberOfBytes == 0)
                return true;

            const std::size_t numberOfBits = static_cast<std::size_t>(numberOfBytes) << 3;
            if ((mReadOffset & 7) == 0)
            {
                if (mReadOffset + numberOfBits > mNumberOfBitsUsed)
                    return false;

                std::memcpy(output, mData.data() + (mReadOffset >> 3), numberOfBytes);
                mReadOffset += numberOfBits;
                return true;
            }

            return readBits(reinterpret_cast<unsigned char*>(output), numberOfBits, true);
        }

        void writeBits(const unsigned char* input, std::size_t numberOfBitsToWrite, bool rightAlignedBits)
        {
            ensureBits(numberOfBitsToWrite);

            const std::size_t numberOfBitsUsedMod8 = mNumberOfBitsUsed & 7;
            if (numberOfBitsUsedMod8 == 0 && (numberOfBitsToWrite & 7) == 0)
            {
                std::memcpy(mData.data() + (mNumberOfBitsUsed >> 3), input, numberOfBitsToWrite >> 3);
                mNumberOfBitsUsed += numberOfBitsToWrite;
                return;
            }

            const unsigned char* inputPtr = input;
            while (numberOfBitsToWrite > 0)
            {
                unsigned char dataByte = *(inputPtr++);
                if (numberOfBitsToWrite < 8 && rightAlignedBits)
                    dataByte <<= 8 - numberOfBitsToWrite;

                if (numberOfBitsUsedMod8 == 0)
                    mData[mNumberOfBitsUsed >> 3] = dataByte;
                else
                {
                    mData[mNumberOfBitsUsed >> 3] |= dataByte >> numberOfBitsUsedMod8;

                    if (8 - numberOfBitsUsedMod8 < 8 && 8 - numberOfBitsUsedMod8 < numberOfBitsToWrite)
                        mData[(mNumberOfBitsUsed >> 3) + 1] = dataByte << (8 - numberOfBitsUsedMod8);
                }

                if (numberOfBitsToWrite >= 8)
                {
                    mNumberOfBitsUsed += 8;
                    numberOfBitsToWrite -= 8;
                }
                else
                {
                    mNumberOfBitsUsed += numberOfBitsToWrite;
                    numberOfBitsToWrite = 0;
                }
            }
        }

        bool readBits(unsigned char* output, std::size_t numberOfBitsToRead, bool alignBitsToRight)
        {
            if (numberOfBitsToRead == 0)
                return true;

            if (mReadOffset + numberOfBitsToRead > mNumberOfBitsUsed)
                return false;

            const std::size_t readOffsetMod8 = mReadOffset & 7;
            if (readOffsetMod8 == 0 && (numberOfBitsToRead & 7) == 0)
            {
                std::memcpy(output, mData.data() + (mReadOffset >> 3), numberOfBitsToRead >> 3);
                mReadOffset += numberOfBitsToRead;
                return true;
            }

            std::memset(output, 0, bytesForBits(numberOfBitsToRead));

            std::size_t offset = 0;
            while (numberOfBitsToRead > 0)
            {
                output[offset] |= mData[mReadOffset >> 3] << readOffsetMod8;

                if (readOffsetMod8 > 0 && numberOfBitsToRead > 8 - readOffsetMod8)
                    output[offset] |= mData[(mReadOffset >> 3) + 1] >> (8 - readOffsetMod8);

                if (numberOfBitsToRead >= 8)
                {
                    numberOfBitsToRead -= 8;
                    mReadOffset += 8;
                }
                else
                {
                    const int neg = static_cast<int>(numberOfBitsToRead) - 8;
                    if (neg < 0)
                    {
                        if (alignBitsToRight)
                            output[offset] >>= -neg;

                        mReadOffset += 8 + neg;
                    }
                    else
                        mReadOffset += 8;

                    numberOfBitsToRead = 0;
                }

                ++offset;
            }

            return true;
        }

        void reset()
        {
            mNumberOfBitsUsed = 0;
            mReadOffset = 0;
        }

        void resetReadPointer()
        {
            mReadOffset = 0;
        }

        void resetWritePointer()
        {
            mNumberOfBitsUsed = 0;
        }

        void ignoreBytes(unsigned int numberOfBytes)
        {
            mReadOffset += static_cast<std::size_t>(numberOfBytes) << 3;
        }

        unsigned char* data()
        {
            return mData.data();
        }

        const unsigned char* data() const
        {
            return mData.data();
        }

        std::size_t size() const
        {
            return bytesForBits(mNumberOfBitsUsed);
        }

    private:
        void ensureBits(std::size_t numberOfBitsToWrite)
        {
            mData.resize(std::max(mData.size(), bytesForBits(mNumberOfBitsUsed + numberOfBitsToWrite)));
        }

        std::vector<unsigned char> mData;
        std::size_t mNumberOfBitsUsed = 0;
        std::size_t mReadOffset = 0;
    };

    namespace
    {
        void writeCompressedBytes(PacketStreamImpl& stream, const unsigned char* data, unsigned int sizeInBits)
        {
            unsigned int currentByte = (sizeInBits >> 3) - 1;

            while (currentByte > 0)
            {
                if (data[currentByte] == 0)
                    stream.write(true);
                else
                {
                    stream.write(false);
                    stream.writeBits(data, (currentByte + 1) << 3, true);
                    return;
                }

                currentByte--;
            }

            if ((*(data + currentByte) & 0xF0) == 0x00)
            {
                stream.write(true);
                stream.writeBits(data + currentByte, 4, true);
            }
            else
            {
                stream.write(false);
                stream.writeBits(data + currentByte, 8, true);
            }
        }

        bool readCompressedBytes(PacketStreamImpl& stream, unsigned char* data, unsigned int sizeInBits)
        {
            unsigned int currentByte = (sizeInBits >> 3) - 1;
            while (currentByte > 0)
            {
                bool isMatchedByte = false;
                if (!stream.read(isMatchedByte))
                    return false;

                if (isMatchedByte)
                {
                    data[currentByte] = 0;
                    currentByte--;
                }
                else
                    return stream.readBits(data, (currentByte + 1) << 3, true);
            }

            bool isMatchedHalfByte = false;
            if (!stream.read(isMatchedHalfByte))
                return false;

            if (isMatchedHalfByte)
            {
                if (!stream.readBits(data + currentByte, 4, true))
                    return false;
            }
            else if (!stream.readBits(data + currentByte, 8, true))
                return false;

            return true;
        }
    }

    PacketStream::PacketStream()
        : mImpl(std::make_unique<PacketStreamImpl>())
    {
    }

    PacketStream::PacketStream(unsigned char* data, unsigned int lengthInBytes)
        : mImpl(std::make_unique<PacketStreamImpl>(data, lengthInBytes))
    {
    }

    PacketStream::~PacketStream() = default;

    PacketStream::PacketStream(PacketStream&&) noexcept = default;

    PacketStream& PacketStream::operator=(PacketStream&&) noexcept = default;

    void PacketStream::Write(bool data)
    {
        mImpl->write(data);
    }

    bool PacketStream::Read(bool& data)
    {
        return mImpl->read(data);
    }

    void PacketStream::Write(const char* data, unsigned int numberOfBytes)
    {
        mImpl->writeBytes(data, numberOfBytes);
    }

    void PacketStream::Write(char* data, unsigned int numberOfBytes)
    {
        mImpl->writeBytes(data, numberOfBytes);
    }

    bool PacketStream::Read(char* data, unsigned int numberOfBytes)
    {
        return mImpl->readBytes(data, numberOfBytes);
    }

    void PacketStream::WriteCompressed(bool data)
    {
        Write(data);
    }

    void PacketStream::WriteCompressed(float data)
    {
        const float clamped = std::clamp(data, -1.0f, 1.0f);
        Write(static_cast<std::uint16_t>((clamped + 1.0f) * 32767.5f));
    }

    void PacketStream::WriteCompressed(double data)
    {
        const double clamped = std::clamp(data, -1.0, 1.0);
        Write(static_cast<std::uint32_t>((clamped + 1.0) * 2147483648.0));
    }

    bool PacketStream::ReadCompressed(bool& data)
    {
        return Read(data);
    }

    bool PacketStream::ReadCompressed(float& data)
    {
        std::uint16_t compressed = 0;
        if (!Read(compressed))
            return false;

        data = static_cast<float>(compressed) / 32767.5f - 1.0f;
        return true;
    }

    bool PacketStream::ReadCompressed(double& data)
    {
        std::uint32_t compressed = 0;
        if (!Read(compressed))
            return false;

        data = static_cast<double>(compressed) / 2147483648.0 - 1.0;
        return true;
    }

    void PacketStream::Reset()
    {
        mImpl->reset();
    }

    void PacketStream::ResetReadPointer()
    {
        mImpl->resetReadPointer();
    }

    void PacketStream::ResetWritePointer()
    {
        mImpl->resetWritePointer();
    }

    void PacketStream::IgnoreBytes(unsigned int numberOfBytes)
    {
        mImpl->ignoreBytes(numberOfBytes);
    }

    unsigned char* PacketStream::data()
    {
        return mImpl->data();
    }

    const unsigned char* PacketStream::data() const
    {
        return mImpl->data();
    }

    std::size_t PacketStream::size() const
    {
        return mImpl->size();
    }

    void PacketStream::WriteRaw(const void* data, std::size_t size)
    {
        if (size == 0)
            return;

        const auto* bytes = static_cast<const unsigned char*>(data);
        const auto sizeInBits = static_cast<unsigned int>(size * 8);

        if (size <= 1 || !doEndianSwap())
        {
            mImpl->writeBits(bytes, sizeInBits, true);
            return;
        }

        std::vector<unsigned char> output(size);
        reverseBytes(bytes, output.data(), size);
        mImpl->writeBits(output.data(), sizeInBits, true);
    }

    bool PacketStream::ReadRaw(void* data, std::size_t size)
    {
        if (size == 0)
            return true;

        auto* bytes = static_cast<unsigned char*>(data);
        const auto sizeInBits = static_cast<unsigned int>(size * 8);

        if (size <= 1 || !doEndianSwap())
            return mImpl->readBits(bytes, sizeInBits, true);

        std::vector<unsigned char> output(size);
        if (!mImpl->readBits(output.data(), sizeInBits, true))
            return false;

        reverseBytes(output.data(), bytes, size);
        return true;
    }

    void PacketStream::WriteCompressedRaw(const void* data, std::size_t size)
    {
        if (size == 0)
            return;

        const auto* bytes = static_cast<const unsigned char*>(data);
        const auto sizeInBits = static_cast<unsigned int>(size * 8);

        if (size <= 1 || !doEndianSwap())
        {
            writeCompressedBytes(*mImpl, bytes, sizeInBits);
            return;
        }

        std::vector<unsigned char> output(size);
        reverseBytes(bytes, output.data(), size);
        writeCompressedBytes(*mImpl, output.data(), sizeInBits);
    }

    bool PacketStream::ReadCompressedRaw(void* data, std::size_t size)
    {
        if (size == 0)
            return true;

        auto* bytes = static_cast<unsigned char*>(data);
        const auto sizeInBits = static_cast<unsigned int>(size * 8);

        if (size <= 1 || !doEndianSwap())
            return readCompressedBytes(*mImpl, bytes, sizeInBits);

        std::vector<unsigned char> output(size);
        if (!readCompressedBytes(*mImpl, output.data(), sizeInBits))
            return false;

        reverseBytes(output.data(), bytes, size);
        return true;
    }
}
