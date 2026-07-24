#include "ForceChallenge.h"

#include <Windows.h>

#include "Hooks.h"
#include "Logging.h"
#include "Functions.h"

#include "json.hpp"

// Offsets are taken from the analysed build (imagebase 0x140000000) and rebased
// off the runtime module base. Re-resolve by AOB if the game updates.
namespace {
    constexpr uintptr_t kImageBase = 0x140000000;
    constexpr uintptr_t kOffFunnel = 0x140CB0860 - kImageBase;  // gameplay event funnel
    constexpr uintptr_t kOffParser = 0x14151DCB0 - kImageBase;  // generic JSON deserializer
    constexpr uintptr_t kOffEvents = 0x1430EAEA0 - kImageBase;  // ZEventsManager singleton
    constexpr uintptr_t kOffGate = 0x1430EA4B8 - kImageBase;    // online-send gate dword
    constexpr uintptr_t kOffDict = 0x1445B3DC0 - kImageBase;    // dict STypeID* sentinel
}

void ForceChallenge::Init() {
    InitializeSRWLock(&m_Lock);
    Hooks::ZAchievementManagerSimple_OnEventSent->AddDetour(
        this, &ForceChallenge::ZAchievementManagerSimple_OnEventSent
    );
}

void ForceChallenge::OnEngineInitialized() {
    ResolveAddresses();
    Logger::Info(
        "ForceChallenge ready. funnel={} parser={} eventsMgr={} gate={}", reinterpret_cast<void*>(m_Funnel),
        reinterpret_cast<void*>(m_Parser), m_EventsManager, reinterpret_cast<const void*>(m_OnlineGate)
    );
}

void ForceChallenge::ResolveAddresses() {
    m_Base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    m_Funnel = reinterpret_cast<ZEventFunnel_t>(m_Base + kOffFunnel);
    m_Parser = reinterpret_cast<ZJsonParse_t>(m_Base + kOffParser);
    m_EventsManager = reinterpret_cast<void*>(m_Base + kOffEvents);
    m_OnlineGate = reinterpret_cast<const volatile int32_t*>(m_Base + kOffGate);
    m_DictTypeId = m_Base + kOffDict;
}

bool ForceChallenge::OnlineGateOpen() const {
    return m_OnlineGate != nullptr && *m_OnlineGate == 2;
}

bool ForceChallenge::BuildFromJson(const std::string& p_Json, ZDynamicObject& p_Out) const {
    if (!m_Parser)
        return false;

    // A default ZString is {0x80000000, ""} — the safe empty label: bit31 is set,
    // so the parser skips the refcount/string-pool paths (passing a raw char* there
    // would corrupt the heap). The parser copies the JSON content into p_Out, which
    // then owns engine-allocated, type-faithful data (numeric types match real events).
    ZString s_Label;
    m_Parser(&p_Out, p_Json.c_str(), p_Json.size(), 0, &s_Label);

    // Verify it parsed to a dict: the raw sentinel I verified in IDA, plus the SDK check.
    if (reinterpret_cast<uintptr_t>(p_Out.GetTypeID()) == m_DictTypeId)
        return true;
    return p_Out.IsObject();
}

void ForceChallenge::InjectOne(const ZDynamicObject& p_Event) {
    if (!m_Funnel || !m_EventsManager)
        return;

    // Retain a deep copy so the engine's queued copy can never become a
    // use-after-free, regardless of whether the funnel copies or stores by ref.
    m_Retained.emplace_back(p_Event);
    if (m_Retained.size() > kRetain)
        m_Retained.pop_front();

    m_Funnel(m_EventsManager, &m_Retained.back());
    // m_Done is incremented by the caller under m_Lock (it's read on the render thread).
}

DEFINE_PLUGIN_DETOUR(
    ForceChallenge, void, ZAchievementManagerSimple_OnEventSent, ZAchievementManagerSimple* th, uint32_t eventIndex,
    const ZDynamicObject& event
) {
    // Re-entrancy guard (game-thread-only flag): never act on our own injected events.
    if (m_Injecting)
        return HookResult<void>(HookAction::Continue());

    // Snapshot shared intent under the lock, then RELEASE it before any engine call.
    // The funnel can re-enter this detour on the same thread, and SRWLOCK is
    // non-recursive, so holding it across an engine call would deadlock.
    bool s_DoCapture, s_DoVerify, s_DoInject;
    Mode s_Mode;
    std::string s_CaptureName, s_VerifyJson, s_Json;
    AcquireSRWLockExclusive(&m_Lock);
    s_DoCapture = m_CaptureArmed;
    s_CaptureName = m_CaptureName;
    s_DoVerify = m_VerifyPending;
    s_VerifyJson = m_VerifyJson;
    m_VerifyPending = false;
    s_DoInject = m_Pending > 0;
    s_Mode = m_ArmedMode;
    s_Json = m_ArmedJson;
    ReleaseSRWLockExclusive(&m_Lock);

    // Capture-replay: stash a deep copy of the next real event matching the target Name.
    if (s_DoCapture) {
        ZString s_Data;
        Functions::ZDynamicObject_ToString->Call(const_cast<ZDynamicObject*>(&event), &s_Data);
        try {
            const auto s_JsonEv = nlohmann::json::parse(s_Data.c_str(), s_Data.c_str() + s_Data.size());
            const std::string s_Name = s_JsonEv.value("Name", std::string());
            if (s_Name == s_CaptureName) {
                m_Template = event; // deep copy via inherited ZObjectRef::operator= (game thread)
                AcquireSRWLockExclusive(&m_Lock);
                m_HasTemplate = true;
                m_CaptureArmed = false;
                m_Status = "captured template: " + s_Name;
                ReleaseSRWLockExclusive(&m_Lock);
            }
        }
        catch (...) {
            // non-JSON / unexpected event — ignore
        }
    }

    // Build & Verify: run the engine parser on the GAME THREAD (never the render thread).
    if (s_DoVerify) {
        ZDynamicObject s_Test;
        const bool s_Ok = BuildFromJson(s_VerifyJson, s_Test);
        AcquireSRWLockExclusive(&m_Lock);
        m_Status = s_Ok ? "event JSON OK (valid dict)" : "INVALID: JSON did not parse to a dict";
        ReleaseSRWLockExclusive(&m_Lock);
        // s_Test destructs here, on the game thread — safe.
    }

    // Emit pending events as real gameplay events flow (an injected event may itself
    // drive the next emission, so >1 can go out per frame — bounded by m_Pending; the
    // CE script is the path for strictly one-at-a-time gated rollout). Gated on online state.
    if (s_DoInject) {
        if (!OnlineGateOpen()) {
            AcquireSRWLockExclusive(&m_Lock);
            m_Status = "BLOCKED: online gate != 2 (events would not be sent) — withheld";
            ReleaseSRWLockExclusive(&m_Lock);
        }
        else {
            bool s_Ok = true;
            std::string s_Abort;

            m_Injecting = true;
            if (s_Mode == Mode::CaptureReplay) {
                if (m_HasTemplate)
                    InjectOne(m_Template);
                else {
                    s_Ok = false;
                    s_Abort = "ABORT: capture-replay armed but no template captured yet";
                }
            }
            else { // Construct
                ZDynamicObject s_Event;
                if (BuildFromJson(s_Json, s_Event))
                    InjectOne(s_Event);
                else {
                    s_Ok = false;
                    s_Abort = "ABORT: event JSON did not parse to a dict";
                }
            }
            m_Injecting = false;

            AcquireSRWLockExclusive(&m_Lock);
            if (!s_Ok) {
                m_Pending = 0;
                m_Status = s_Abort;
            }
            else if (m_Pending > 0) { // re-check: the render thread may have cancelled mid-flight
                ++m_Done;
                --m_Pending;
                if (m_Pending == 0)
                    m_Status = "emitted all events — verify the unlock in-game (loadout/planning to refresh)";
            }
            ReleaseSRWLockExclusive(&m_Lock);
        }
    }

    return HookResult<void>(HookAction::Continue());
}

void ForceChallenge::OnDrawMenu() {
    if (ImGui::Button("FORCE CHALLENGE"))
        m_ShowWindow = !m_ShowWindow;
}

void ForceChallenge::OnDrawUI(bool p_HasFocus) {
    if (!m_ShowWindow)
        return;

    ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const bool s_Open = ImGui::Begin("Force Challenge###ForceChallenge", &m_ShowWindow);
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Open) {
        // snapshot shared (game-thread-updated) state for display
        AcquireSRWLockShared(&m_Lock);
        const std::string s_Status = m_Status;
        const int s_Pending = m_Pending;
        const int s_Done = m_Done;
        const bool s_CapArmed = m_CaptureArmed;
        const bool s_HasTpl = m_HasTemplate;
        ReleaseSRWLockShared(&m_Lock);

        const bool s_Online = OnlineGateOpen();
        ImGui::Text(
            "Online send gate: %s",
            s_Online ? "OPEN (==2) - events WILL reach the server" : "CLOSED (!=2) - events will NOT be sent"
        );
        if (!s_Online)
            ImGui::TextWrapped("Get the game into ACTIVE ONLINE PLAY (a live session). Do NOT force the gate.");

        ImGui::Separator();
        ImGui::TextWrapped("Target (for your reference / to avoid touching the wrong challenge):");
        ImGui::InputText("Drops GUID", m_DropsGuid, sizeof(m_DropsGuid));
        ImGui::InputText("Challenge Id", m_ChallengeId, sizeof(m_ChallengeId));

        ImGui::Separator();
        int s_ModeSel = static_cast<int>(m_Mode);
        ImGui::RadioButton("Construct (from JSON)", &s_ModeSel, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Capture-replay (clone a real event)", &s_ModeSel, 1);
        m_Mode = static_cast<Mode>(s_ModeSel);

        if (m_Mode == Mode::Construct) {
            ImGui::InputTextMultiline("Event JSON", m_EventJson, sizeof(m_EventJson), ImVec2(-1.0f, 80.0f));
            if (ImGui::Button("Build & Verify (no inject)")) {
                // the engine parser must run on the game thread — queue it for the detour
                AcquireSRWLockExclusive(&m_Lock);
                m_VerifyJson = m_EventJson;
                m_VerifyPending = true;
                m_Status = "verify queued (runs on the next gameplay event)";
                ReleaseSRWLockExclusive(&m_Lock);
            }
        }
        else {
            ImGui::InputText("Target event Name", m_TargetName, sizeof(m_TargetName));
            ImGui::Text("Template: %s", s_HasTpl ? "captured" : (s_CapArmed ? "arming..." : "none"));
            if (ImGui::Button("Arm capture (then trigger the event once in-game)")) {
                AcquireSRWLockExclusive(&m_Lock);
                m_CaptureArmed = true;
                m_CaptureName = m_TargetName;
                m_Status = "capture armed";
                ReleaseSRWLockExclusive(&m_Lock);
            }
        }

        ImGui::InputInt("Repeat count", &m_RepeatCount);
        if (m_RepeatCount < 0)
            m_RepeatCount = 0;
        if (m_RepeatCount > 100)
            m_RepeatCount = 100;

        ImGui::Separator();
        ImGui::BeginDisabled(!s_Online);
        if (ImGui::Button("FORCE COMPLETE - emit events")) {
            // snapshot the config the game thread will consume (no torn reads of the ImGui buffers)
            AcquireSRWLockExclusive(&m_Lock);
            m_ArmedJson = m_EventJson;
            m_ArmedMode = m_Mode;
            m_CaptureName = m_TargetName;
            m_Done = 0;
            m_Pending = m_RepeatCount;
            m_Status = "armed: emitting on the next gameplay events...";
            ReleaseSRWLockExclusive(&m_Lock);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            AcquireSRWLockExclusive(&m_Lock);
            m_Pending = 0;
            m_Status = "cancelled";
            ReleaseSRWLockExclusive(&m_Lock);
        }

        ImGui::Separator();
        ImGui::Text("pending: %d   emitted: %d", s_Pending, s_Done);
        ImGui::TextWrapped("Status: %s", s_Status.c_str());
        ImGui::TextDisabled(
            "Emission rides real gameplay events: stay in active play until 'emitted' reaches your count."
        );
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();
}

DEFINE_ZHM_PLUGIN(ForceChallenge);
