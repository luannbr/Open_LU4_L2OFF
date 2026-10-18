#include "L2MobileNoti.h"

#include "L2Core/src/inc/Guard.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

int MakeBufferL2MobileNoti::L2M_CREATE_NPC::MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6b;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &npcId, sizeof(npcId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_DESPAWN_NPC::MakeSendBuffer(unsigned char* buff, int maxBuffLength, int worldId, int npcId) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6c;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &npcId, sizeof(npcId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_DIE_BOSS_NPC::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int npcId
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6d;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &npcId, sizeof(npcId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_TOWER_DESTRUCT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    int residenceId,
    int towerId
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x28;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x73;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &x, sizeof(x));
    std::memcpy(buff + 0x10, &y, sizeof(y));
    std::memcpy(buff + 0x18, &z, sizeof(z));
    std::memcpy(buff + 0x20, &residenceId, sizeof(residenceId));
    std::memcpy(buff + 0x24, &towerId, sizeof(towerId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_DOOR_DESTRUCT::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int residenceId, int doorType, int damageGrade
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x14;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x74;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &residenceId, sizeof(residenceId));
    std::memcpy(buff + 0x0c, &doorType, sizeof(doorType));
    std::memcpy(buff + 0x10, &damageGrade, sizeof(damageGrade));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_SIEGE_START::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int residenceId
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x75;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &residenceId, sizeof(residenceId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_SIEGE_END::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int residenceId
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x76;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &residenceId, sizeof(residenceId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_SIEGE_SCORE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int residenceId,
    std::int64_t attackerKillScore,
    std::int64_t defenderKillScore
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x1c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x77;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &residenceId, sizeof(residenceId));
    std::memcpy(buff + 0x0c, &attackerKillScore, sizeof(attackerKillScore));
    std::memcpy(buff + 0x14, &defenderKillScore, sizeof(defenderKillScore));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_BOSS_REGEN::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int npcId, std::int64_t spawnTime
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x14;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x78;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &npcId, sizeof(npcId));
    std::memcpy(buff + 0x0c, &spawnTime, sizeof(spawnTime));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PLEDGE_WAR_DECLARE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int declareWorldId,
    int declarePledgeId,
    int declaredWorldId,
    int declaredPledgeId,
    int type
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x18;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7a;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &declareWorldId, sizeof(declareWorldId));
    std::memcpy(buff + 8, &declarePledgeId, sizeof(declarePledgeId));
    std::memcpy(buff + 0x0c, &declaredWorldId, sizeof(declaredWorldId));
    std::memcpy(buff + 0x10, &declaredPledgeId, sizeof(declaredPledgeId));
    std::memcpy(buff + 0x14, &type, sizeof(type));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_COMMISSION_TRADE_BUY::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int commissionId
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x0c;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x84;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &commissionId, sizeof(commissionId));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_GIVE_POST_RESULT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int presentId,
    int worldId,
    int charDBId,
    int itemTemplateId,
    std::int64_t itemAmount,
    bool isSuccess
) {
    L2_GUARD_FUNCTION();

    constexpr std::int16_t packetLen = 0x20;
    if (!buff || maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x86;
    const int boolValue = isSuccess == true ? 1 : 0;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &presentId, sizeof(presentId));
    std::memcpy(buff + 8, &worldId, sizeof(worldId));
    std::memcpy(buff + 0x0c, &charDBId, sizeof(charDBId));
    std::memcpy(buff + 0x10, &itemTemplateId, sizeof(itemTemplateId));
    std::memcpy(buff + 0x14, &itemAmount, sizeof(itemAmount));
    std::memcpy(buff + 0x1c, &boolValue, sizeof(boolValue));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_CHAR_LOG_IN::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    std::int64_t exp,
    int lev,
    bool isPCCafeUser,
    int type
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x14u);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x68;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    const int isPCCafeUserValue = isPCCafeUser == true ? 1 : 0;
    std::memcpy(buff + offset, &exp, sizeof(exp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &lev, sizeof(lev));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &isPCCafeUserValue, sizeof(isPCCafeUserValue));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &type, sizeof(type));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_CHAR_LOG_OUT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int aaccountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    std::int64_t exp,
    int lev,
    int charPlayTime
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x10u);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x69;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &aaccountId, sizeof(aaccountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &exp, sizeof(exp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &lev, sizeof(lev));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &charPlayTime, sizeof(charPlayTime));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PC_KILL_PC::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int dieUserWorldId,
    int dieUserDBId,
    const std::u16string dieUserName,
    int dieUserPledgeId,
    int killerWorldId,
    int killerDBId,
    const std::u16string killerName,
    int killerPledgeId,
    int type
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x0e;
    if (!dieUserName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(dieUserName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x0eu);
    if (!killerName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(killerName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 8u);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6a;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &dieUserWorldId, sizeof(dieUserWorldId));
    std::memcpy(buff + 8, &dieUserDBId, sizeof(dieUserDBId));

    std::int16_t offset = 0x0e;
    std::int16_t dieUserNameLength = 0;
    if (!dieUserName.empty()) {
        dieUserNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(dieUserName.size()) * 2u);
    }
    std::memcpy(buff + 0x0c, &dieUserNameLength, sizeof(dieUserNameLength));
    if (!dieUserName.empty()) {
        std::memcpy(buff + 0x0e, dieUserName.data(), static_cast<std::size_t>(dieUserNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(dieUserNameLength) + 0x10u);
    }

    std::memcpy(buff + offset, &dieUserPledgeId, sizeof(dieUserPledgeId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &killerWorldId, sizeof(killerWorldId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &killerDBId, sizeof(killerDBId));

    const std::int16_t killerNameOffset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x0cu);
    std::int16_t killerNameLength = 0;
    if (!killerName.empty()) {
        killerNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(killerName.size()) * 2u);
    }
    std::memcpy(buff + killerNameOffset, &killerNameLength, sizeof(killerNameLength));
    if (killerName.empty()) {
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x0eu);
    } else {
        const std::int16_t killerNameDataOffset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x0eu);
        std::memcpy(buff + killerNameDataOffset, killerName.data(), static_cast<std::size_t>(killerNameLength));
        offset = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(offset) + 0x10u + static_cast<std::uint16_t>(killerNameLength)
        );
    }

    std::memcpy(buff + offset, &killerPledgeId, sizeof(killerPledgeId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &type, sizeof(type));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PRIVATESTORE_CLOSE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x70;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_WORLD_CHAT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    const std::u16string contents
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 2u);
    if (!contents.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(contents.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x71;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::int16_t contentsLength = 0;
    if (!contents.empty()) {
        contentsLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(contents.size()) * 2u);
    }
    std::memcpy(buff + offset, &contentsLength, sizeof(contentsLength));
    if (!contents.empty()) {
        const std::int16_t contentsOffset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 2u);
        std::memcpy(buff + contentsOffset, contents.data(), static_cast<std::size_t>(contentsLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_FISHING_ITEM::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    int itemId,
    std::int64_t exp,
    std::int64_t sp
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x14u);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x72;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &itemId, sizeof(itemId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &exp, sizeof(exp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &sp, sizeof(sp));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_CHANAGE_PLEDGE_ANNOUNCE::MakeSendBuffer(
    unsigned char* buff, int maxBuffLength, int worldId, int pledgeId, const std::u16string contents
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x0e;
    if (!contents.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(contents.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x79;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &pledgeId, sizeof(pledgeId));

    std::int16_t contentsLength = 0;
    if (!contents.empty()) {
        contentsLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(contents.size()) * 2u);
    }
    std::memcpy(buff + 0x0c, &contentsLength, sizeof(contentsLength));
    if (!contents.empty()) {
        std::memcpy(buff + 0x0e, contents.data(), static_cast<std::size_t>(contentsLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_OLYMPIAD::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7d;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_PLEDGEGAME::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7e;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_CASTLE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7f;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_FORTRESS::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x80;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_AGIT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x81;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_ENTER_RAID_SERVER::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x82;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_GET_RAID_POINT::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    int npcClassId,
    int point,
    int totalPoint
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x0cu);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x83;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &npcClassId, sizeof(npcClassId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &point, sizeof(point));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &totalPoint, sizeof(totalPoint));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_SAY_PLEDGE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int pledgeId,
    int charId,
    const std::u16string charName,
    const std::u16string chatMsg
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x12;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 2u);
    if (!chatMsg.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(chatMsg.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0xca;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &pledgeId, sizeof(pledgeId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));

    std::int16_t offset = 0x12;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x10, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x12, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x14u);
    }

    std::int16_t chatMsgLength = 0;
    if (!chatMsg.empty()) {
        chatMsgLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(chatMsg.size()) * 2u);
    }
    std::memcpy(buff + offset, &chatMsgLength, sizeof(chatMsgLength));
    if (!chatMsg.empty()) {
        std::memcpy(buff + static_cast<std::int16_t>(offset + 2), chatMsg.data(), static_cast<std::size_t>(chatMsgLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_SAY_ALLIANCE::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int allianceId,
    int pledgeId,
    int charId,
    const std::u16string charName,
    const std::u16string chatMsg
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 2u);
    if (!chatMsg.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(chatMsg.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0xcc;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &allianceId, sizeof(allianceId));
    std::memcpy(buff + 0x0c, &pledgeId, sizeof(pledgeId));
    std::memcpy(buff + 0x10, &charId, sizeof(charId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::int16_t chatMsgLength = 0;
    if (!chatMsg.empty()) {
        chatMsgLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(chatMsg.size()) * 2u);
    }
    std::memcpy(buff + offset, &chatMsgLength, sizeof(chatMsgLength));
    if (!chatMsg.empty()) {
        std::memcpy(buff + static_cast<std::int16_t>(offset + 2), chatMsg.data(), static_cast<std::size_t>(chatMsgLength));
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

bool ReadBufferL2MobileNoti::Read_ML2_GIVE_POST(ML2_GIVE_POST& packet, unsigned char* buff, int offset, int packetBodyLen) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return true;
    }
    if (offset + 2 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.presentId, buff + offset + 2, sizeof(packet.presentId));
    if (offset + 6 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.worldId, buff + offset + 6, sizeof(packet.worldId));
    if (offset + 0x0a >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.charDBId, buff + offset + 0x0a, sizeof(packet.charDBId));
    if (offset + 0x0e >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.itemTemplateId, buff + offset + 0x0e, sizeof(packet.itemTemplateId));
    if (offset + 0x12 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.itemAmount, buff + offset + 0x12, sizeof(packet.itemAmount));
    if (offset + 0x1a >= packetBodyLen) {
        return false;
    }

    std::uint16_t mailTitleLength = 0;
    std::memcpy(&mailTitleLength, buff + offset + 0x1a, sizeof(mailTitleLength));
    int cursor = offset + 0x1c;
    if (mailTitleLength >= 0x2000) {
        packet.mailTitle.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(mailTitleLength) > 0) {
        const std::uint16_t terminator = 0;
        std::memcpy(buff + cursor + (static_cast<int>(mailTitleLength) & ~1), &terminator, sizeof(terminator));
        const auto* mailTitle = reinterpret_cast<const char16_t*>(buff + cursor);
        const std::size_t mailTitleUnits = std::char_traits<char16_t>::length(mailTitle);
        packet.mailTitle.assign(std::u16string_view(mailTitle, mailTitleUnits));
        cursor = offset + 0x1e + static_cast<std::int16_t>(mailTitleLength);
    }
    if (cursor >= packetBodyLen) {
        return false;
    }

    std::uint16_t mailBodyLength = 0;
    std::memcpy(&mailBodyLength, buff + cursor, sizeof(mailBodyLength));
    if (mailBodyLength >= 0x2000) {
        packet.mailBody.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(mailBodyLength) > 0) {
        const int mailBodyOffset = cursor + 2;
        const std::uint16_t terminator = 0;
        std::memcpy(buff + mailBodyOffset + (static_cast<int>(mailBodyLength) & ~1), &terminator, sizeof(terminator));
        const auto* mailBody = reinterpret_cast<const char16_t*>(buff + mailBodyOffset);
        const std::size_t mailBodyUnits = std::char_traits<char16_t>::length(mailBody);
        packet.mailBody.assign(std::u16string_view(mailBody, mailBodyUnits));
    }
    return true;
}

bool ReadBufferL2MobileNoti::Read_ML2_SAY_PLEDGE(
    ML2_SAY_PLEDGE& packet, unsigned char* buff, int offset, int packetBodyLen
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return true;
    }
    if (offset + 2 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.worldId, buff + offset + 2, sizeof(packet.worldId));
    if (offset + 6 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.pledgeId, buff + offset + 6, sizeof(packet.pledgeId));
    if (offset + 0x0a >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.charId, buff + offset + 0x0a, sizeof(packet.charId));
    if (offset + 0x0e >= packetBodyLen) {
        return false;
    }

    std::uint16_t charNameLength = 0;
    std::memcpy(&charNameLength, buff + offset + 0x0e, sizeof(charNameLength));
    int cursor = offset + 0x10;
    if (charNameLength >= 0x2000) {
        packet.charName.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(charNameLength) > 0) {
        const std::uint16_t terminator = 0;
        std::memcpy(buff + cursor + (static_cast<int>(charNameLength) & ~1), &terminator, sizeof(terminator));
        const auto* charName = reinterpret_cast<const char16_t*>(buff + cursor);
        const std::size_t charNameUnits = std::char_traits<char16_t>::length(charName);
        packet.charName.assign(std::u16string_view(charName, charNameUnits));
        cursor = offset + 0x12 + static_cast<std::int16_t>(charNameLength);
    }
    if (cursor >= packetBodyLen) {
        return false;
    }

    std::uint16_t chatMsgLength = 0;
    std::memcpy(&chatMsgLength, buff + cursor, sizeof(chatMsgLength));
    if (chatMsgLength >= 0x2000) {
        packet.chatMsg.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(chatMsgLength) > 0) {
        const int chatMsgOffset = cursor + 2;
        const std::uint16_t terminator = 0;
        std::memcpy(buff + chatMsgOffset + (static_cast<int>(chatMsgLength) & ~1), &terminator, sizeof(terminator));
        const auto* chatMsg = reinterpret_cast<const char16_t*>(buff + chatMsgOffset);
        const std::size_t chatMsgUnits = std::char_traits<char16_t>::length(chatMsg);
        packet.chatMsg.assign(std::u16string_view(chatMsg, chatMsgUnits));
    }
    return true;
}

bool ReadBufferL2MobileNoti::Read_ML2_SAY_ALLIANCE(
    ML2_SAY_ALLIANCE& packet, unsigned char* buff, int offset, int packetBodyLen
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return true;
    }
    if (offset + 2 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.worldId, buff + offset + 2, sizeof(packet.worldId));
    if (offset + 6 >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.allianceId, buff + offset + 6, sizeof(packet.allianceId));
    if (offset + 0x0a >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.pledgeId, buff + offset + 0x0a, sizeof(packet.pledgeId));
    if (offset + 0x0e >= packetBodyLen) {
        return false;
    }
    std::memcpy(&packet.charId, buff + offset + 0x0e, sizeof(packet.charId));
    if (offset + 0x12 >= packetBodyLen) {
        return false;
    }

    std::uint16_t charNameLength = 0;
    std::memcpy(&charNameLength, buff + offset + 0x12, sizeof(charNameLength));
    int cursor = offset + 0x14;
    if (charNameLength >= 0x2000) {
        packet.charName.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(charNameLength) > 0) {
        const std::uint16_t terminator = 0;
        std::memcpy(buff + cursor + (static_cast<int>(charNameLength) & ~1), &terminator, sizeof(terminator));
        const auto* charName = reinterpret_cast<const char16_t*>(buff + cursor);
        const std::size_t charNameUnits = std::char_traits<char16_t>::length(charName);
        packet.charName.assign(std::u16string_view(charName, charNameUnits));
        cursor = offset + 0x16 + static_cast<std::int16_t>(charNameLength);
    }
    if (cursor >= packetBodyLen) {
        return false;
    }

    std::uint16_t chatMsgLength = 0;
    std::memcpy(&chatMsgLength, buff + cursor, sizeof(chatMsgLength));
    if (chatMsgLength >= 0x2000) {
        packet.chatMsg.assign(std::u16string_view{});
        return false;
    }
    if (static_cast<std::int16_t>(chatMsgLength) > 0) {
        const int chatMsgOffset = cursor + 2;
        const std::uint16_t terminator = 0;
        std::memcpy(buff + chatMsgOffset + (static_cast<int>(chatMsgLength) & ~1), &terminator, sizeof(terminator));
        const auto* chatMsg = reinterpret_cast<const char16_t*>(buff + chatMsgOffset);
        const std::size_t chatMsgUnits = std::char_traits<char16_t>::length(chatMsg);
        packet.chatMsg.assign(std::u16string_view(chatMsg, chatMsgUnits));
    }
    return true;
}

MakeBufferL2MobileNoti::L2M_PARTY_MATCHING_LIST::partyRoomInfoTuple::partyRoomInfoTuple(
    std::u16string charNameValue,
    int lowerLevValue,
    int upperLevValue,
    int maxMemberValue,
    std::u16string titleValue,
    int currentMemberValue
) :
    charName(charNameValue),
    lowerLev(lowerLevValue),
    upperLev(upperLevValue),
    maxMember(maxMemberValue),
    title(titleValue),
    currentMember(currentMemberValue) {}

int MakeBufferL2MobileNoti::L2M_PRIVATESTORE_OPEN::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    std::int64_t x,
    std::int64_t y,
    std::int64_t z,
    int itemCount,
    int privateStoreType,
    std::vector<ItemListTuple> ItemList
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x22u);
    for (std::size_t index = 0; index < ItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6e;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &x, sizeof(x));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &y, sizeof(y));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &z, sizeof(z));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x18), &itemCount, sizeof(itemCount));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x1c), &privateStoreType, sizeof(privateStoreType));
    const std::int16_t itemListCount = static_cast<std::int16_t>(ItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x20), &itemListCount, sizeof(itemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x22u);
    if (itemListCount > 0 && !ItemList.empty()) {
        for (const ItemListTuple& item : ItemList) {
            std::memcpy(buff + offset, &item.itemId, sizeof(item.itemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.ItemDbId, sizeof(item.ItemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.price, sizeof(item.price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_USER_PING::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int accountId,
    int charId,
    int pledgeId,
    const std::u16string charName,
    std::int64_t exp,
    int lev,
    bool bPcCafe,
    int premiumType,
    int posX,
    int posY,
    int posZ,
    std::int64_t getExp,
    std::int64_t getSp,
    std::int64_t getAdena,
    int privateStoreType,
    std::vector<ItemListTuple> ItemList
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!charName.empty()) {
        const std::uint16_t measuredUnits = static_cast<std::uint16_t>(std::char_traits<char16_t>::length(charName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x3eu);
    for (std::size_t index = 0; index < ItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7c;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &accountId, sizeof(accountId));
    std::memcpy(buff + 0x0c, &charId, sizeof(charId));
    std::memcpy(buff + 0x10, &pledgeId, sizeof(pledgeId));

    std::int16_t offset = 0x16;
    std::int16_t charNameLength = 0;
    if (!charName.empty()) {
        charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(charName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &charNameLength, sizeof(charNameLength));
    if (!charName.empty()) {
        std::memcpy(buff + 0x16, charName.data(), static_cast<std::size_t>(charNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(charNameLength) + 0x18u);
    }

    const int pcCafeValue = bPcCafe == true ? 1 : 0;
    std::memcpy(buff + offset, &exp, sizeof(exp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &lev, sizeof(lev));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &pcCafeValue, sizeof(pcCafeValue));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &premiumType, sizeof(premiumType));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x14), &posX, sizeof(posX));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x18), &posY, sizeof(posY));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x1c), &posZ, sizeof(posZ));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x20), &getExp, sizeof(getExp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x28), &getSp, sizeof(getSp));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x30), &getAdena, sizeof(getAdena));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x38), &privateStoreType, sizeof(privateStoreType));
    const std::int16_t itemListCount = static_cast<std::int16_t>(ItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x3c), &itemListCount, sizeof(itemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x3eu);
    if (itemListCount > 0 && !ItemList.empty()) {
        for (const ItemListTuple& item : ItemList) {
            std::memcpy(buff + offset, &item.itemId, sizeof(item.itemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.itemDbId, sizeof(item.itemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.price, sizeof(item.price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int sellerworldId,
    int sellerAccountId,
    int sellerCharId,
    int sellerPledgeId,
    const std::u16string sellerCharName,
    int buyerworldId,
    int buyerAccountId,
    int buyerCharId,
    int buyerPledgeId,
    const std::u16string buyerCharName,
    int BuyItemCount,
    std::vector<BuyItemListTuple> BuyItemList,
    int privateStoreType,
    int RemainItemCount,
    std::vector<RemainItemListTuple> RemainItemList
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!sellerCharName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(sellerCharName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x12u);
    if (!buyerCharName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(buyerCharName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 6u);
    for (std::size_t index = 0; index < BuyItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x0au);
    for (std::size_t index = 0; index < RemainItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x6f;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &sellerworldId, sizeof(sellerworldId));
    std::memcpy(buff + 8, &sellerAccountId, sizeof(sellerAccountId));
    std::memcpy(buff + 0x0c, &sellerCharId, sizeof(sellerCharId));
    std::memcpy(buff + 0x10, &sellerPledgeId, sizeof(sellerPledgeId));

    std::int16_t offset = 0x16;
    std::int16_t sellerCharNameLength = 0;
    if (!sellerCharName.empty()) {
        sellerCharNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(sellerCharName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &sellerCharNameLength, sizeof(sellerCharNameLength));
    if (!sellerCharName.empty()) {
        std::memcpy(buff + 0x16, sellerCharName.data(), static_cast<std::size_t>(sellerCharNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(sellerCharNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &buyerworldId, sizeof(buyerworldId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &buyerAccountId, sizeof(buyerAccountId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &buyerCharId, sizeof(buyerCharId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &buyerPledgeId, sizeof(buyerPledgeId));
    std::int16_t buyerCharNameLength = 0;
    if (!buyerCharName.empty()) {
        buyerCharNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(buyerCharName.size()) * 2u);
    }
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &buyerCharNameLength, sizeof(buyerCharNameLength));
    if (buyerCharName.empty()) {
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x12u);
    } else {
        std::memcpy(
            buff + static_cast<std::int16_t>(offset + 0x12), buyerCharName.data(),
            static_cast<std::size_t>(buyerCharNameLength)
        );
        offset = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(offset) + 0x14u + static_cast<std::uint16_t>(buyerCharNameLength)
        );
    }

    std::memcpy(buff + offset, &BuyItemCount, sizeof(BuyItemCount));
    const std::int16_t buyItemListCount = static_cast<std::int16_t>(BuyItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &buyItemListCount, sizeof(buyItemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 6u);
    if (buyItemListCount > 0 && !BuyItemList.empty()) {
        for (const BuyItemListTuple& item : BuyItemList) {
            std::memcpy(buff + offset, &item.ItemId, sizeof(item.ItemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.ItemDbId, sizeof(item.ItemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.Price, sizeof(item.Price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }

    std::memcpy(buff + offset, &privateStoreType, sizeof(privateStoreType));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &RemainItemCount, sizeof(RemainItemCount));
    const std::int16_t remainItemListCount = static_cast<std::int16_t>(RemainItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &remainItemListCount, sizeof(remainItemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x0au);
    if (remainItemListCount > 0 && !RemainItemList.empty()) {
        for (const RemainItemListTuple& item : RemainItemList) {
            std::memcpy(buff + offset, &item.ItemId, sizeof(item.ItemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.ItemDbId, sizeof(item.ItemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.Price, sizeof(item.Price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PRIVATESTORE_BUY_BUY::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int buyerworldId,
    int buyerAccountId,
    int buyerCharId,
    int buyerPledgeId,
    const std::u16string buyerCharName,
    int sellerworldId,
    int sellerAccountId,
    int sellerCharId,
    int sellerPledgeId,
    const std::u16string sellerCharName,
    int SellItemCount,
    std::vector<SellItemListTuple> SellItemList,
    int RemainBuyItemCount,
    std::vector<RemainBuyItemListTuple> RemainBuyItemList
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x16;
    if (!buyerCharName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(buyerCharName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x12u);
    if (!sellerCharName.empty()) {
        const std::uint16_t measuredUnits =
            static_cast<std::uint16_t>(std::char_traits<char16_t>::length(sellerCharName.data()));
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 6u);
    for (std::size_t index = 0; index < SellItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 6u);
    for (std::size_t index = 0; index < RemainBuyItemList.size(); ++index) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x18u);
    }
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x85;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &buyerworldId, sizeof(buyerworldId));
    std::memcpy(buff + 8, &buyerAccountId, sizeof(buyerAccountId));
    std::memcpy(buff + 0x0c, &buyerCharId, sizeof(buyerCharId));
    std::memcpy(buff + 0x10, &buyerPledgeId, sizeof(buyerPledgeId));

    std::int16_t offset = 0x16;
    std::int16_t buyerCharNameLength = 0;
    if (!buyerCharName.empty()) {
        buyerCharNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(buyerCharName.size()) * 2u);
    }
    std::memcpy(buff + 0x14, &buyerCharNameLength, sizeof(buyerCharNameLength));
    if (!buyerCharName.empty()) {
        std::memcpy(buff + 0x16, buyerCharName.data(), static_cast<std::size_t>(buyerCharNameLength));
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(buyerCharNameLength) + 0x18u);
    }

    std::memcpy(buff + offset, &sellerworldId, sizeof(sellerworldId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &sellerAccountId, sizeof(sellerAccountId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &sellerCharId, sizeof(sellerCharId));
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &sellerPledgeId, sizeof(sellerPledgeId));
    std::int16_t sellerCharNameLength = 0;
    if (!sellerCharName.empty()) {
        sellerCharNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(sellerCharName.size()) * 2u);
    }
    std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &sellerCharNameLength, sizeof(sellerCharNameLength));
    if (sellerCharName.empty()) {
        offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x12u);
    } else {
        std::memcpy(
            buff + static_cast<std::int16_t>(offset + 0x12), sellerCharName.data(),
            static_cast<std::size_t>(sellerCharNameLength)
        );
        offset = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(offset) + 0x14u + static_cast<std::uint16_t>(sellerCharNameLength)
        );
    }

    std::memcpy(buff + offset, &SellItemCount, sizeof(SellItemCount));
    const std::int16_t sellItemListCount = static_cast<std::int16_t>(SellItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &sellItemListCount, sizeof(sellItemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 6u);
    if (sellItemListCount > 0 && !SellItemList.empty()) {
        for (const SellItemListTuple& item : SellItemList) {
            std::memcpy(buff + offset, &item.ItemId, sizeof(item.ItemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.ItemDbId, sizeof(item.ItemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.Price, sizeof(item.Price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }

    std::memcpy(buff + offset, &RemainBuyItemCount, sizeof(RemainBuyItemCount));
    const std::int16_t remainBuyItemListCount = static_cast<std::int16_t>(RemainBuyItemList.size());
    std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &remainBuyItemListCount, sizeof(remainBuyItemListCount));
    offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 6u);
    if (remainBuyItemListCount > 0 && !RemainBuyItemList.empty()) {
        for (const RemainBuyItemListTuple& item : RemainBuyItemList) {
            std::memcpy(buff + offset, &item.ItemId, sizeof(item.ItemId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &item.ItemDbId, sizeof(item.ItemDbId));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &item.Price, sizeof(item.Price));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x10), &item.count, sizeof(item.count));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x18u);
        }
    }
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}

int MakeBufferL2MobileNoti::L2M_PARTY_MATCHING_LIST::MakeSendBuffer(
    unsigned char* buff,
    int maxBuffLength,
    int worldId,
    int listCount,
    std::vector<partyRoomInfoTuple> partyRoomInfo,
    bool firstPacket
) {
    L2_GUARD_FUNCTION();

    if (!buff) {
        return INVALID_SEND_BUFFER_LEN;
    }

    std::int16_t packetLen = 0x0e;
    for (const partyRoomInfoTuple& room : partyRoomInfo) {
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 2u);
        if (!room.charName.empty()) {
            const std::uint16_t measuredUnits =
                static_cast<std::uint16_t>(std::char_traits<char16_t>::length(room.charName.data()));
            packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
        }
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 0x0eu);
        if (!room.title.empty()) {
            const std::uint16_t measuredUnits =
                static_cast<std::uint16_t>(std::char_traits<char16_t>::length(room.title.data()));
            packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + measuredUnits * 2u + 2u);
        }
        packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 4u);
    }
    packetLen = static_cast<std::int16_t>(static_cast<std::uint16_t>(packetLen) + 4u);
    if (maxBuffLength <= packetLen) {
        return INVALID_SEND_BUFFER_LEN;
    }

    constexpr std::int16_t packetId = 0x7b;
    std::memcpy(buff + 2, &packetId, sizeof(packetId));
    std::memcpy(buff + 4, &worldId, sizeof(worldId));
    std::memcpy(buff + 8, &listCount, sizeof(listCount));
    const std::int16_t roomCount = static_cast<std::int16_t>(partyRoomInfo.size());
    std::memcpy(buff + 0x0c, &roomCount, sizeof(roomCount));

    std::int16_t offset = 0x0e;
    if (roomCount > 0 && !partyRoomInfo.empty()) {
        for (const partyRoomInfoTuple& room : partyRoomInfo) {
            std::int16_t charNameLength = 0;
            if (!room.charName.empty()) {
                charNameLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(room.charName.size()) * 2u);
            }
            std::memcpy(buff + offset, &charNameLength, sizeof(charNameLength));
            if (room.charName.empty()) {
                offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 2u);
            } else {
                std::memcpy(
                    buff + static_cast<std::int16_t>(offset + 2), room.charName.data(),
                    static_cast<std::size_t>(charNameLength)
                );
                offset = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(offset) + 4u + static_cast<std::uint16_t>(charNameLength)
                );
            }

            std::memcpy(buff + offset, &room.lowerLev, sizeof(room.lowerLev));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 4), &room.upperLev, sizeof(room.upperLev));
            std::memcpy(buff + static_cast<std::int16_t>(offset + 8), &room.maxMember, sizeof(room.maxMember));
            std::int16_t titleLength = 0;
            if (!room.title.empty()) {
                titleLength = static_cast<std::int16_t>(static_cast<std::uint16_t>(room.title.size()) * 2u);
            }
            std::memcpy(buff + static_cast<std::int16_t>(offset + 0x0c), &titleLength, sizeof(titleLength));
            if (room.title.empty()) {
                offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 0x0eu);
            } else {
                std::memcpy(
                    buff + static_cast<std::int16_t>(offset + 0x0e), room.title.data(), static_cast<std::size_t>(titleLength)
                );
                offset = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(offset) + 0x10u + static_cast<std::uint16_t>(titleLength)
                );
            }
            std::memcpy(buff + offset, &room.currentMember, sizeof(room.currentMember));
            offset = static_cast<std::int16_t>(static_cast<std::uint16_t>(offset) + 4u);
        }
    }

    const int firstPacketValue = firstPacket == true ? 1 : 0;
    std::memcpy(buff + offset, &firstPacketValue, sizeof(firstPacketValue));
    std::memcpy(buff, &packetLen, sizeof(packetLen));
    return packetLen;
}
