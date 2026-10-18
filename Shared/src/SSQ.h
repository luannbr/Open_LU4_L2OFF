#pragma once

#include <cstdint>

// Shared SSQ vocabulary used by L2NPC, CacheD and L2Server. These are
// structs with nested enums in the shipping PDB (instead of namespaces).
// NASC64_2015.pdb keeps every enumerator and the current CacheD machine
// code confirms the same 32-bit values at its SSQ persistence/wire boundary.
struct SSQPart {
    enum type : std::int32_t {
        NONE = 0,
        TWILIGHT = 1,
        DAWN = 2,
        MAX = 3,
    };

    struct range {
        enum : std::int32_t {
            min = TWILIGHT,
            max = DAWN,
        };
    };
};

struct SSQStatus {
    enum type : std::int32_t {
        NONE = 0,
        EVENT = 1,
        ACCOUNTING = 2,
        SEAL_EFFECT = 3,
        MAX = 4,
    };

    struct range {
        enum : std::int32_t {
            min = NONE,
            max = SEAL_EFFECT,
        };
    };
};

struct SSQPosition {
    enum type : std::int32_t {
        NONE = 0,
        JUNIOR = 1,
        SENIOR = 2,
        MAX = 3,
    };

    struct range {
        enum : std::int32_t {
            min = JUNIOR,
            max = SENIOR,
        };
    };
};

struct SSQSeal {
    enum type : std::int32_t {
        NONE = 0,
        AVARICE = 1,
        REVELATION = 2,
        STRIFE = 3,
        CHASTISEMENT = 4,
        AROUSAL = 5,
        DISASTER = 6,
        AMAGEDON = 7,
        MAX = 8,
    };

    struct range {
        enum : std::int32_t {
            min = AVARICE,
            max = AMAGEDON,
        };
    };
};

struct SSQItem {
    enum type : std::int32_t {
        TYPE_A = 0,
        TYPE_B = 1,
        TYPE_C = 2,
        MAX = 3,
    };
};

// Windows long is four bytes in this target. A fixed-width alias keeps the
// persisted and packet layouts identical on LP64 hosts as well.
using time_l2_t = std::int32_t;
