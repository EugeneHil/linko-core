// Call state machine (design spec 4.2): the lifecycle of one call as a
// single pure, total function from (CallState, CallEvent) to Transition.
// The machine owns no clocks, sockets or randomness: timer expiries arrive
// as events, and message sends, CallKit reports and ICE restarts leave as
// effects for the host to execute. The same machine drives the apps and the
// server's call registry.
#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace linko {

// Timeout budget per phase, in milliseconds (design spec 4.2): a call may
// ring for 45 s, ICE has 20 s to connect after accept, and a dropped call
// has 15 s to reconnect before it fails.
inline constexpr int kRingTimeoutMs = 45000;
inline constexpr int kConnectTimeoutMs = 20000;
inline constexpr int kReconnectTimeoutMs = 15000;

// Lifecycle states of one call. Ended is terminal: the host starts a fresh
// machine in Idle for the next call instead of reviving an ended one.
enum class CallStateKind : std::uint8_t {
    Idle,
    OutgoingRinging,
    IncomingRinging,
    Connecting,
    Active,
    Reconnecting,
    Ended,
};

// Why a call ended; meaningful only while CallStateKind is Ended.
enum class EndReason : std::uint8_t {
    Normal,
    Busy,
    Rejected,
    Cancelled,
    Missed,
    Timeout,
    Error,
};

// Snapshot of one call. `reason` is meaningful only in Ended; `video`
// records the media mode the call was set up with.
struct CallState {
    CallStateKind kind = CallStateKind::Idle;
    std::string callId;
    std::string peer;
    bool video = false;
    EndReason reason = EndReason::Normal;

    auto operator==(const CallState &other) const -> bool {
        return std::tie(kind, callId, peer, video, reason) ==
               std::tie(other.kind, other.callId, other.peer, other.video,
                        other.reason);
    }
};

// One input to the machine: a local user action, a remote signaling message,
// an ICE connection state change or a timer expiry.
struct CallEvent {
    enum Kind : std::uint8_t {
        LocalInvite,
        LocalAccept,
        LocalReject,
        LocalCancel,
        LocalHangup,
        RemoteInvite,
        RemoteRinging,
        RemoteAccept,
        RemoteReject,
        RemoteBusy,
        RemoteCancel,
        RemoteOffer,
        RemoteAnswer,
        RemoteIce,
        RemoteIceRestart,
        RemoteHangup,
        IceConnected,
        IceFailed,
        NetworkChanged,
        TimerRingExpired,
        TimerConnectExpired,
        TimerReconnectExpired,
    };

    Kind kind = LocalInvite;
    // Call the event belongs to. An event whose callId is set and differs from
    // the state's callId is ignored; glare (OutgoingRinging + RemoteInvite) is
    // the single exception.
    std::string callId;
    // Peer username and media mode; read only for invite events.
    std::string peer;
    bool video = false;

    auto operator==(const CallEvent &other) const -> bool {
        return std::tie(kind, callId, peer, video) ==
               std::tie(other.kind, other.callId, other.peer, other.video);
    }
};

// One output for the host to execute; the effects of a Transition run in
// order. SendMessage carries the message-type constant (a linko::kType*
// value from messages.hpp) in `messageType`; StartTimer and CancelTimer
// carry which timer they address in `timeoutMs` (one of the k*TimeoutMs
// constants above); ReportEnded carries its EndReason in `reason`.
struct Effect {
    enum Kind : std::uint8_t {
        SendMessage,
        StartRinging,
        StopRinging,
        StartTimer,
        CancelTimer,
        ReportIncoming,
        ReportEnded,
        StartIceRestart,
    };

    Kind kind = SendMessage;
    std::string messageType;
    int timeoutMs = 0;
    EndReason reason = EndReason::Normal;

    auto operator==(const Effect &other) const -> bool {
        return std::tie(kind, messageType, timeoutMs, reason) ==
               std::tie(other.kind, other.messageType, other.timeoutMs,
                        other.reason);
    }
};

// The result of applyEvent: the next state plus the effects to execute.
struct Transition {
    CallState next;
    std::vector<Effect> effects;
};

// The transition table: applies `event` to `state` and returns the next
// state with the effects the host must run. Pure and total - no I/O, no
// clocks, no exceptions. Events a state does not list are ignored (the
// state is returned unchanged with no effects), as are events that address
// a different call; glare resolution (OutgoingRinging + RemoteInvite for
// another callId) is the one exception.
[[nodiscard]] auto applyEvent(const CallState &state, const CallEvent &event)
    -> Transition;

} // namespace linko
