#include "Shared/src/Log.h"

#include "Pet.h"

#include "CReadFile.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <string>

CPetDB g_petDb;

CPet::CPet() = default;

CPetWarehouseSP CPet::GetWarehouse() { return m_warehouse.GetObjectW(); }

std::uint32_t CPet::NpcClassId() const noexcept {
    // Source name kept by C1 CPet.h. Current CacheSocket.cpp inlines the
    // accessor as the direct CPet +0x1c load in opcodes 127 and 128.
    return m_npcClassId;
}

int CPet::Level() {
    // Current Cached.exe 0x004ffccc. Preserve the target's unusual edge
    // behavior: zero experience returns one, a threshold miss returns the
    // preceding zero-based index, and exhausting all 122 entries returns one.
    L2_GUARD_FUNCTION();

    int thresholdIndex = 0;
    int level = 1;
    if (m_exp != 0) {
        const std::int64_t* threshold = &g_ExpTable[0];
        do {
            level = thresholdIndex;
            if (m_exp < *threshold)
                break;
            ++threshold;
            ++thresholdIndex;
            level = 1;
        } while (threshold < &g_ExpTable[122]);
    }
    return level;
}

void CPet::LoadItems(int ownerId) {
    // Current Cached.exe 0x00501a70. The current procedure projects 21
    // columns: the 19-column modern pet-item donor shape followed by the
    // current hLocked/use-count pair. Inventory-slot and intensive-item
    // state are constructor constants on this path, not SQL results.
    L2_GUARD_FUNCTION();

    m_itemsLoaded = true;

    std::uint32_t itemId = 0;
    std::uint32_t itemType = 0;
    std::int64_t amount = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    ItemOptionKey optionKey;
    int appearanceType = 0;
    short attackType = ATTRT_NONE;
    short attackValue = 0;
    std::array<short, ATTRT_MAX> defendValues{};
    short hLocked = 0;
    short useCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&itemType);
    connection.Bind(&amount);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    connection.Bind(&bless);
    connection.Bind(&ident);
    connection.Bind(&wished);
    connection.Bind(&optionKey.variationOption1);
    connection.Bind(&optionKey.variationOption2);
    connection.Bind(&appearanceType);
    connection.Bind(&attackType);
    connection.Bind(&attackValue);
    for (short& defendValue : defendValues) {
        connection.Bind(&defendValue);
    }
    connection.Bind(&hLocked);
    connection.Bind(&useCount);

    CPetWarehouse* const warehouse = new CPetWarehouse;
    m_warehouse.SetObject(warehouse);
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);
    warehouse->SetOwnerId(static_cast<std::uint32_t>(ownerId));
    warehouse->Init(WareHouseType_Pet, false);

    constexpr const wchar_t* statement = L"{Call lin_LoadPetItems (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        if (amount < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]item count[%d] of pet is invalid. "
            //     "pet[%d] item[%d]",
            //     "Pet.cpp", 0x5b, static_cast<int>(amount), ownerId, static_cast<int>(itemId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet items] Skipping item %u (type %u) for inventory owner %d: database quantity %lld is negative.",
            //     itemId, itemType, ownerId, static_cast<long long>(amount))
            L2_LOG_ERROR(Log,
                "[pet items] Skipping item %u (type %u) for inventory owner %d: database quantity %lld is negative.",
                itemId, itemType, ownerId, static_cast<long long>(amount));
            continue;
        }

        CItem* item = nullptr;
        try {
            const BaseAttribute attribute(attackType, attackValue, defendValues.data());
            item = new CItem(
                itemId, static_cast<std::uint32_t>(ownerId), itemType, amount, enchant, eroded, bless, ident, wished,
                WareHouseType_Pet, optionKey, 0, attribute, -1, appearanceType, hLocked, useCount
            );
        } catch (const std::bad_alloc&) {
            item = nullptr;
        }

        if (!item) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] new item failed -_- pet[%d] item[%d]", "Pet.cpp", 0x69,
            //     static_cast<int>(m_id), static_cast<int>(itemId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet items] Cannot allocate item %u (type %u) for pet %u. Item skipped.",
            //     itemId, itemType, m_id)
            L2_LOG_ERROR(Log,
                "[pet items] Cannot allocate item %u (type %u) for pet %u; item skipped.",
                itemId, itemType, m_id);
            continue;
        }

        item->LoadEnsoulInfo();
        CPetWarehouseSP currentWarehouse = GetWarehouse();
        if (!currentWarehouse) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] Petwarehouse is invalid [%d] item[%d]", "Pet.cpp", 0x77,
            //     static_cast<int>(m_id), static_cast<int>(itemId)
            // )
            // Log.Add(LOG_ERROR,
            //     "[pet items] Cannot add item %u to pet %u: pet warehouse is unavailable. Item not added.",
            //     itemId, m_id)
            L2_LOG_ERROR(Log,
                "[pet items] Cannot add item %u to pet %u: pet warehouse is unavailable; item not added.",
                itemId, m_id);
        } else {
            currentWarehouse->PushItem(item);
        }
        L2_RELEASE(item, ObjectRefType::CreateDelete);
    }
}

bool CPet::Save() {
    // Current Cached.exe 0x00500440.
    L2_GUARD_FUNCTION();

    const double hp = static_cast<double>(m_hp);
    const double mp = static_cast<double>(m_mp);
    const long long experience = static_cast<long long>(m_exp);
    const long long spellPoint = static_cast<long long>(m_spellPoint);
    const long long downExp = static_cast<long long>(m_downExp);
    const unsigned int wasDead = m_wasDead != 0 ? 1U : 0U;
    const std::u16string nickname(FixedUtf16View(m_nickName));

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SavePet (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    connection.BindParameter(&experience);
    connection.BindParameter(&hp);
    connection.BindParameter(&mp);
    connection.BindParameter(&spellPoint);
    connection.BindParameter(&m_meal);
    connection.BindParameter(nickname.c_str());
    connection.BindParameter(&m_slot1);
    connection.BindParameter(&m_slot2);
    connection.BindParameter(&m_slot3);
    connection.BindParameter(&downExp);
    connection.BindParameter(&wasDead);
    connection.BindParameter(&m_deathCount);
    if (connection.ExecutePrepared(statement))
        return true;

    // Log.Add(LOG_ERROR, "[%s][%d] RequestSavePet failed", "Pet.cpp", 0xa9)
    // Log.Add(LOG_ERROR,
    //     "[pets] Cannot save pet %u (owner %u) in database %d.",
    //     m_id, m_ownerId, runtime::process::CurrentDatabaseId())
    L2_LOG_ERROR(Log,
        "[pets] Cannot save pet %u (owner %u) in database %d.",
        m_id, m_ownerId, runtime::process::CurrentDatabaseId());
    return false;
}

bool CPet::SetName(const wchar_t* name, int& result) {
    // Current Cached.exe 0x00500878.
    L2_GUARD_FUNCTION();
    if (!name)
        return false;

    result = -1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_CheckPetName (?)}";
    connection.Prepare(statement);
    connection.BindParameter(name);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result != 1) {
        return false;
    }

    m_nickName.fill(u'\0');
    const std::u16string targetName = WideToUtf16(name);
    if (targetName.size() >= m_nickName.size()) {
        // The target's wcscpy_s clears destination[0] and calls the
        // process invalid-parameter owner, then continues to Save if that
        // owner returns.
        runtime::process::HandleInvalidParameter(L"wcscpy_s");
    } else {
        std::copy(targetName.begin(), targetName.end(), m_nickName.begin());
    }
    Save();
    return true;
}

CPetSP CPetDB::GetPet(int petId, int worldId) {
    // Current Cached.exe 0x00500a10.
    L2_GUARD_FUNCTION();

    const CPetKey key(petId, worldId);
    CPetSP pet = m_pets.FindObject(key);
    if (pet)
        return pet;

    int npcClassId = 0;
    long long experience = 0;
    double hp = 0.0;
    double mp = 0.0;
    long long spellPoint = 0;
    int meal = 0;
    std::array<char16_t, 24> nickname{};
    std::int64_t nicknameIndicator = -1;
    int slot1 = 0;
    int slot2 = 0;
    int slot3 = 0;
    long long downExp = 0;
    int wasDead = 0;
    int deathCount = 0;

    // The map key receives the explicit world argument. The shipping DBConn
    // reads request TLS +0x10, so keep that distinction.
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&npcClassId);
    connection.Bind(&experience);
    connection.Bind(&hp);
    connection.Bind(&mp);
    connection.Bind(&spellPoint);
    connection.Bind(&meal);
    connection.BindNullable(nickname.data(), 0x30, &nicknameIndicator);
    connection.Bind(&slot1);
    connection.Bind(&slot2);
    connection.Bind(&slot3);
    connection.Bind(&downExp);
    connection.Bind(&wasDead);
    connection.Bind(&deathCount);

    constexpr const wchar_t* statement = L"{Call lin_LoadPet (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&petId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "[%s][%d] Load Pet(%d) from db failed", "Pet.cpp", 0xf0, petId)
        // Log.Add(LOG_ERROR,
        //     "[pets] No database row loaded for pet %d in database %d (cache world %d). No pet returned.",
        //     petId, runtime::process::CurrentDatabaseId(), worldId)
        L2_LOG_ERROR(Log,
            "[pets] No database row loaded for pet %d in database %d (cache world %d); no pet returned.",
            petId, runtime::process::CurrentDatabaseId(), worldId);
        runtime::process::ReportNamedCallStack(L"GetPet", 2, false);
        return {};
    }

    CPet* const loaded = new CPet;
    loaded->m_ownerId = 0;
    loaded->m_id = static_cast<std::uint32_t>(petId);
    loaded->m_npcClassId = static_cast<std::uint32_t>(npcClassId);
    loaded->m_hp = runtime::Win64CvttDoubleToInt32(hp);
    loaded->m_mp = runtime::Win64CvttDoubleToInt32(mp);
    loaded->m_exp = experience;
    loaded->m_spellPoint = spellPoint;
    loaded->m_meal = meal;
    loaded->m_slot1 = slot1;
    loaded->m_slot2 = slot2;
    loaded->m_slot3 = slot3;
    loaded->m_downExp = downExp;
    loaded->m_wasDead = wasDead != 0 ? 1U : 0U;
    loaded->m_deathCount = deathCount;

    const std::u16string targetName = std::u16string(nickname.data());
    const std::size_t copiedNameUnits = std::min(targetName.size(), loaded->m_nickName.size() - 1);
    std::copy_n(targetName.data(), copiedNameUnits, loaded->m_nickName.data());

    CPetWarehouse* const warehouse = new CPetWarehouse;
    warehouse->Init(WareHouseType_Pet, false);
    loaded->m_warehouse.SetObject(warehouse);
    L2_RELEASE(warehouse, ObjectRefType::CreateDelete);

    m_pets.Insert(key, loaded);
    L2_RELEASE(loaded, ObjectRefType::Anonymous);
    return CPetSP(loaded, ObjectRefType::FunctionChain);
}

CPetSP CPetDB::ReloadPet(int petId, int worldId) {
    // Current Cached.exe 0x00500f24. Unlike GetPet, this always refreshes the
    // persistent fields first and only then decides whether to create a new
    // cached object or overwrite the existing world-qualified entry.
    L2_GUARD_FUNCTION();

    int npcClassId = 0;
    long long experience = 0;
    double hp = 0.0;
    double mp = 0.0;
    long long spellPoint = 0;
    int meal = 0;
    std::array<char16_t, 24> nickname{};
    std::int64_t nicknameIndicator = -1;
    int slot1 = 0;
    int slot2 = 0;
    int slot3 = 0;
    long long downExp = 0;
    int wasDead = 0;
    int deathCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&npcClassId);
    connection.Bind(&experience);
    connection.Bind(&hp);
    connection.Bind(&mp);
    connection.Bind(&spellPoint);
    connection.Bind(&meal);
    connection.BindNullable(nickname.data(), 0x30, &nicknameIndicator);
    connection.Bind(&slot1);
    connection.Bind(&slot2);
    connection.Bind(&slot3);
    connection.Bind(&downExp);
    connection.Bind(&wasDead);
    connection.Bind(&deathCount);

    constexpr const wchar_t* statement = L"{Call lin_LoadPet (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&petId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "[%s][%d] Load Pet(%d) from db failed", "Pet.cpp", 0x139, petId)
        // Log.Add(LOG_ERROR,
        //     "[pets] Cannot reload pet %d from database %d (cache world %d): no row loaded. Cached pet not updated.",
        //     petId, runtime::process::CurrentDatabaseId(), worldId)
        L2_LOG_ERROR(Log,
            "[pets] Cannot reload pet %d from database %d (cache world %d): no row loaded; cached pet not updated.",
            petId, runtime::process::CurrentDatabaseId(), worldId);
        runtime::process::ReportNamedCallStack(L"GetPet", 2, false);
        return {};
    }

    const CPetKey key(petId, worldId);
    CPetSP pet = m_pets.FindObject(key);
    if (!pet) {
        CPet* const loaded = new CPet;
        loaded->m_ownerId = 0;
        loaded->m_id = static_cast<std::uint32_t>(petId);
        loaded->m_npcClassId = static_cast<std::uint32_t>(npcClassId);
        loaded->m_hp = runtime::Win64CvttDoubleToInt32(hp);
        loaded->m_mp = runtime::Win64CvttDoubleToInt32(mp);
        loaded->m_exp = experience;
        loaded->m_spellPoint = spellPoint;
        loaded->m_meal = meal;
        loaded->m_slot1 = slot1;
        loaded->m_slot2 = slot2;
        loaded->m_slot3 = slot3;
        loaded->m_downExp = downExp;
        loaded->m_wasDead = wasDead != 0 ? 1U : 0U;
        loaded->m_deathCount = deathCount;

        const std::u16string targetName = std::u16string(nickname.data());
        const std::size_t copiedNameUnits = std::min(targetName.size(), loaded->m_nickName.size() - 1);
        std::copy_n(targetName.data(), copiedNameUnits, loaded->m_nickName.data());

        CPetWarehouse* const warehouse = new CPetWarehouse;
        warehouse->Init(WareHouseType_Pet, false);
        loaded->m_warehouse.SetObject(warehouse);
        L2_RELEASE(warehouse, ObjectRefType::CreateDelete);

        m_pets.Insert(key, loaded);
        L2_RELEASE(loaded, ObjectRefType::Anonymous);
        return CPetSP(loaded, ObjectRefType::FunctionChain);
    }

    pet->m_ownerId = 0;
    pet->m_id = static_cast<std::uint32_t>(petId);
    pet->m_npcClassId = static_cast<std::uint32_t>(npcClassId);
    pet->m_hp = runtime::Win64CvttDoubleToInt32(hp);
    pet->m_mp = runtime::Win64CvttDoubleToInt32(mp);
    pet->m_exp = experience;
    pet->m_spellPoint = spellPoint;
    pet->m_meal = meal;
    pet->m_slot1 = slot1;
    pet->m_slot2 = slot2;
    pet->m_slot3 = slot3;
    pet->m_downExp = downExp;
    pet->m_wasDead = wasDead != 0 ? 1U : 0U;
    pet->m_deathCount = deathCount;
    pet->m_nickName.fill(u'\0');
    const std::u16string targetName = std::u16string(nickname.data());
    const std::size_t copiedNameUnits = std::min(targetName.size(), pet->m_nickName.size() - 1);
    std::copy_n(targetName.data(), copiedNameUnits, pet->m_nickName.data());
    return pet;
}

bool CPetDB::DeletePet(int petId, bool deleteItems [[maybe_unused]], int worldId) {
    // Current Cached.exe 0x00501778. The named bool parameter is present in
    // the source signature but is not read anywhere in this revision.
    L2_GUARD_FUNCTION();

    CPetSP pet = GetPet(petId, worldId);
    if (!pet)
        return false;
    CPetWarehouseSP warehouse = pet->GetWarehouse();
    if (!warehouse)
        return false;

    const int ownerId = static_cast<int>(pet->m_ownerId);
    const CPetKey key(petId, worldId);
    m_pets.DeleteObject(key);

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePet (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&petId);
    connection.BindParameter(&ownerId);
    if (connection.ExecutePrepared(statement))
        return true;

    // Log.Add(
    //     LOG_ERROR,
    //     "[%s][%d] RequestDeletePet Failed (delete pet failed) "
    //     "PetCollarDBID(%d)",
    //     "Pet.cpp", 0x196, petId
    // )
    // Log.Add(LOG_ERROR,
    //     "[pets] Cannot delete pet %d (owner %d) from database %d. Its cache entry in world %d was already removed.",
    //     petId, ownerId, runtime::process::CurrentDatabaseId(), worldId)
    L2_LOG_ERROR(Log,
        "[pets] Cannot delete pet %d (owner %d) from database %d; its cache entry in world %d was already removed.",
        petId, ownerId, runtime::process::CurrentDatabaseId(), worldId);
    return false;
}
