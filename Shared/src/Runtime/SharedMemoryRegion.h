// Cross-platform views of the named mappings consumed by SharedType.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>


enum class SharedMemoryCreateMode {
    NormalizedFullExtent,
    LiteralRequestedExtent, // Existing shared-factory producer contract.
};

struct SharedMemoryViewSpec {
    void* data = nullptr;
    std::size_t bytes = 0;
    std::shared_ptr<void> lifetime;
    void* nativeHandle = nullptr; // Actual Windows HANDLE. Never a POSIX fd.
};

class SharedMemoryRegion {
public:
    bool AttachView(SharedMemoryViewSpec view, std::size_t minimumBytes, std::string* error = nullptr);
    bool AttachNamed(const std::string& name, std::size_t minimumBytes, std::string* error = nullptr);
    bool CreateNamed(const std::string& name, std::size_t bytes, std::string* error = nullptr);

    bool CreateNamed(const wchar_t* name, std::size_t bytes, SharedMemoryCreateMode mode, std::string* error = nullptr);

    // Host-side metadata. No field is added to a recovered factory.
    void* NativeHandle() const noexcept { return nativeHandle_; }

    void Reset();
    bool IsAttached() const { return data_ != nullptr; }
    std::size_t Size() const { return bytes_; }
    void* Data() { return data_; }
    const void* Data() const { return data_; }

    template <typename T>
    bool Read(std::size_t offset, T& value) const {
        static_assert(std::is_trivially_copyable<T>::value, "shared-memory fields must be trivially copyable");
        if (!Contains(offset, sizeof(T))) {
            return false;
        }
        std::memcpy(&value, static_cast<const std::uint8_t*>(data_) + offset, sizeof(T));
        return true;
    }

    template <typename T>
    bool Write(std::size_t offset, const T& value) {
        static_assert(std::is_trivially_copyable<T>::value, "shared-memory fields must be trivially copyable");
        if (!Contains(offset, sizeof(T))) {
            return false;
        }
        std::memcpy(static_cast<std::uint8_t*>(data_) + offset, &value, sizeof(T));
        return true;
    }

    bool ReadBytes(std::size_t offset, void* output, std::size_t count) const;
    bool WriteBytes(std::size_t offset, const void* input, std::size_t count);

private:
    bool CreateNamedImpl(
        const std::string& name, const wchar_t* wideName, std::size_t bytes, SharedMemoryCreateMode mode, std::string* error
    );

    bool Contains(std::size_t offset, std::size_t count) const;

    void* nativeHandle_ = nullptr;
    void* data_ = nullptr;
    std::size_t bytes_ = 0;
    std::shared_ptr<void> lifetime_;
};
