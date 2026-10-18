#pragma once

// The current executable stores and passes the warehouse category as a
// 32-bit value. Names come from the matching C1 owner. Values 0..6 and the
// four seized categories are present in current control flow.
enum WareHouseType : int {
    WareHouseType_Uninitialized = -1,
    WareHouseType_Inventory = 0,
    WareHouseType_CharWarehouse = 1,
    WareHouseType_Pledge = 2,
    WareHouseType_Castle = 3,
    WareHouseType_Manor = 4,
    WareHouseType_Pet = 5,
    WareHouseType_Mail = 6,
    WareHouseType_Seized_CharWarehouse = 1001,
    WareHouseType_Seized_Pledge = 1002,
    WareHouseType_Seized_Castle = 1003,
    WareHouseType_Seized_Pet = 1005,
};
