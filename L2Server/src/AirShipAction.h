#pragma once

#include "AirShip.h"

#include "Shared/src/IOC.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

enum AltitudeActionType : std::int32_t {
    AAT_NONE = 0,
    AAT_ASCENT = 1,
    AAT_DESCENT = -1,
};

enum SHIP_ACTION_RESULT : std::int32_t {
    SAR_STOP = 0,
    SAR_GO = 1,
};

enum ShipActionType : std::int32_t {
    ACT_SHIP_NOTHING = 0,
    ACT_SHIP_MOVE = 1,
};

enum ActionOrder : std::int32_t {
    AO_CURRENT_ACTION = 0,
    AO_PENDING_ACTION = 1,
    AO_MAX = 2,
};

class AltitudeHelper {
    enum {
        ALTITUDE_DELTA = 500,
        L1_ALTITUDE = 2000,
        L1_SPEED_BONUS = 10,
    };

public:
    static int AdjustAltitude(int orgAltitude);
    static int ChangeAltitude(int orgAltitude, AltitudeActionType action);
    static bool CanChangeAltitude(int orgAltitude, AltitudeActionType action);
    static int GetSpeedByAltitude(double orgSpeed, int altitude);
};

class CShipAction {
public:
    CShipAction() = default;
    virtual ~CShipAction() = default;

    virtual SHIP_ACTION_RESULT OnStart() = 0;
    virtual SHIP_ACTION_RESULT OnTick() = 0;
    virtual SHIP_ACTION_RESULT OnStopMove(bool force) = 0;
    virtual void OnEnd(bool force) = 0;

    void AddTimer(int delayMilliseconds);

    CSmartID m_spShip;
};

class CShipNothingAction final : public CShipAction {
public:
    SHIP_ACTION_RESULT OnStart() override;
    SHIP_ACTION_RESULT OnTick() override;
    SHIP_ACTION_RESULT OnStopMove(bool force) override;
    void OnEnd(bool force) override;
};

class CShipMoveAction final : public CShipAction {
public:
    struct NaviInfo {
        FVector startNaviPoint;
        FVector endNaviPoint;
        FVector midNaviPoint;
        double distPassed;
        std::int32_t nextTick;
        bool blocked;
        bool bArrived;
        std::byte padding_56[2];
        double radius;
        double height;
    };

    CShipMoveAction();
    CShipMoveAction& operator=(const CShipMoveAction& other);

    SHIP_ACTION_RESULT OnStart() override;
    SHIP_ACTION_RESULT OnTick() override;
    SHIP_ACTION_RESULT OnStopMove(bool force) override;
    void OnEnd(bool force) override;

    void CalcMidDest(NaviInfo& navigation);
    SHIP_ACTION_RESULT SetMoving();

    FVector m_DestPos;
    FVector m_vMidDest;
    std::int32_t m_nTick;
    bool m_bArrived;
    bool m_bResetDest;
    bool m_bMoving;
    bool m_bIsBroadCast;
    std::uint32_t m_StartTime;
    std::int32_t m_MoveTickCount;
    AltitudeActionType m_AltitudeAction;
};

struct CShipActionData {
    CShipActionData();

    CShipNothingAction m_Nothing;
    CShipMoveAction m_Move;
    ShipActionType m_ActionType;
    std::byte padding_6c[4];
};

class CShipController final : public CIOObject {
public:
    L2_ASYNC_DECLARE_FINAL_OUT_OF_LINE_FIELDS_2(__async_arg_Move, FVector, destination, bool, broadcast);

    L2_ASYNC_DECLARE_DEFAULTED_EMPTY_2_SLOT(__async_arg_Ascent);

    L2_ASYNC_DECLARE_DEFAULTED_EMPTY_2_SLOT(__async_arg_Descent);

    L2_ASYNC_DECLARE_DEFAULTED_EMPTY_2_SLOT(__async_arg_DoNothing);

    L2_ASYNC_DECLARE_FINAL_OUT_OF_LINE_FIELDS_1(__async_arg_AsyncTimerExpired, std::int32_t, int, actionNumber);

    L2_DECLARE_OBJECT_REGISTRATION(CShipController)

    explicit CShipController(CAirShip* airShip);
    ~CShipController() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Move(FVector destination, bool broadcast);
    void Ascent();
    void Descent();
    void DoNothing();
    void AddTimerEx(std::int64_t delayMilliseconds);
    CShipAction* GetCurrentAction();
    bool IsChangingAltitude();
    void SetAsMove(ActionOrder order, FVector destination, bool broadcast);
    void DoPendingAction(int depth);
    void DoFirstAction();
    void ActionBroadcastStopMove(bool currentPosition);
    void ActionBroadcastMove(FVector source, FVector destination);

private:
    static void RunMove(CShipController* owner, CExecutionArgumentPad* argument);
    static void RunAscent(CShipController* owner, CExecutionArgumentPad* argument);
    static void RunDescent(CShipController* owner, CExecutionArgumentPad* argument);
    static void RunDoNothing(CShipController* owner, CExecutionArgumentPad* argument);
    static void RunAsyncTimerExpired(CShipController* owner, CExecutionArgumentPad* argument);

    void __async_Move(CExecutionArgumentPad* argument);
    void __async_Ascent(CExecutionArgumentPad* argument);
    void __async_Descent(CExecutionArgumentPad* argument);
    void __async_DoNothing(CExecutionArgumentPad* argument);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);

    CExecutionSerializer<CShipController, 64> Serializer;
    CShipActionData m_Action[2];
    bool m_ForFreeAirShip;
    [[maybe_unused]] std::byte padding_1739[3];
    std::int32_t m_nActionNumber;
    CSmartID m_spMyShip;
};
