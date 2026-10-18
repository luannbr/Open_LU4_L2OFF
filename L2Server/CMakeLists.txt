find_package(LibXml2 REQUIRED)
find_package(Threads REQUIRED)

set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shared")
if(APPLE)
    # The system ar drops the tail of this large positional object list while
    # still returning success.  Apple's static-library driver keeps the
    # complete archive, including owners rebuilt after Raid.cpp.
    find_program(L2SERVER_APPLE_LIBTOOL libtool REQUIRED)
    set(CMAKE_CXX_ARCHIVE_CREATE
        "\"${L2SERVER_APPLE_LIBTOOL}\" -static <LINK_FLAGS> -o <TARGET> <OBJECTS>")
endif()

include(L2Grammar)

# Bison and re2c generate the game data parsers and scanners at build time.
l2_grammar_sources(
    L2SERVER_GRAMMAR_SOURCES
    src/BuilderCmdParser.y
    src/BuilderCmdLex.re
    src/HennaParser.y
    src/HennaLex.re
    src/ItemAnnounceParser.y
    src/ItemAnnounceLexer.re
    src/ManorParser.y
    src/ManorLex.re
    src/MonRaceParser.y
    src/MonRaceLex.re
    src/ParserArmorEnchantBonus.y
    src/LexerArmorEnchantBonus.re
    src/ParserColorEx.y
    src/LexerColorEx.re
    src/ParserConvertData.y
    src/LexerConvertData.re
    src/ParserCursedWeaponData.y
    src/LexerCursedWeaponData.re
    src/ParserFaceEx.y
    src/LexerFaceEx.re
    src/ParserFieldCycle.y
    src/LexerFieldCycle.re
    src/ParserHairEx.y
    src/LexerHairEx.re
    src/ParserPathToAwakening.y
    src/LexerPathToAwakening.re
    src/ParserSoulshotEffect.y
    src/LexerSoulshotEffect.re
    src/ParserTeleportQuestPath.y
    src/LexerTeleportQuestPath.re
    src/UseCountItemParser.y
    src/UseCountItemLexer.re
    src/ArtifactParser.y
    src/ArtifactLexer.re
    src/ItemAttrChangeParser.y
    src/ItemAttrChangeLexer.re
    src/ParserBasicAction.y
    src/LexerBasicAction.re
    src/ParserCombinationItem.y
    src/LexerCombinationItem.re
    src/ParserDoor.y
    src/LexerDoor.re
    src/ParserUpgradeSystem.y
    src/LexerUpgradeSystem.re
    src/PredefinedListParser.y
    src/PredefinedListLexer.re
    src/RecipeParser.y
    src/RecipeLex.re
    src/CardCollectSystemParser.y
    src/CardCollectSystemLexer.re
    src/CardUpDownGameParser.y
    src/CardUpDownGameLexer.re
    src/EnsoulParser.y
    src/EnsoulLexer.re
    src/FishingParser.y
    src/FishingLex.re
    src/ParserAlchemy.y
    src/LexerAlchemy.re
    src/ParserExDrop.y
    src/LexerExDrop.re
    src/ParserExpData.y
    src/LexerExpData.re
    src/ParserLuckyGame.y
    src/LexerLuckyGame.re
    src/ParserSkillEnchantData.y
    src/LexerSkillEnchantData.re
    src/ParserVariation.y
    src/LexerVariation.re
    src/PledgeMissionParser.y
    src/PledgeMissionLexer.re
    src/AreaDataParser.y
    src/AreaDataLex.re
    src/CastleParser.y
    src/CastleLex.re
    src/DecoDataParser.y
    src/DecoDataLexer.re
    src/DecoNPCDataParser.y
    src/DecoNPCDataLexer.re
    src/MultiSellParser.y
    src/MultiSellLex.re
    src/ParserAirShip.y
    src/LexerAirShip.re
    src/ParserCubic.y
    src/LexerCubic.re
    src/ParserMonsterBook.y
    src/LexerMonsterBook.re
    src/ParserProduct.y
    src/LexerProduct.re
    src/ParserQuestSystem.y
    src/LexerQuestSystem.re
    src/ParserRestrictArea.y
    src/LexerRestrictArea.re
    src/ParserShuttle.y
    src/LexerShuttle.re
    src/DynamicQuestParser.y
    src/DynamicQuestLexer.re
    src/InstantAgitParser.y
    src/InstantAgitLexer.re
    src/ParserInstantZone.y
    src/LexerInstantZone.re
    src/ParserPremiumService.y
    src/LexerPremiumService.re
    src/ParserSkillSynergy.y
    src/LexerSkillSynergy.re
    src/SkillAcquireYacc.y
    src/SkillAcquireLex.re
    src/ParserAuctionData.y
    src/LexerAuctionData.re
    src/ParserEnchantOption.y
    src/LexerEnchantOption.re
    src/ParserWeaponEnchantEx.y
    src/LexerWeaponEnchantEx.re
    src/ParserFlyMove.y
    src/LexerFlyMove.re
    src/VehicleDataParser.y
    src/VehicleDataLex.re
    src/QuestDataYacc.y
    src/QuestDataLex.re
    src/QuestHelperQuestParser.y
    src/QuestHelperQuestLexer.re
    src/QuestHelperUserQuestParser.y
    src/QuestHelperUserQuestLexer.re
    src/ParserOption.y
    src/LexerOption.re
    src/EventCampaignParser.y
    src/EventCampaignLexer.re
    src/ParserNPC.y
    src/LexerNPC.re
    src/ParserSkill.y
    src/LexerSkill.re
    src/ParserSetItem.y
    src/LexerSetItem.re
    src/ParserShapeShiftedSetItem.y
    src/LexerShapeShiftedSetItem.re
    src/ParserFaction.y
    src/LexerFaction.re
    src/ParserMatchingPoint.y
    src/LexerMatchingPoint.re
    src/PetDataParser.y
    src/PetDataLexer.re
    src/ParserSetting.y
    src/LexerSetting.re
    src/ParserTransform.y
    src/LexerTransform.re
    src/ParserObject.y
    src/LexerObject.re
    src/PCParamYacc.y
    src/PCParamLex.re)

add_library(l2server_core STATIC
    ../Protocol/src/L2NPCProtocol.cpp
    src/AlchemySystem.cpp
    src/Area.cpp
    src/AreaBasedRule.cpp
    src/Artifact.cpp
    src/Artifact.h
    src/ArtifactLexer.h
    src/ArtifactParser.h
    src/Agit.cpp
    src/AgitAuctionPacketHandler.cpp
    src/AgitHtml.cpp
    src/AgitDeco.cpp
    src/AgitDecoDb.cpp
    src/AirShip.cpp
    src/Agent.cpp
    src/AgentGameGuard.cpp
    src/AgentNewInspector.cpp
    src/AirShipAction.cpp
    src/AirShipPart.cpp
    src/AnnounceTimer.cpp
    src/Auction.cpp
    src/AuctionAgit.cpp
    src/AtomicJobServerSide.cpp
    src/AccountClient/AccountClientSocket.cpp
    src/AccountClient/AccountServerStub.cpp
    src/Auth.cpp
    src/AuthSocket.cpp
    src/BaseCampAgit.cpp
    src/BeautyManager.cpp
    src/BuilderCmdHandler.cpp
    src/BuilderCmdHandlerForEvent.cpp
    src/BotChecker.cpp
    src/BotReportAgent.cpp
    src/BlockUpset.cpp
    src/Boss.cpp
    src/BuySell.cpp
    src/BuySellListManager.cpp
    src/Alliance.cpp
    src/CardCollectSystem.cpp
    src/CardUpDownGame.cpp
    src/Captcha.cpp
    src/Cleft.cpp
    src/CrataeCube.cpp
    src/Party.cpp
    src/PartyMatchingWaitingRoom.cpp
    src/PartyMatchingResponse.cpp
    src/PartyMatchingRequest.cpp
    src/PartyRoom.cpp
    src/PartyRoomInfo.cpp
    src/PartyRoomListFilter.cpp
    src/PartyRoomMember.cpp
    src/PartyRoomRegistry.cpp
    src/ParseHtml.cpp
    src/Memo.cpp
    src/ClanBbs.cpp
    src/Cmpccroom.cpp
    src/BattleCamp.cpp
    src/BRDB.cpp
    src/BRProductServer.cpp
    src/BRShopServer.cpp
    src/ControlTower.cpp
    src/CouponKey.cpp
    src/BRCreatureServer.cpp
    src/BRGameEvent.cpp
    src/CreatureBase.cpp
    src/CreatureCommon.cpp
    src/CreatureBroadcast.cpp
    src/CreatureController.cpp
    src/CreatureServer.cpp
    src/CreatureTrigger.cpp
    src/CreatureNew.cpp
    src/Door.cpp
    src/EventMatch.cpp
    src/MultiPartyCommandChannel.cpp
    src/MultiPartyCommandChannelRegistry.cpp
    src/Object.cpp
    src/Chair.cpp
    src/SignBoard.cpp
    src/StaticObject.cpp
    src/CWorldObject.cpp
    src/WorldServer.cpp
    src/Config.cpp
    src/ConnectThreadManager.cpp
    src/Cubic.cpp
    src/ChannelChattingSocket.cpp
    src/ChannelChatting.cpp
    src/Collection.cpp
    src/CombinationItem.cpp
    src/CompeteAgit.cpp
    src/CommissionTrade.cpp
    src/CommCli.cpp
    src/CommSocket.cpp
    src/Community.cpp
    src/DB.cpp
    src/DailyMission.cpp
    src/DBPacketEx.cpp
    src/DBSocket.cpp
    src/DBSocketOp.cpp
    src/DeferredJob.cpp
    src/Dominion.cpp
    src/Doppelganger.cpp
    src/DynamicQuest.cpp
    src/EventCampaign.cpp
    src/DecoNpcData.cpp
    src/Default.cpp
    src/Duel.cpp
    src/DuelUserInfo.cpp
    src/Economy.cpp
    src/EnchantOption.cpp
    src/MultiSell.cpp
    src/CursedWeapon.cpp
    src/PredefinedListDB.cpp
    src/EnsoulData.cpp
    src/MatchingPointDB.cpp
    src/EventMonitor.cpp
    src/EventController.cpp
    src/EventMaker.cpp
    src/GameEventSystem.cpp
    src/GameEventSystemDB.cpp
    src/GameEventSystemDropEvent.cpp
    src/GarbageCollector.cpp
    src/BalthusEvent.cpp
    src/ExDrop.cpp
    src/FactionManager.cpp
    src/FieldCycleMissionData.cpp
    src/FieldCycleStuff.cpp
    src/FieldCycle.cpp
    src/FlyMove.cpp
    src/External/GameGuardServerLibCompat.cpp
    src/External/NCGuardServerLibCompat.cpp
    src/FontCheck.cpp
    src/Fishing.cpp
    src/Fortress.cpp
    src/Friends.cpp
    src/GMSetting.cpp
    src/GoodsInventory.cpp
    src/Henna.cpp
    src/HolyThing.cpp
    src/HTMLCache.cpp
    src/HtmlBuilder.cpp
    src/HtmlBuilderPacketEx.cpp
    src/HtmlCommand.cpp
    src/HtmlGenerator.cpp
    src/InventoryServer.cpp
    src/Inspector.cpp
    src/InstantAgit.cpp
    src/InstantZone.cpp
    src/IPBlock.cpp
    src/IOCPMessageServer.cpp
    src/IssueManager.cpp
    src/Item.cpp
    src/ItemSub.cpp
    src/ItemEnchantBonus.cpp
    src/ItemAssembler.cpp
    src/ItemAnnounceInfo.cpp
    src/ItemAnnouncer.cpp
    src/ItemAttrChange.cpp
    src/ItemCondition.cpp
    src/ItemOptionServer.cpp
    src/KalieEvent.cpp
    src/KillPVPPointChecker.cpp
    src/L2MobileNoti.cpp
    src/L2Packet.cpp
    src/BRInventoryServer.cpp
    src/BRUser.cpp
    src/BRUserItem.cpp
    src/SharedFactories.cpp
    src/SmartIdManager.cpp
    src/L2StatusWnd.cpp
    src/L2Time.cpp
    src/LambdaMemoryObject.cpp
    src/LuckyGameSystem.cpp
    src/MarketPriceList.cpp
    src/MainEvent.cpp
    src/MakerEvent.cpp
    src/Manor.cpp
    src/Mentor.cpp
    src/MentorSetting.cpp
    src/MiniGameAgit.cpp
    src/MonRaceData.cpp
    src/QuestManager.cpp
    src/PotentialSkill.cpp
    src/MonsterBook.cpp
    src/MultiPartyVisitor.cpp
    src/MultiPartyCommandChannelManager.cpp
    src/NewAction.cpp
    src/NewActionMove.cpp
    src/NewActionSkill.cpp
    src/NewActionHelper.cpp
    src/NCGuard.cpp
    src/NPC.cpp
    src/NpcBattleAgit.cpp
    src/Merchant.cpp
    src/NPConst.cpp
    src/NPSystem.cpp
    src/NPProtocol.cpp
    src/NPSocket.cpp
    src/NpcServer.cpp
    src/NoblessSystem.cpp
    src/NpcSocket.cpp
    src/NpcSocketEx.cpp
    src/ObjectDB.cpp
    src/OwnThing.cpp
    src/OptionDB.cpp
    src/VariationDB.cpp
    src/OlympiadNetwork.cpp
    src/OlympiadPacketHandlerInHost.cpp
    src/OlympiadPacketHandlerInPeer.cpp
    src/OlympiadServiceProxy.cpp
    src/OlympiadStuff.cpp
    src/OlympiadService.cpp
    src/OlympiadMatch.cpp
    src/OlympiadSystem.cpp
    src/Path.cpp
    src/PathToAwakeningNotice.cpp
    src/PCParam.cpp
    src/MapBug.cpp
    src/Pet.cpp
    src/PetDataSheet.cpp
  src/Pledge.cpp
  src/SubPledge.cpp
  src/PledgeBuilderCmdHandler.cpp
  src/PledgePenaltyViewer.cpp
  src/PledgeContribution.cpp
  src/PledgeContributionInfo.cpp
  src/PledgeInfo.cpp
  src/PledgeItem.cpp
  src/PledgeItemInfo.cpp
  src/PledgeJoinSystem.cpp
  src/PledgeFunctionInfo.cpp
  src/PledgeFunction.cpp
  src/PledgeGameSystem.cpp
  src/PledgeLevel.cpp
  src/PledgeLevelInfo.cpp
  src/PledgeMasteryInfo.cpp
  src/PledgeMastery.cpp
  src/PledgeMemberService.cpp
  src/PledgeMember.cpp
  src/PledgeMemberList.cpp
  src/PledgeMission.cpp
  src/PledgeMissionInfo.cpp
  src/PledgePacketHandler.cpp
  src/PledgeRaid.cpp
  src/PledgeRaidInfo.cpp
  src/PledgeRaidNetwork.cpp
  src/PledgeRaidPacketHandler.cpp
  src/PledgeSkillInfo.cpp
  src/PledgeSkill.cpp
  src/PledgeStuff.cpp
    src/PetitionCli.cpp
    src/PetitionSocket.cpp
    src/Post.cpp
    src/PrivateStoreMarket.cpp
    src/PremiumService.cpp
    src/PVPMatch.cpp
    src/PugiXml.cpp
    src/QueueTicket.cpp
    src/QuestionAnswer.cpp
    src/NewInspector.cpp
    src/Quest.cpp
    src/QuestHelper.cpp
    src/QuestData.cpp
    src/QuestBase.cpp
    src/Raid.cpp
    src/SSQ.cpp
    src/Recipe.cpp
    src/RuneItem.cpp
    src/RelationManager.cpp
    src/ReplyChecker.cpp
    src/Residence.cpp
    src/RestrictArea.cpp
    src/Castle.cpp
    src/SiegeAgit.cpp
    src/RestartOriginal.cpp
    src/RaidClientPacketHandler.cpp
    src/RaidServerPacketHandler.cpp
    src/RelayClientPacketHandler.cpp
    src/RelayClientSocket.cpp
    src/Relay.cpp
    src/ReservationAnnounceTimer.cpp
    src/ReservationNPCSpawnTimer.cpp
    src/SecureINIFileReader.cpp
    src/SequenceJob.cpp
    src/Server.cpp
    src/ServerType.cpp
    src/SerializedMessage.cpp
    src/Setting.cpp
    src/Shuttle.cpp
    src/ShortCut.cpp
    src/SlotIndexManager.cpp
    src/Portable/ClientProtocolRuntime.cpp
    src/Portable/Lu4Protocol.cpp
    src/Portable/Protocol152Compatibility.cpp
    src/Portable/ProcessRuntime.cpp
    src/Portable/RuntimeTimingTrace.cpp
    src/SharedContainer.cpp
    src/Skill.cpp
    src/SkillFxFunc.cpp
    src/SkillFxFuncAura.cpp
    src/SkillFxFuncConsume.cpp
    src/SkillFxFuncPump.cpp
    src/SkillFxFuncInstant.cpp
    src/SkillFxFuncTick.cpp
    src/SkillAcquire.cpp
    src/SkillEnchant.cpp
    src/SkillReuseDelay.cpp
    src/StartupLoaderGate.cpp
    src/StatData.cpp
    src/TotalChecker.cpp
    src/Trade.cpp
    src/TradeItem.cpp
    src/UIEventCountManager.cpp
    src/TeamBattleAgit.cpp
    src/TeamBattleFlag.cpp
    src/TeleportManager.cpp
    src/ToDoList.cpp
    src/TrainingZoneSetting.cpp
    src/Trap.cpp
    src/ThreadLocalWorldMap.cpp
    src/Transform.cpp
    src/Trait.cpp
    src/UndergroundColosseum.cpp
    src/User.cpp
    src/UserBasicAction.cpp
    src/UserBroadcast.cpp
    src/UserBroadcaster.cpp
    src/UserFriend.cpp
    src/UserItem.cpp
    src/UserPacket.cpp
    src/UserPledge.cpp
    src/UserSkill.cpp
    src/UserSummon.cpp
    src/UserWorld.cpp
    src/UserBookmark.cpp
    src/UserNickColor.cpp
    src/UserSocket.cpp
    src/UserSocketAgit.cpp
    src/UserSocketBbs.cpp
    src/UserUtil.cpp
    src/WareHouse.cpp
    src/WHeroSystem.cpp
    src/VipAttendance.cpp
    src/Vehicle.cpp
    src/UserSocketHTMLCmd.cpp
    src/WorldCollision.cpp
    src/Zzoldagu.cpp
    src/UserPacketEx.cpp
    src/XmlLoader.cpp
    src/BRXmlLoader.cpp
    ${L2SERVER_GRAMMAR_SOURCES})
target_include_directories(l2server_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Portable"
)

target_link_libraries(l2server_core
    PUBLIC Shared::L2Server
    PRIVATE LibXml2::LibXml2 ${CMAKE_DL_LIBS})
# Shared::L2Server also uses code that l2server_core defines, so each static
# library needs the other. GNU ld (Linux and MinGW) reads each library once,
# in order. With this cycle in the graph, CMake repeats both libraries on the
# link line. Apple ld searches all libraries and warns about repeated ones,
# so it does not get the cycle.
if(NOT APPLE)
    target_link_libraries(SharedL2Server INTERFACE "$<LINK_ONLY:l2server_core>")
endif()
if(WIN32)
    # NCGuardServerLibCompat reads file version resources.
    target_link_libraries(l2server_core PUBLIC version)
endif()

add_executable(L2Server
    src/L2Server.cpp
    # This TU owns current CRT-startup packet registration. No extracted
    # archive member references it, so it must also be a direct final object.
    src/ChannelChattingSocket.cpp)
target_link_libraries(L2Server PRIVATE l2server_core)
if(WIN32)
    target_link_libraries(L2Server PRIVATE version)
endif()
l2_configure_product_runtime(L2Server L2Server)
l2_stage_runtime_file(L2Server etc/l2server.ini l2server.ini)
l2_stage_runtime_file(L2Server etc/perfmon.ini perfmon.ini)
l2_stage_runtime_file(L2Server etc/secure.ini secure.ini)
l2_stage_runtime_file(L2Server etc/SerialKey.txt SerialKey.txt)
# Original NewInspector database, opened relative to the runtime directory.
l2_stage_runtime_file(L2Server etc/serverdata.dat serverdata.dat)
# NPC names and NpcStrings for the LU4 client (tools/client/lu4_npc_tables.py).
l2_stage_runtime_file(L2Server etc/lu4_npcname.txt lu4_npcname.txt)
l2_stage_runtime_file(L2Server etc/lu4_npcstring.txt lu4_npcstring.txt)
l2_stage_runtime_file(
    L2Server etc/NCGuard/ncgsvrcfg.ini NCGuard/ncgsvrcfg.ini)
l2_stage_runtime_file(
    L2Server etc/NCGuard/penalty_pattern.ini NCGuard/penalty_pattern.ini)

add_executable(
    l2server_serial_key
    tools/GenerateSerialKey.cpp)
target_link_libraries(
    l2server_serial_key
    PRIVATE Shared::Runtime)
l2_configure_product_runtime(l2server_serial_key L2Server)

# Current LoadData loads protocol 70 through the physical ggauth70.dll
# boundary named by the two-byte csauth2.cfg.  Build a native module with the
# same export surface. Never load or execute the shipping PE image.
add_library(l2server_ggauth70 MODULE
    src/External/GameGuardProtocol70.cpp)
set_target_properties(l2server_ggauth70 PROPERTIES
    OUTPUT_NAME ggauth70
    PREFIX ""
    SUFFIX ".dll")
target_link_libraries(l2server_ggauth70 PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(l2server_ggauth70 PRIVATE user32)
endif()
if(CMAKE_CONFIGURATION_TYPES)
    foreach(configuration IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(l2server_ggauth70 PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                "${CMAKE_BINARY_DIR}/${configuration}/L2Server"
            "LIBRARY_OUTPUT_DIRECTORY_${configuration_upper}"
                "${CMAKE_BINARY_DIR}/${configuration}/L2Server")
    endforeach()
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/L2Server/csauth2.cfg"
        CONTENT "70")
else()
    set_target_properties(l2server_ggauth70 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/L2Server"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/L2Server")
    file(GENERATE
        OUTPUT "${CMAKE_BINARY_DIR}/L2Server/csauth2.cfg"
        CONTENT "70")
endif()
add_dependencies(L2Server l2server_ggauth70)

l2_target_warnings(l2server_core L2Server l2server_serial_key l2server_ggauth70)
# PugiXml.cpp is a third-party copy of pugixml 1.0. Keep it unchanged and do
# not report its own style warnings.
if(NOT MSVC)
    set_source_files_properties(src/PugiXml.cpp PROPERTIES COMPILE_OPTIONS
        "-Wno-deprecated-declarations;-Wno-unused-function;$<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-string-plus-int>")
endif()

# The executable is the production completion gate. l2server_core remains a
# reusable implementation library for this target and the focused audit/test
# executables above. Building the archive alone does not prove link closure.
install(
    TARGETS L2Server
    RUNTIME DESTINATION L2Server)
install(
    TARGETS l2server_serial_key
    RUNTIME DESTINATION L2Server OPTIONAL)
install(
    TARGETS l2server_ggauth70
    RUNTIME DESTINATION L2Server
    LIBRARY DESTINATION L2Server)
install(
    FILES
        etc/l2server.ini
        etc/perfmon.ini
        etc/secure.ini
        etc/SerialKey.txt
        etc/serverdata.dat
        etc/lu4_npcname.txt
        etc/lu4_npcstring.txt
        "$<TARGET_FILE_DIR:L2Server>/csauth2.cfg"
    DESTINATION L2Server)
install(
    FILES etc/NCGuard/ncgsvrcfg.ini etc/NCGuard/penalty_pattern.ini
    DESTINATION L2Server/NCGuard)
