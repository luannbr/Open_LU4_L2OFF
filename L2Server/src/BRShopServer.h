#pragma once

#include "BRProductServer.h"

#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSocket;

class CShopServer : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CShopServer)

    CShopServer();
    ~CShopServer() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool Init();
    bool Load();
    void SendProductList(CSocket* socket, std::int64_t value1, std::int64_t value2);
    CProductSP GetProduct(int productId);
    bool PushProduct(CProduct* product);

    bool IsLoaded() const noexcept { return m_loaded; }
    bool ExistNewProduct() const noexcept { return m_newProduct; }
    void SetNewProduct(bool value) noexcept { m_newProduct = value; }

protected:
    void WriteLock(const wchar_t* file, int line);
    void WriteUnlock();
    void ReadLock(const wchar_t* file, int line);
    void ReadUnlock();

private:
    template <typename T>
    friend auto scoped_lock_detail::ReadLock(T& lock, const wchar_t* file, int line, int)
        -> decltype(lock.ReadLock(file, line), void());
    template <typename T>
    friend auto scoped_lock_detail::WriteLock(T& lock, const wchar_t* file, int line, int)
        -> decltype(lock.WriteLock(file, line), void());
    template <typename T>
    friend auto scoped_lock_detail::ReadUnlock(T& lock, const wchar_t*, int, int) -> decltype(lock.ReadUnlock(), void());
    template <typename T>
    friend auto scoped_lock_detail::WriteUnlock(T& lock, const wchar_t*, int, int) -> decltype(lock.WriteUnlock(), void());

    using ProductMap = CObjectMap<CProduct, 1, 5, RWLock, int>;

    RWLock m_lock; // +0x18
    ProductMap* m_products; // +0x50
    bool m_loaded; // +0x58
    bool m_newProduct; // +0x59
    std::array<std::byte, 0x06> m_tailPadding5aTo60; // +0x5a
};

extern CShopServer g_ShopServer;
