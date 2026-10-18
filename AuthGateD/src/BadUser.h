#pragma once

#include <cstddef>
#include <cstdint>

class CBadUser {
public:
    CBadUser();
    virtual ~CBadUser();

    CBadUser(const CBadUser&) = delete;
    CBadUser& operator=(const CBadUser&) = delete;

    bool Init(std::uint16_t maxPenaltySeconds);
    std::uint32_t Add(const char* userId);

private:
    struct Node {
        char userId[16];
        std::uint32_t cntBadConn;
        Node* next;
    };

    static constexpr std::size_t kBucketCount = 100000;

    std::uint32_t hash(const char* userId);
    void ReleaseTable();

    Node** mpData = nullptr;
    std::uint16_t mMaxPenaltySeconds = 0;
};
