#pragma once

// NCSoft's L2Core runtime extends the MSVC standard-library namespace with
// these key carriers. Current L2NPC, CacheD and L2Server instantiate them.
// CodeView records the physical owner as Server/Main/L2Core/inc/xstd.h. The
// namespace, member names and ordering are also visible in PDB names and in
// the generated map-comparison machine code.
namespace std {

template <typename TPrimary, typename TSecondary>
struct CDualKey {
    TPrimary m_PrimaryKey{};
    TSecondary m_SecondaryKey{};

    CDualKey() = default;

    CDualKey(TPrimary primaryKey, TSecondary secondaryKey) : m_PrimaryKey(primaryKey), m_SecondaryKey(secondaryKey) {}

    bool operator<(const CDualKey& other) const {
        return m_PrimaryKey < other.m_PrimaryKey ||
            (m_PrimaryKey == other.m_PrimaryKey && m_SecondaryKey < other.m_SecondaryKey);
    }

    bool operator>(const CDualKey& other) const { return other < *this; }

    bool operator==(const CDualKey& other) const {
        return m_PrimaryKey == other.m_PrimaryKey && m_SecondaryKey == other.m_SecondaryKey;
    }
};

template <typename TPrimaryFirst, typename TPrimarySecond, typename TSecondary>
struct CTriKey {
    CDualKey<TPrimaryFirst, TPrimarySecond> m_PrimaryKey{};
    TSecondary m_SecondaryKey{};

    CTriKey() = default;

    CTriKey(TPrimaryFirst primaryFirst, TPrimarySecond primarySecond, TSecondary secondary) :
        m_PrimaryKey(primaryFirst, primarySecond), m_SecondaryKey(secondary) {}

    bool operator<(const CTriKey& other) const {
        if (m_PrimaryKey < other.m_PrimaryKey) {
            return true;
        }
        if (!(m_PrimaryKey == other.m_PrimaryKey)) {
            return false;
        }
        return m_SecondaryKey < other.m_SecondaryKey;
    }

    bool operator>(const CTriKey& other) const { return other < *this; }

    bool operator==(const CTriKey& other) const {
        return m_PrimaryKey == other.m_PrimaryKey && m_SecondaryKey == other.m_SecondaryKey;
    }
};

} // namespace std
