#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class CItem;
class CDBSocket;
class CCombinationItem;
class CCreature;
class CNPC;
class CObject;
class CSetItem;
class CNewSetItem;
class CShapeShiftedSetItem;
class CHenna;
class CSkillInfo;

// Exact value copied into CObjectDB's current soul-shot-effect map. The map
// key is CSoulShoteffect::nID, so it is not repeated here.
struct SoulShotEffectStruct final {
    std::int32_t EffectType;
    std::int32_t EffectLevel;
    std::int32_t ItemID;
    std::int32_t SkillID;
    std::int32_t SkillLevel;
};

// Exact current value stored by CObjectDB for one two-slot combination.
// Natural 64-bit alignment supplies the three four-byte padding gaps proved
// by the current copy/registration bodies.
struct CombinationItemStruct final {
    std::int32_t SlotOne;
    std::int64_t SlotOneAmount;
    std::int32_t SlotTwo;
    std::int64_t SlotTwoAmount;
    std::int32_t Prob;
    std::int32_t SuccessResultItem;
    std::int64_t SuccessResultItemAmount;
    std::int32_t FailResultItem;
    std::int64_t FailResultItemAmount;
};

using UpgradeMaterial = std::CDualKey<std::int32_t, std::int64_t>;
using UpgradeMaterialVector = std::vector<UpgradeMaterial>;

// Standalone current upgrade recipe object. It has no CObject/MemoryObject
// base. Its only vtable slot is the deleting destructor.
class CUpgradeSystem {
public:
    CUpgradeSystem();
    virtual ~CUpgradeSystem();

    void SetID(std::int32_t id);
    void SetUpgradeItem(std::int32_t itemId, std::int32_t enchant);
    void SetMaterialItem(std::int32_t itemId, std::int64_t amount);
    void SetCommission(std::int64_t commission);
    void SetResultItem(std::int32_t itemId, std::int32_t enchant);

    friend class CObjectDB;

    std::int32_t m_nID;
    std::int32_t m_nUpgradeID;
    std::int64_t m_nUpgradeEnchant;
    UpgradeMaterialVector m_vMaterial;
    std::int64_t m_nCommission;
    std::int32_t m_nResultID;
    std::int64_t m_nResultEnchant;
};

// Standalone parser record used to create SoulShotEffectStruct values.
// The unusual historic spelling is the spelling present in current RTTI.
class CSoulShoteffect {
public:
    CSoulShoteffect();
    virtual ~CSoulShoteffect();

    void Set(std::int32_t id);
    void SetType(std::int32_t effectType);
    void SetLevel(std::int32_t effectLevel);
    void SetItemID(std::int32_t itemId);
    void SetSkillID(std::int32_t skillId, std::int32_t skillLevel);

private:
    friend class CObjectDB;

    std::int32_t nID;
    std::int32_t nEffectType;
    std::int32_t nEffectLevel;
    std::int32_t nItemID;
    std::int32_t nSkillID;
    std::int32_t nSkillLevel;
};

using ObjectDbNameMap = std::map<std::u16string, std::int32_t, runtime::CStringLess<std::u16string>>;

template <typename TObject>
using ObjectDbNamedPointerMap = std::map<std::u16string, TObject*, runtime::CStringLess<std::u16string>>;

// Current L2Server 0x008d9afc/0x008d9460 proves the 0x340-byte physical
// owner and every 0x20-byte tree carrier. TotalChecker contributes only the
// original member spellings/types/access structure. Heavy data parsers remain
// a later ObjectDB-owned layer. Lifecycle and class lookup are complete here.
class CObjectDB : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CObjectDB)

    CObjectDB();
    ~CObjectDB() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    CObject* GetObjectW(std::int32_t classId, bool silent) const;
    CItem* CreateItem(std::int32_t classId) const;
    CNPC* CreateNPC(std::int32_t classId, bool copy) const;
    CObject* GetObjectByName(const char16_t* name) const;
    const wchar_t* GetAI(std::int32_t classId) const;
    const char16_t* GetName(std::int32_t classId) const;
    const wchar_t* GetSysString(std::uint8_t countryCode, std::int32_t stringNumber) const;
    void SetSysString(std::uint8_t countryCode, std::int32_t stringNumber, const char16_t* value);
    int GetClassIdFromName(const char16_t* name, bool silent) const;
    bool RegisterName(std::int32_t classId, wchar_t* name);
    bool RegisterName(CObject* object, wchar_t* name);
    void RegisterAI(CObject* object, wchar_t* name);
    void RegisterObject(CObject* object);
    void PostLoad();

    bool LoadItem(bool reload);
    bool LoadItemData(char16_t* data, int dataLength, bool reload);

    bool LoadNPC(bool checkAI, bool reload);
    bool LoadNPCData(char16_t* data, int dataLength, bool checkAI, bool reload);

    bool LoadCombinationItem(bool onlyPrecompiledHeader);
    bool LoadCombinationItemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);
    CombinationItemStruct* FindCombinationItemStructBySlotOneItem(std::int32_t slotOneItem);
    CombinationItemStruct* FindCombinationItemStructByMapKey(std::int32_t slotOneItem, std::int32_t slotTwoItem);
    bool IsExistCombination(std::int32_t slotOneItem, std::int32_t slotTwoItem);
    void RegisterCombinationItemStruct(CombinationItemStruct combinationItem);
    void SetCombinationItemStruct(CCombinationItem* combinationItem);

    bool LoadSoulShotEffect(bool onlyPrecompiledHeader);
    bool LoadSoulShotEffectData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);
    void SetRegisterSoulShotEffectObject(CSoulShoteffect* soulShotEffect);
    const CSkillInfo* GetSoulShotSkill(int action, CCreature* creature);
    int GetSoulShotClassID(CCreature* creature) const;

    bool LoadUseCountItem(bool onlyPrecompiledHeader);
    bool LoadUseCountItemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);

    bool LoadSetItem(bool onlyPrecompiledHeader);
    bool LoadSetItemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);
    void SetRegisterObject(CSetItem* setItem);
    CSetItem* GetSetItemBySetItemId(std::int32_t setItemId) const;
    int IsItSet(CCreature* creature, int* slotIndices, bool& additional2) const;
    CNewSetItem* GetNewSetItemBySetItemId(std::int32_t setItemId) const;
    void SetRegisterSetObject(CNewSetItem* setItem);

    bool LoadShapeShiftedSetItem(bool onlyPrecompiledHeader);
    bool LoadShapeShiftedSetItemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);
    CShapeShiftedSetItem* GetShapeShiftedSetItemBySetItemId(std::int32_t setItemId) const;
    void SetRegisterSetObject(CShapeShiftedSetItem* setItem);

    bool LoadUpgradeSystem(bool onlyPrecompiledHeader);
    bool LoadUpgradeSystemData(char16_t* data, int dataLength, bool onlyPrecompiledHeader);
    CUpgradeSystem* CheckUpgradeSystem(std::int32_t id);

    CItem* m_pLastParsedItem;
    CNPC* m_pLastParsedNpc;
    std::map<std::int32_t, CUpgradeSystem*> m_mapUpgradeSystem;
    std::map<std::CDualKey<std::int32_t, std::int32_t>, CombinationItemStruct> m_mapCombinationItemStruct;
    std::map<std::int32_t, SoulShotEffectStruct> m_mapSoulShotEffect;

private:
    friend bool CacheVersionPacket(CDBSocket*, const std::uint8_t*);
    static const wchar_t* m_pClassName_CObjectDB;

    std::map<std::int32_t, std::u16string> m_ClassIdToAI;
    std::map<std::int32_t, std::u16string> m_ClassIdToName;
    ObjectDbNameMap m_NameToClassId;
    ObjectDbNamedPointerMap<CObject> m_NameToObject;
    std::map<std::int32_t, CObject*> m_ClassIdToObject;
    std::map<std::int32_t, std::u16string> m_SetItemIdToName;
    ObjectDbNameMap m_SetNameToSetItemId;
    ObjectDbNamedPointerMap<CSetItem> m_SetNameToObject;
    std::map<std::int32_t, CSetItem*> m_SetItemIdToObject;
    std::map<std::int32_t, CSetItem*> m_SetKeyIdToObject;
    std::map<std::int32_t, std::u16string> m_NewSetItemIdToName;
    ObjectDbNameMap m_NewSetNameToSetItemId;
    ObjectDbNamedPointerMap<CNewSetItem> m_NewSetNameToObject;
    std::map<std::int32_t, CNewSetItem*> m_NewSetItemIdToObject;
    std::map<std::int32_t, CShapeShiftedSetItem*> m_ShapeShiftedSetItemIdToObject;
    std::map<std::int32_t, std::u16string> m_HennaClassIdToName;
    ObjectDbNameMap m_HennaNameToClassId;
    ObjectDbNamedPointerMap<CHenna> m_HennaNameToObject;
    std::map<std::int32_t, CHenna*> m_HennaClassIdToObject;
    std::map<std::CDualKey<std::uint8_t, std::int32_t>, std::wstring> m_SysString;
    std::int32_t m_nRegisteredItemCount;
    std::int32_t m_nRegisteredNPCCount;

public:
    bool m_bOnlyItemPrecompiledHeader;
    std::FILE* m_item_pch_file;
    std::FILE* m_item_pch_file2;
    bool m_bOnlyNpcPrecompiledHeader;
    std::FILE* m_npc_pch_file;
    std::int32_t m_ClassIndex;
};

extern CObjectDB g_ObjectDB;
extern std::array<char, 40000> g_bRegisteredNpcID;
