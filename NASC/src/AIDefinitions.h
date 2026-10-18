#pragma once

#include <array>
#include <string_view>

namespace nasc {

// Word lists of the original compiler. Each word is added to the lexer
// definitions with its zero-based index as the value.
inline constexpr std::array<std::u16string_view, 18> playerStateWords{
    u"PSTATE_IDLE",
    u"PSTATE_MOVE_AROUND",
    u"PSTATE_DO_NOTHING",
    u"PSTATE_ATTACK",
    u"PSTATE_CHASE",
    u"PSTATE_FLEE",
    u"PSTATE_GET_ITEM",
    u"PSTATE_FOLLOW",
    u"PSTATE_DECAYING",
    u"PSTATE_MOVE_TO_WAY_POINT",
    u"PSTATE_USE_SKILL",
    u"PSTATE_MOVE_TO",
    u"PSTATE_EFFECT_ACTION",
    u"PSTATE_EFFECT_ACTION2",
    u"PSTATE_PET_DEFAULT",
    u"PSTATE_MOVE_TO_TARGET",
    u"PSTATE_MOVE_SUPERPOINT",
    u"PSTATE_MOVE_FREEWAY",
};

inline constexpr std::array<std::u16string_view, 3> makerStateWords{
    u"MSTATE_NORMAL",
    u"MSTATE_BERSERK",
    u"MS_SILENCE",
};

inline constexpr std::array<std::u16string_view, 5> definitionWords{
    u"FRIEND",
    u"SAME_PARTY",
    u"SAME_BLOODPLEDGE",
    u"PC",
    u"NPC",
};

} // namespace nasc
