#include "LottoGame.h"

#include "Shared/src/Runtime/Time32.h"

#include "DBNPC.h"
#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/NPCServer.h"
#include "L2NPC/src/ServerSocket.h"
#include "Portable/NPCServerConfiguration.h"
#include "Runtime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"
#include "net/Config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <optional>

namespace l2npc {

CLottoGameManager g_LottoGameManager;
int LOTTO_TICKET_PRICE = 2000;
int LOTTO_MINIMUM_WINNER_PRICE = -1;
// Current 0x006c1f1f starts zero and is written only by CConfig::Load.
bool g_bCanLotto = false;

const wchar_t* IntToWDay(int weekday) {
    static constexpr std::array<const wchar_t*, 7> kWeekdays{{L"sun", L"mon", L"tue", L"wed", L"thu", L"fri", L"sat"}};
    if (weekday >= 0 && weekday < static_cast<int>(kWeekdays.size())) {
        return kWeekdays[static_cast<std::size_t>(weekday)];
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"IntToWDay : invalid WDay[%d]",
    //     weekday)
    // Log.Add(LOG_ERROR,
    //     "[lottery] Cannot format weekday %d. Expected 0 (Sunday) to 6 (Saturday). "
    //     "Using an empty weekday name.", weekday)
    L2_LOG_ERROR(
        Log,
        "[lottery] Cannot format weekday %d; expected 0 (Sunday) to 6 (Saturday). "
        "Using an empty weekday name.",
        weekday
    );
    return L"";
}

namespace {

bool EqualAsciiCaseInsensitive(const char* left, const char* right) {
    if (!left || !right) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        const auto leftByte = static_cast<unsigned char>(*left++);
        const auto rightByte = static_cast<unsigned char>(*right++);
        if (std::tolower(leftByte) != std::tolower(rightByte)) {
            return false;
        }
    }
    return *left == *right;
}

void TargetLocalWeekdayAndHour(const std::int32_t* unixSeconds, int& weekday, int& hour) {
    // Current L2NPC.exe 0x004645bc/0x004645da calls the CRT localtime
    // routine and immediately dereferences its returned pointer. Keep that
    // unchecked boundary: a corrupt out-of-range persisted LastTime must not
    // silently suppress the shipping state transition.
    const std::tm* const local = runtime::crt::LocalTime32(*unixSeconds);
    weekday = local->tm_wday;
    hour = local->tm_hour;
}

void SendBroadcastSystemMessage(
    std::uint16_t messageId, int roundNumber, std::int64_t accumulatedReward, std::optional<int> winnerCount
) {
    CServerSocket* const serverSocket = g_ns.GetServerSocket();
    if (winnerCount) {
        serverSocket->BroadcastSystemMessageW(
            const_cast<char*>("hccdcdcd"), static_cast<int>(messageId), 3, 1, roundNumber, 1,
            static_cast<int>(accumulatedReward), 1, *winnerCount
        );
        return;
    }

    serverSocket->BroadcastSystemMessageW(
        const_cast<char*>("hccdcd"), static_cast<int>(messageId), 2, 1, roundNumber, 1, static_cast<int>(accumulatedReward)
    );
}

void SendBuilderText(int requesterId, const std::string& text) {
    const std::u16string wide = Utf8ToUtf16(text);
    g_ns.Send("cdS", 0x54, requesterId, wide.c_str());
}

} // namespace

void CLottoGameManager::Initialize() {
    L2_GUARD_FUNCTION();

    std::array<char, 1000> weekday{};
    int hour = 19;
    int day = 6;
    const runtime::Config* const configuration = portable::GetNPCServerConfiguration();
    std::u16string announcementWide = Utf8ToUtf16(
        configuration && configuration->Loaded() ? configuration->Get("setting", "LottoAnnounce", "sat 19")
                                                 : std::string("sat 19")
    );
    if (announcementWide.size() > 998U) {
        announcementWide.resize(998U);
    }
    // The current image really uses the 1008-byte stack local even though
    // UnicodeToAnsi ignores its nominal 499-byte destination limit.
    std::array<char, 1008> announcementAnsi{};
    UnicodeToAnsi(announcementWide.data(), 999, announcementAnsi.data(), 499);

    // The target uses sscanf_s("%s %d", weekday, 1000, &hour), accepts
    // trailing input, performs _stricmp calls and does not constrain the
    // parsed hour.
    const int parsed = std::sscanf(announcementAnsi.data(), "%999s %d", weekday.data(), &hour);
    if (parsed == 2) {
        static constexpr std::array<const char*, 7> kWeekdays{{"sun", "mon", "tue", "wed", "thu", "fri", "sat"}};
        bool matched = false;
        for (int index = 0; index < static_cast<int>(kWeekdays.size()); ++index) {
            if (EqualAsciiCaseInsensitive(weekday.data(), kWeekdays[index])) {
                day = index;
                matched = true;
                break;
            }
        }
        if (!matched) {
            hour = 19;
        }
    } else {
        hour = 19;
    }

    m_AnnounceDay = day;
    m_AnnounceHour = hour;
    CurRound = 0;
    dbnpc.LoadLottoInfo();
}

void CLottoGameManager::AddLottoRoundFromDB(const CLottoRound& round) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        m_LottoRoundMap[round.RoundNumber] = round;
        CurRound = std::max(CurRound, round.RoundNumber);
    }
}

void CLottoGameManager::WaitForLoading(int expectedRoundCount) {
    L2_GUARD_FUNCTION();

    // L2_LOG_INFO(Log,
    //     "Lottery initialization is waiting for saved rounds from CacheD: expected rounds=%d.",
    //     expectedRoundCount)
    // Previous: L2_LOG_INFO(Log,
    //     "Lottery initialization requires %d saved rounds from CacheD. Checking whether they have arrived.",
    //     expectedRoundCount)
    L2_LOG_INFO(
        Log, "[lottery] Initialization requires %d saved rounds from CacheD; checking whether they have arrived.",
        expectedRoundCount
    );
    for (;;) {
        bool loaded;
        {
            L2_GUARD(m_Sect);
            // 0x0046428b compares the 64-bit map size with the sign-extended
            // explicit int. It does not truncate the container size to int.
            loaded = m_LottoRoundMap.size() == static_cast<std::size_t>(expectedRoundCount);
        }
        if (loaded) {
            break;
        }
    }

    {
        L2_GUARD(m_Sect);
        const runtime::Config* const configuration = portable::GetNPCServerConfiguration();
        const std::uint32_t presetReward = configuration && configuration->Loaded()
            ? static_cast<std::uint32_t>(configuration->GetInt("setting", "LottoPresetReward", 0))
            : 0U;
        // GetPrivateProfileIntW returns UINT. The current call at 0x004642e1
        // therefore zero-extends EAX into the nominal __int64 reward argument.
        SetPresetReward(0, static_cast<std::int64_t>(presetReward));
        if (CurRound == 0) {
            StartNextRound();
        }
        Initialized = true;
        // Log.Add(LOG_NORMAL, "[lottery] Loaded %d saved rounds, ready at round %d.",
        //     expectedRoundCount, CurRound)
        L2_LOG_INFO(Log, "[lottery] Loaded %d saved rounds; ready at round %d.", expectedRoundCount, CurRound);
    }
    AddTimer(10U, 0);
}

void CLottoGameManager::Update(int roundNumber, int totalCount) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        CLottoRound& current = GetCurRound();
        if (current.RoundNumber != roundNumber) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] LottoGameManager::Update "
            //     L"failed. Top Round[%d] != Round[%d]",
            //     L"LottoGame.cpp",
            //     0x1f6,
            //     roundNumber,
            //     totalCount)
            // Log.Add(LOG_ERROR,
            //     "[lottery] Ignoring ticket-count update for round %d (%d tickets): "
            //     "the current round is %d.", roundNumber, totalCount, current.RoundNumber)
            L2_LOG_ERROR(
                Log,
                "[lottery] Ignoring ticket-count update for round %d (%d tickets): "
                "the current round is %d.",
                roundNumber, totalCount, current.RoundNumber
            );
        } else {
            current.TotalCount = totalCount;
        }
    }
}

void CLottoGameManager::UpdateDrawingState(
    int roundNumber, int winner1Count, int winner2Count, int winner3Count, int winner4Count
) {
    L2_GUARD_FUNCTION();

    // Current L2NPC.exe 0x00462a38 jumps directly to the epilogue on
    // a round mismatch, bypassing Leave at 0x00462a6a. Keep this interval
    // manual: a local guard would change that shipping branch's ownership.
    m_Sect.Enter(L"LottoGame.cpp", 0x207);
    CLottoRound& current = GetCurRound();
    if (current.RoundNumber != roundNumber) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UpdateDrawingState failed. "
        //     L"Current Round[%d] != Round[%d]",
        //     L"LottoGame.cpp",
        //     0x20d,
        //     current.RoundNumber,
        //     roundNumber)
        // Log.Add(LOG_ERROR,
        //     "[lottery] Ignoring drawing results for round %d: the current round is %d.",
        //     roundNumber, current.RoundNumber)
        L2_LOG_ERROR(
            Log, "[lottery] Ignoring drawing results for round %d: the current round is %d.", roundNumber,
            current.RoundNumber
        );
        return;
    }
    current.DrawingSuccessed = true;
    current.Winner1Count = winner1Count;
    current.Winner2Count = winner2Count;
    current.Winner3Count = winner3Count;
    current.Winner4Count = winner4Count;
    m_Sect.Leave(L"LottoGame.cpp", 0x217);
}

std::int64_t CLottoGameManager::CalculateReward(int roundNumber, int numberFlag, int& rank) {
    L2_GUARD_FUNCTION();

    rank = 0;
    // Current L2NPC.exe 0x0046352d queries the process-global
    // manager here, even though the remaining calculation uses this.
    if (g_LottoGameManager.GetState(roundNumber) == -1) {
        rank = 4;
        return LOTTO_TICKET_PRICE;
    }

    std::int64_t reward;
    {
        L2_GUARD(m_Sect);
        CLottoRound& round = GetRound(roundNumber);
        std::uint32_t matched = static_cast<std::uint32_t>(round.ChosenNumber) & static_cast<std::uint32_t>(numberFlag);
        int matchedCount = 0;
        while (matched != 0) {
            if ((matched & 1U) != 0) {
                ++matchedCount;
            }
            matched >>= 1;
        }

        reward = 0;
        if (matchedCount < 3) {
            rank = 4;
            reward = 2;
        } else if (matchedCount == 3) {
            rank = 3;
            reward = round.Winner3Reward();
        } else if (matchedCount == 4) {
            rank = 2;
            reward = round.Winner2Reward();
        } else if (matchedCount == 5) {
            rank = 1;
            reward = round.Winner1Reward();
        }
    }
    return reward;
}

void CLottoGameManager::MakeFinalResultFHTML(CFHTML* html) {
    L2_GUARD_FUNCTION();

    if (GetTopState() != 3) {
        return;
    }
    if (!html) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MakeFinalResultFHTML "
        //     L"failed. html=NULL",
        //     L"LottoGame.cpp",
        //     0x2c0)
        // Log.Add(LOG_ERROR, "[lottery] Cannot prepare the results page: the HTML object is null.")
        L2_LOG_ERROR(Log, "[lottery] Cannot prepare the results page: the HTML object is null.");
        return;
    }

    {
        L2_GUARD(m_Sect);
        CLottoRound& round = GetCurRound();
        html->Set(u"lotto_round", round.RoundNumber);
        html->Set(u"total_reward", GetAccumulatedReward());
        html->Set(u"1st_num", round.Winner1Count);
        html->Set(u"2nd_num", round.Winner2Count);
        html->Set(u"3rd_num", round.Winner3Count);
        html->Set(u"1st_reward", round.Winner1Reward());
        html->Set(u"2nd_reward", round.Winner2Reward());
        html->Set(u"3rd_reward", round.Winner3Reward());
        html->Set(u"4th_reward", round.Winner4Reward());

        const auto setCarry = [html](const char16_t* key, std::int64_t value) {
            const std::string text = "(" + std::to_string(runtime::Int32FromBits(value)) +
                " <fstring p1=\"\" p2=\"\" p3=\"\" "
                "p4=\"\" p5=\"\">1000171</fstring>.)";
            const std::u16string textUtf16 = Utf8ToUtf16(text);
            html->Set(key, textUtf16.c_str());
        };
        if (round.Winner1Count == 0) {
            setCarry(u"carry1", round.Winner1Carry());
        }
        if (round.Winner2Count == 0) {
            setCarry(u"carry2", round.Winner2Carry());
        }
        if (round.Winner3Count == 0) {
            setCarry(u"carry3", round.Winner3Carry());
        }
    }
}

void CLottoGameManager::CancelRound(int requesterId, int roundNumber) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        const char* response = nullptr;
        if (roundNumber == CurRound) {
            CLottoRound& current = GetCurRound();
            current.State = -1;
            SaveTopRound();
            StartNextRound();
            response = "Set the current round canceled. lotto system paused.";
        } else if (roundNumber < CurRound) {
            auto found = m_LottoRoundMap.find(roundNumber);
            if (found == m_LottoRoundMap.end()) {
                response = "fail to cancel the round. invalid round number.";
            } else {
                found->second.State = -1;
                dbnpc.Lotto_SaveRound(found->second);
                response = "Set the round canceled.";
            }
        } else {
            response = "Round number is bigger than current round";
        }
        SendBuilderText(requesterId, response);
    }
}

void CLottoGameManager::ShowRoundInfo(int requesterId, int roundNumber) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        if (roundNumber == 0) {
            roundNumber = CurRound;
        }
        const auto found = m_LottoRoundMap.find(roundNumber);
        CLottoRound* const round = found == m_LottoRoundMap.end() ? nullptr : &found->second;
        static constexpr const char* kSeparator = "----------------------------------";
        if (round) {
            SendBuilderText(requesterId, kSeparator);
            SendBuilderText(
                requesterId,
                "Round[" + std::to_string(round->RoundNumber) + "], Rule[" + std::to_string(round->RuleNumber) +
                    "], State[" + WideToUtf8(round->GetStateString()) + "]"
            );
            SendBuilderText(
                requesterId,
                "Ticket sell [" + std::to_string(round->TotalCount) + "], Carried Adena [" +
                    std::to_string(runtime::Int32FromBits(round->CarriedAdena)) + "]"
            );
            SendBuilderText(
                requesterId, "Total prize [" + std::to_string(runtime::Int32FromBits(round->GetTotalReward())) + "]"
            );
            SendBuilderText(
                requesterId, "DrawingTime : " + WideToUtf8(IntToWDay(m_AnnounceDay)) + " " + std::to_string(m_AnnounceHour)
            );

            if (round->ChosenNumber != 0) {
                SendBuilderText(
                    requesterId,
                    std::string(round->State == 1 ? "preset numbers " : "chosen numbers ") +
                        Utf16ToUtf8(runtime::StringView(g_LottoGameManager.CastLottoNumber(round->ChosenNumber)))
                );
            }

            if (round->State == 3 || round->State == -1) {
                SendBuilderText(
                    requesterId,
                    "winner 1, reward[" + std::to_string(runtime::Int32FromBits(round->Winner1Reward())) + "], count[" +
                        std::to_string(round->Winner1Count) + "]"
                );
                SendBuilderText(
                    requesterId,
                    "winner 2, reward[" + std::to_string(runtime::Int32FromBits(round->Winner2Reward())) + "], count[" +
                        std::to_string(round->Winner2Count) + "]"
                );
                SendBuilderText(
                    requesterId,
                    "winner 3, reward[" + std::to_string(runtime::Int32FromBits(round->Winner3Reward())) + "], count[" +
                        std::to_string(round->Winner3Count) + "]"
                );
                SendBuilderText(requesterId, "winner 4, reward[2], count[" + std::to_string(round->Winner4Count) + "]");
            }
        }
        SendBuilderText(requesterId, kSeparator);
    }
}

void CLottoGameManager::SetPresetReward(int requesterId [[maybe_unused]], std::int64_t reward) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        CLottoRound& current = GetCurRound();
        current.PresetReward = runtime::Int32FromBits(reward);
        current.CarriedAdena = reward;
        m_PresetReward = reward;
        dbnpc.Lotto_SaveRound(current);
        SetPresetRewardProfile(reward);
    }
}

void CLottoGameManager::SetNumberFlag(int requesterId [[maybe_unused]], int numberFlag) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        CLottoRound& current = GetCurRound();
        current.ChosenNumber = numberFlag;
        dbnpc.Lotto_SaveRound(current);
    }
}

void CLottoGameManager::SetDrawingTime(int requesterId [[maybe_unused]], int weekday, int hour) {
    L2_GUARD_FUNCTION();

    m_AnnounceDay = weekday;
    m_AnnounceHour = hour;
    std::array<wchar_t, 1000> value{};
    std::swprintf(value.data(), value.size(), L"\"%ls %d\"", IntToWDay(weekday), hour);
    SetPrivateProfile(L"setting", L"LottoAnnounce", value.data(), L".\\l2npc.ini");
}

void CLottoGameManager::TimerExpired(int) {
    L2_GUARD_FUNCTION();

    if (Pause) {
        AddTimer(1000U, 0);
        return;
    }

    {
        L2_GUARD(m_Sect);
        CLottoRound& round = GetCurRound();

        switch (round.State) {
            case 1: {
                if (round.StateFirstTime) {
                    round.StateFirstTime = false;
                    round.LastTime = runtime::crt::Time32Now();
                }

                int previousWeekday = 0;
                int previousHour = 0;
                int currentWeekday = 0;
                int currentHour = 0;
                const std::int32_t currentTime = runtime::crt::Time32Now();
                TargetLocalWeekdayAndHour(&round.LastTime, previousWeekday, previousHour);
                TargetLocalWeekdayAndHour(&currentTime, currentWeekday, currentHour);
                round.LastTime = currentTime;

                if (previousWeekday == m_AnnounceDay && previousHour < m_AnnounceHour && currentWeekday == m_AnnounceDay &&
                    m_AnnounceHour <= currentHour) {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     L"LOTTO : SELL -> CHOOSING")
                    // Log.Add(LOG_NORMAL,
                    //     "[lottery] Round %d: ticket sales ended. Entering the drawing phase.",
                    //     round.RoundNumber)
                    L2_LOG_INFO(
                        Log, "[lottery] Round %d: ticket sales ended; entering the drawing phase.", round.RoundNumber
                    );
                    round.State = 2;
                    round.StateFirstTime = true;
                }
                round.LeftTime = 60;
                SaveTopRound();
                AddTimer(30000U, 0);
                break;
            }

            case 2:
                if (round.StateFirstTime) {
                    Drawing();
                    round.StateFirstTime = false;
                }
                if (!round.DrawingSuccessed) {
                    AddTimer(30000U, 0);
                    break;
                }

                round.LeftTime = runtime::AddInt32Wrapping(round.LeftTime, -30);
                if (round.LeftTime > 0) {
                    SaveTopRound();
                    AddTimer(30000U, 0);
                    break;
                }

                // Log.Add(
                //     LOG_NORMAL,
                //     L"LOTTO : CHOOSING -> ANNOUNCING")
                // Log.Add(LOG_NORMAL,
                //     "[lottery] Round %d: drawing completed. Entering the results phase.",
                //     round.RoundNumber)
                L2_LOG_INFO(Log, "[lottery] Round %d: drawing completed; entering the results phase.", round.RoundNumber);
                round.State = 3;
                round.LeftTime = 600;
                round.StateFirstTime = true;
                SaveTopRound();
                AddTimer(30000U, 0);
                {
                    const std::int64_t reward = round.GetTotalReward();
                    if (round.Winner1Count == 0) {
                        if (reward > 0) {
                            SendBroadcastSystemMessage(0x0459, round.RoundNumber, reward, std::nullopt);
                        }
                    } else {
                        SendBroadcastSystemMessage(0x0458, round.RoundNumber, reward, round.Winner1Count);
                    }
                }
                break;

            case 3:
                round.StateFirstTime = false;
                round.LeftTime = runtime::AddInt32Wrapping(round.LeftTime, -30);
                if (round.LeftTime > 0) {
                    SaveTopRound();
                    AddTimer(30000U, 0);
                } else {
                    // Log.Add(
                    //     LOG_NORMAL,
                    //     L"LOTTO : ANNOUNCING -> SELL")
                    // Log.Add(LOG_NORMAL,
                    //     "[lottery] Round %d: results phase ended. Starting the next round.",
                    //     round.RoundNumber)
                    L2_LOG_INFO(Log, "[lottery] Round %d: results phase ended; starting the next round.", round.RoundNumber);
                    StartNextRound();
                    AddTimer(30000U, 0);
                }
                break;

            default:
                break;
        }
    }
}

void CLottoGameManager::Drawing() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        CLottoRound& round = GetCurRound();
        if (round.ChosenNumber == 0) {
            std::uint32_t mask = 0;
            int count = 0;
            do {
                const int value = LegacyCrtRand();
                const unsigned bitIndex = static_cast<unsigned>(value % 20);
                const std::uint32_t bit = 1U << bitIndex;
                if ((mask & bit) == 0) {
                    mask |= bit;
                    ++count;
                }
            } while (count < 5);
            round.ChosenNumber = static_cast<std::int32_t>(mask);
        }

        dbnpc.Lotto_Drawing(
            round.RoundNumber, round.TotalCount, round.CarriedAdena, round.ChosenNumber, round.LeftTime,
            static_cast<std::int64_t>(round.PresetReward)
        );
    }
}

void CLottoGameManager::SaveTopRound() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        dbnpc.Lotto_SaveRound(GetCurRound());
    }
}

void CLottoGameManager::StartNextRound() {
    L2_GUARD_FUNCTION();

    std::optional<CLottoRound> nextStorage;
    {
        L2_GUARD(m_Sect);
        std::int64_t carriedAdena = 0;
        if (CurRound > 0) {
            CLottoRound& previous = GetCurRound();
            if (previous.IsCanceled()) {
                carriedAdena = previous.CarriedAdena;
            } else {
                carriedAdena = previous.TotalCarry();
            }
        }

        auto& next = nextStorage.emplace();
        CurRound = runtime::AddInt32Wrapping(CurRound, 1);
        next.RoundNumber = CurRound;
        next.State = 1;
        next.LeftTime = 0x25cb0;
        next.CarriedAdena = carriedAdena;
        next.StateFirstTime = true;
        m_LottoRoundMap[next.RoundNumber] = next;
    }

    // ResetPresetReward mutates the newly inserted round, rewrites the INI
    // profile and does not save another round snapshot.
    auto& next = *nextStorage;
    ResetPresetReward();
    dbnpc.Lotto_StartNextRound(next.RoundNumber, next.LeftTime, next.CarriedAdena);
}

void CLottoGameManager::ResetPresetReward() {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_Sect);
        GetCurRound().PresetReward = 0;
        m_PresetReward = 0;
        SetPresetRewardProfile(0);
    }
}

void CLottoGameManager::SetPresetRewardProfile(std::int64_t reward) {
    L2_GUARD_FUNCTION();

    // The current EXE calls wsprintfW(L"%d", reward): the variadic format
    // consumes only the signed low dword of this nominal __int64 argument.
    std::array<wchar_t, 1000> value{};
    std::swprintf(value.data(), value.size(), L"%d", runtime::Int32FromBits(reward));
    SetPrivateProfile(L"setting", L"LottoPresetReward", value.data(), L".\\l2npc.ini");
}

std::u16string CLottoGameManager::CastLottoNumber(int numberFlag) {
    L2_GUARD_FUNCTION();

    std::u16string result;
    const std::uint32_t bits = static_cast<std::uint32_t>(numberFlag);
    for (unsigned bit = 0; bit < 20; ++bit) {
        if ((bits & (1U << bit)) == 0) {
            continue;
        }
        const std::string number = std::to_string(bit + 1);
        std::u16string wideNumber;
        wideNumber.reserve(number.size());
        for (const char byte : number) {
            wideNumber.push_back(static_cast<char16_t>(static_cast<unsigned char>(byte)));
        }
        // Current 0x00464688 emits the lvalue-string + pointer helper,
        // followed by the rvalue-string + pointer helper for the space.
        result = result + wideNumber.c_str() + u" ";
    }
    return result;
}

CLottoRound& CLottoGameManager::GetRound(int roundNumber) {
    // Current L2NPC.exe 0x00462614 initializes this function-local fallback
    // before looking in m_LottoRoundMap, including when the requested round
    // is present.
    L2_GUARD_FUNCTION();

    // Noise: WaitForLoading calls this through SetPresetReward and
    // StartNextRound before setting Initialized. That normal startup access
    // still performs the lookup below. The flag does not prove lookup failure.
    // if (!Initialized) {
    //     Log.Add(
    //         LOG_ERROR,
    //         L"[%s][%d] LottoGameManager::GetRound failed.. "
    //         L"function called before initialized",
    //         L"LottoGame.cpp",
    //         0x1d2)
    //     ReportCallStack(LOG_ERROR, false)
    // }
    static CLottoRound r;
    auto found = m_LottoRoundMap.find(roundNumber);
    return found == m_LottoRoundMap.end() ? r : found->second;
}

CLottoRound& CLottoGameManager::GetCurRound() {
    L2_GUARD_FUNCTION();

    return GetRound(CurRound);
}

int CLottoGameManager::GetTopState() {
    L2_GUARD_FUNCTION();

    if (!Initialized) {
        return 0;
    }
    int state;
    {
        L2_GUARD(m_Sect);
        state = GetCurRound().State;
    }
    return state;
}

int CLottoGameManager::GetState(int roundNumber) {
    L2_GUARD_FUNCTION();

    int state;
    {
        L2_GUARD(m_Sect);
        state = GetRound(roundNumber).State;
    }
    return state;
}

int CLottoGameManager::GetAccumulatedReward() {
    L2_GUARD_FUNCTION();

    std::int64_t reward;
    {
        L2_GUARD(m_Sect);
        reward = GetCurRound().GetTotalReward();
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(reward));
}

int CLottoGameManager::GetChosenNumber() {
    L2_GUARD_FUNCTION();

    int chosen;
    {
        L2_GUARD(m_Sect);
        chosen = GetCurRound().ChosenNumber;
    }
    return chosen;
}

std::u16string CLottoGameManager::GetChooseTime() {
    L2_GUARD_FUNCTION();

    bool choosing;
    {
        L2_GUARD(m_Sect);
        choosing = GetTopState() == 1;
        if (!choosing) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CLottoGame::GetChooseTime "
            //     L"failed. top state [%d]",
            //     L"LottoGame.cpp",
            //     0x25d,
            //     GetTopState())
            // Log.Add(LOG_WARN,
            //     "[lottery] Cannot display the draw schedule in state %d. Using the placeholder '...'.",
            //     GetTopState())
            L2_LOG_WARN(
                Log, "[lottery] Cannot display the draw schedule in state %d; using the placeholder '...'.", GetTopState()
            );
        }
    }
    if (choosing) {
        // Original: u"토요일 19시"
        return std::u16string(std::u16string_view(u"Saturday 19:00"));
    }
    return std::u16string(std::u16string_view(u"..."));
}

std::u16string CLottoGameManager::GetChosenNumberString() {
    L2_GUARD_FUNCTION();

    return CastLottoNumber(GetChosenNumber());
}

} // namespace l2npc
