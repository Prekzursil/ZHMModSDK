#pragma once

//============================================================================
//  ForceChallenge — ZHMModSDK mod
//
//  General "force-complete any challenge" tool for HITMAN: World of Assassination.
//
//  HOW IT WORKS
//    The IOI server re-derives challenge completion from the uploaded gameplay
//    EVENT stream — not from any client "completed" flag. So this mod GRANTS a
//    challenge by emitting the genuine qualifying gameplay events into the live
//    event funnel on the GAME THREAD; the server then re-derives the challenge
//    and grants its reward (e.g. the Druzhina 34 ICA for "Quintuple Trouble").
//
//    Two ways to produce a type-faithful event:
//      * Construct mode — parse the captured JSON payload with the game's own
//        JSON deserializer (engine-owned, numeric types match real events).
//      * Capture-replay mode — observe a REAL event of the target type via the
//        OnEventSent hook, deep-copy it, and replay it N times (pure-SDK, the
//        most type-faithful; needs you to trigger the event once in-game).
//
//  HARD PRECONDITION
//    Events only reach the server when the online-send gate (dword_1430EA4B8)
//    == 2 (active online play). Offline, injected events serialize locally and
//    are never sent ⇒ no unlock. The mod refuses to inject otherwise and never
//    writes that gate.
//
//  SAFETY
//    * Game-thread only: injection happens inside the OnEventSent detour (game
//      thread); a re-entrancy guard stops injected events from cascading.
//    * Crash-safe memory: each injected event is RETAINED in a ring so the
//      queued copy can never become a use-after-free.
//    * Targeted: you configure the exact event for the exact challenge (shown
//      alongside its Drops[] GUID), so you don't advance unrelated challenges.
//
//  BUILD-SPECIFIC: the funnel / events-manager / parser / online-gate addresses
//  are RE'd from one game build (imagebase 0x140000000). They are rebased off
//  the runtime module base, but the OFFSETS must be re-resolved (by AOB) if the
//  game updates. See README.md.
//============================================================================

#include <cstdint>
#include <deque>
#include <string>

#include "IPluginInterface.h"

#include <Glacier/ZObject.h>
#include <Glacier/ZString.h>

class ZAchievementManagerSimple;

class ForceChallenge : public IPluginInterface {
public:
    void Init() override;
    void OnEngineInitialized() override;
    void OnDrawMenu() override;
    void OnDrawUI(bool p_HasFocus) override;

private:
    enum class Mode {
        Construct,      // build the event from JSON via the engine parser
        CaptureReplay,  // clone the next real matching event and replay it
    };

    // engine entry points (function pointers), resolved off the module base
    using ZEventFunnel_t = uint32_t (*)(void* p_EventsManager, ZDynamicObject* p_Event);
    using ZJsonParse_t = void* (*)(ZDynamicObject* p_Result, const char* p_Data, uint64_t p_Len, char p_Flag,
                                   const ZString* p_Label);

    void ResolveAddresses();
    bool OnlineGateOpen() const;                 // dword_1430EA4B8 == 2
    bool BuildFromJson(const std::string& p_Json, ZDynamicObject& p_Out) const;  // engine parser → dict
    void InjectOne(const ZDynamicObject& p_Event);  // retain + funnel, game thread only

private:
    DECLARE_PLUGIN_DETOUR(
        ForceChallenge, void, ZAchievementManagerSimple_OnEventSent, ZAchievementManagerSimple* th,
        uint32_t eventIndex, const ZDynamicObject& event
    );

private:
    // resolved addresses (0 until OnEngineInitialized)
    uintptr_t m_Base = 0;
    ZEventFunnel_t m_Funnel = nullptr;
    ZJsonParse_t m_Parser = nullptr;
    void* m_EventsManager = nullptr;
    const volatile int32_t* m_OnlineGate = nullptr;
    uintptr_t m_DictTypeId = 0;     // STypeID* sentinel for a dict (rebased 0x1445B3DC0)

    // ---- render-thread only (ImGui buffers; never read on the game thread) ----
    bool m_ShowWindow = false;
    Mode m_Mode = Mode::Construct;
    int m_RepeatCount = 2;          // Quintuple is at 3/5 by default ⇒ 2 more
    char m_EventJson[1024] =
        R"({"Name":"MultiKillTargets","ContractId":"00e57709-e049-44c9-a2c3-7655e19884fb","Value":{"KillCount":2}})";
    char m_TargetName[128] = "MultiKillTargets";  // event Name for capture-replay
    char m_DropsGuid[128] = "FIREARMS_SNIPER_DRUZHINA_34_ICA";
    char m_ChallengeId[128] = "66fe5dfa-2014-4f5d-9bb9-460078ed893d";

    // ---- shared between render + game threads: guard EVERY access with m_Lock ----
    // (the render thread arms/reads; the game-thread detour consumes/updates.) The
    // engine parser + funnel are NEVER called while holding the lock (re-entrancy
    // could try to re-acquire it → SRWLOCK is non-recursive → deadlock).
    SRWLOCK m_Lock = {};
    std::string m_Status = "idle";
    int m_Pending = 0;              // events left to emit
    int m_Done = 0;                 // events emitted this session
    Mode m_ArmedMode = Mode::Construct;  // snapshot of the mode at arm-time
    std::string m_ArmedJson;        // snapshot of m_EventJson at arm-time (no torn reads on the game thread)
    bool m_CaptureArmed = false;
    std::string m_CaptureName;      // snapshot of m_TargetName at capture-arm
    bool m_VerifyPending = false;   // "Build & Verify" runs on the game thread, not render
    std::string m_VerifyJson;

    // ---- game-thread only ----
    bool m_Injecting = false;       // re-entrancy guard (no lock: touched only on the game thread)
    bool m_HasTemplate = false;
    ZDynamicObject m_Template;      // deep copy of a real event
    static constexpr size_t kRetain = 32;
    std::deque<ZDynamicObject> m_Retained;  // retain injected events so the engine's queued copy can't dangle
};

DECLARE_ZHM_PLUGIN(ForceChallenge)
