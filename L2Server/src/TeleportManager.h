#pragma once

#include "NPC.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class User;

enum TeleporterType : std::int32_t {
    TELEPORTER_NONE = 0,
    TELEPORTER_COMMON = 1,
    TELEPORTER_CORETIME = 2,
    TELEPORTER_FREE = 3,
    TELEPORTER_MAX = 4,
};


extern std::int16_t TeleporterTypeMask[TELEPORTER_MAX];

struct TelPos {
    std::int32_t m_nNameIndex;
    std::array<std::byte, 0x04> m_alignment04To08;
    char16_t* m_pszName;
    std::int32_t m_nX;
    std::int32_t m_nY;
    std::int32_t m_nZ;
    std::int32_t m_nPrice;
    std::int32_t m_nCoretimePrice;
    std::array<std::byte, 0x04> m_alignment24To28;
    std::vector<std::int32_t> m_vResidenceIdList;
};

extern std::map<std::int64_t, std::vector<TelPos>*> s_mapTelPosList;
extern CIOCriticalSection s_mapTelPosListSect;


class CTeleporter : public CNPC {
public:

    CTeleporter();
    CTeleporter(const CTeleporter& other);
    ~CTeleporter() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsTeleporter() const override;
    CTeleporter* CastTeleporter() override;
    void SetType(TeleporterType type, int value);
    TeleporterType CanTeleport(User* user);
    int GetParam(int type) { return m_param[static_cast<std::size_t>(type)]; }
    std::int16_t GetTeleporterFlag() { return m_teleporterFlag; }

private:
    std::int16_t m_teleporterFlag; // +0x3d78
    [[maybe_unused]] std::array<std::byte, 0x02> m_alignment3d7aTo3d7c;
    std::array<std::int32_t, TELEPORTER_MAX> m_param; // +0x3d7c
};
