#pragma once

#include "Shared/src/TemplateHelper.h"

#include <cstddef>

class NpcSocket;

using NpcSocketExCallback = bool (*)(NpcSocket*, const unsigned char*);
using NpcSocketExCallbackMap = FixedSizeCallbackMap<58, NpcSocketExCallback>;

extern NpcSocketExCallbackMap g_NPCSocketEx;

bool NpcExSocket(NpcSocket* socket, const unsigned char* packet);

namespace NpcSocketEx {

bool DummyNpcSocketEx(NpcSocket* socket, const unsigned char* packet);
bool CheckWeightToChangeSubclass(NpcSocket* socket, const unsigned char* packet);
bool ShowItemAttributionChange(NpcSocket* socket, const unsigned char* packet);
bool SetUserLevel(NpcSocket* socket, const unsigned char* packet);
bool GiveQuest(NpcSocket* socket, const unsigned char* packet);
bool CompleteQuest(NpcSocket* socket, const unsigned char* packet);
bool StartJournal(NpcSocket* socket, const unsigned char* packet);
bool CompleteJournal(NpcSocket* socket, const unsigned char* packet);
bool SetShillienMoon(NpcSocket* socket, const unsigned char* packet);
bool NpcResetCastle(NpcSocket* socket, const unsigned char* packet);
bool NpcSetCastleType(NpcSocket* socket, const unsigned char* packet);
bool RegisterPledgeGameMaker(NpcSocket* socket, const unsigned char* packet);
bool NpcPledgeGameFunc(NpcSocket* socket, const unsigned char* packet);
bool GiveItemViaPledgeGame(NpcSocket* socket, const unsigned char* packet);
bool SetGotItemPledgeGame(NpcSocket* socket, const unsigned char* packet);
bool AddLogFromNPCEx(NpcSocket* socket, const unsigned char* packet);
bool NpcEventCampaignEx(NpcSocket* socket, const unsigned char* packet);
bool SetCompeteAgitOwner(NpcSocket* socket, const unsigned char* packet);
bool StartDynamicQuest(NpcSocket* socket, const unsigned char* packet);
bool ShowBeautyShop(NpcSocket* socket, const unsigned char* packet);
bool MoveToServer(NpcSocket* socket, const unsigned char* packet);
bool ReturnToOrigin(NpcSocket* socket, const unsigned char* packet);
bool ShowCardCollectSystem(NpcSocket* socket, const unsigned char* packet);

bool IncreaseAPByNpc(NpcSocket* socket, const unsigned char* packet);
bool ShowTutorialPageID(NpcSocket* socket, const unsigned char* packet);
bool SetMasterFriendNPC(NpcSocket* socket, const unsigned char* packet);
bool ShowAPWnd(NpcSocket* socket, const unsigned char* packet);
bool NpcServerLoadComplete(NpcSocket* socket, const unsigned char* packet);
bool NpcUpdateEventScoreEx(NpcSocket* socket, const unsigned char* packet);
bool ShowLuckyGame(NpcSocket* socket, const unsigned char* packet);
bool TrainingState(NpcSocket* socket, const unsigned char* packet);
bool NpcGiveItemOnAccount(NpcSocket* socket, const unsigned char* packet);
bool NpcGiveItemOnAccountForEvent(NpcSocket* socket, const unsigned char* packet);
bool NpcCreateDualJob(NpcSocket* socket, const unsigned char* packet);
bool ShowKamarokaRankingList(NpcSocket* socket, const unsigned char* packet);
bool ShowKamarokaUserRankInfo(NpcSocket* socket, const unsigned char* packet);
bool AddKamarokaNewRecord(NpcSocket* socket, const unsigned char* packet);
bool GetGreatEventContributor(NpcSocket* socket, const unsigned char* packet);
bool GeneralObjectPacket(NpcSocket* socket, const unsigned char* packet);
bool SetDroidNPC(NpcSocket* socket, const unsigned char* packet);
bool ShowEnsoulWindow(NpcSocket* socket, const unsigned char* packet);
bool BossRegenTime(NpcSocket* socket, const unsigned char* packet);
bool ShowEnsoulExtractionWindow(NpcSocket* socket, const unsigned char* packet);
bool SyncServerConfig(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_BROADCAST_SCREEN_MESSAGE_FSTRING_FIELD_CYCLE(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_BROADCAST_FIELD_EVENT(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_CAST_BUFF_TO_RANDOM_USER(NpcSocket* socket, const unsigned char* packet);
bool ShowUpgradeSystem(NpcSocket* socket, const unsigned char* packet);
bool ResetInzoneBinding(NpcSocket* socket, const unsigned char* packet);
bool ShowCardUpdownGame(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_PLEDGE_RAID_POINT_SET(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_PLEDGE_RAID_SHOW_INFO(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_PLEDGE_MISSION_AI_CONTROL(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_WORLD_RAID_START(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_WORLD_RAID_STOP(NpcSocket* socket, const unsigned char* packet);
bool Handle_NC_EX_WORLD_RAID_BROADCAST_EVENT(NpcSocket* socket, const unsigned char* packet);
bool ShowCreatePledge(NpcSocket* socket, const unsigned char* packet);
bool SendAgitWarPacket(NpcSocket* socket, const unsigned char* packet);

} // namespace NpcSocketEx

static_assert(NpcSocketExCallbackMap::SIZE == 58);
