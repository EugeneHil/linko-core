// Call state machine transition table (design spec 4.2); see
// include/linko/call_state_machine.hpp for the contract. One handler per
// state, a switch over the event inside: anything a state does not list is
// ignored, and an event that names a different call is ignored everywhere
// except glare resolution in OutgoingRinging.
#include "linko/call_state_machine.hpp"

#include <utility>
#include <vector>

#include "linko/messages.hpp"

namespace linko {
namespace {

// --- effect factories ---

auto sendMessage(std::string_view messageType) -> Effect {
    Effect effect;
    effect.kind = Effect::SendMessage;
    effect.messageType = std::string(messageType);
    return effect;
}

auto startRinging() -> Effect {
    Effect effect;
    effect.kind = Effect::StartRinging;
    return effect;
}

auto stopRinging() -> Effect {
    Effect effect;
    effect.kind = Effect::StopRinging;
    return effect;
}

auto startTimer(int timeoutMs) -> Effect {
    Effect effect;
    effect.kind = Effect::StartTimer;
    effect.timeoutMs = timeoutMs;
    return effect;
}

auto cancelTimer(int timeoutMs) -> Effect {
    Effect effect;
    effect.kind = Effect::CancelTimer;
    effect.timeoutMs = timeoutMs;
    return effect;
}

auto reportIncoming() -> Effect {
    Effect effect;
    effect.kind = Effect::ReportIncoming;
    return effect;
}

auto reportEnded(EndReason reason) -> Effect {
    Effect effect;
    effect.kind = Effect::ReportEnded;
    effect.reason = reason;
    return effect;
}

auto startIceRestart() -> Effect {
    Effect effect;
    effect.kind = Effect::StartIceRestart;
    return effect;
}

// --- transition helpers ---

auto transition(CallState next, std::vector<Effect> effects) -> Transition {
    return Transition{std::move(next), std::move(effects)};
}

// The event does not apply: the state is returned unchanged, no effects.
auto ignored(const CallState &state) -> Transition {
    return Transition{state, {}};
}

// `state` with only the kind replaced (callId, peer and video stay).
auto withKind(const CallState &state, CallStateKind kind) -> CallState {
    CallState next = state;
    next.kind = kind;
    return next;
}

// `state` ended for `reason`.
auto withEnded(const CallState &state, EndReason reason) -> CallState {
    CallState next = state;
    next.kind = CallStateKind::Ended;
    next.reason = reason;
    return next;
}

// `event` addresses the state's call (or carries no callId at all, in which
// case it belongs to whatever call is current).
auto addressesThisCall(const CallState &state, const CallEvent &event) -> bool {
    return event.callId.empty() || event.callId == state.callId;
}

// --- glare resolution (design spec 4.2) ---
//
// Both sides invited at once. The lexicographically smaller callId survives;
// reached only from OutgoingRinging with a RemoteInvite for another call.

auto applyGlare(const CallState &state, const CallEvent &event) -> Transition {
    if (state.callId < event.callId) {
        // Our call survives; the peer learns we are busy with the winning call.
        return transition(state, {sendMessage(kTypeCallBusy)});
    }
    // Our call loses: withdraw it and take the peer's call as incoming.
    const CallState incoming{
        CallStateKind::IncomingRinging,
        event.callId,
        event.peer,
        event.video,
        EndReason::Normal,
    };
    return transition(incoming, {
                                    sendMessage(kTypeCallCancel),
                                    startRinging(),
                                    reportIncoming(),
                                    cancelTimer(kRingTimeoutMs),
                                    startTimer(kRingTimeoutMs),
                                });
}

// --- one handler per state ---

auto applyIdle(const CallState &state, const CallEvent &event) -> Transition {
    switch (event.kind) {
    case CallEvent::LocalInvite: {
        const CallState outgoing{
            CallStateKind::OutgoingRinging,
            event.callId,
            event.peer,
            event.video,
            EndReason::Normal,
        };
        return transition(outgoing, {
                                        sendMessage(kTypeCallInvite),
                                        startTimer(kRingTimeoutMs),
                                    });
    }
    case CallEvent::RemoteInvite: {
        const CallState incoming{
            CallStateKind::IncomingRinging,
            event.callId,
            event.peer,
            event.video,
            EndReason::Normal,
        };
        return transition(incoming, {
                                        startRinging(),
                                        reportIncoming(),
                                        startTimer(kRingTimeoutMs),
                                    });
    }
    default: {
        return ignored(state);
    }
    }
}

auto applyOutgoingRinging(const CallState &state, const CallEvent &event)
    -> Transition {
    if (!addressesThisCall(state, event)) {
        // A concurrent invite for another call is glare; anything else for
        // another call is not ours to touch.
        if (event.kind == CallEvent::RemoteInvite) {
            return applyGlare(state, event);
        }
        return ignored(state);
    }
    switch (event.kind) {
    case CallEvent::RemoteRinging: {
        // Progress only; the ring timer keeps running until accept or expiry.
        return ignored(state);
    }
    case CallEvent::RemoteAccept: {
        return transition(withKind(state, CallStateKind::Connecting),
                          {
                              sendMessage(kTypeCallAccept),
                              cancelTimer(kRingTimeoutMs),
                              startTimer(kConnectTimeoutMs),
                          });
    }
    case CallEvent::RemoteReject: {
        return transition(
            withEnded(state, EndReason::Rejected),
            {cancelTimer(kRingTimeoutMs), reportEnded(EndReason::Rejected)});
    }
    case CallEvent::RemoteBusy: {
        return transition(
            withEnded(state, EndReason::Busy),
            {cancelTimer(kRingTimeoutMs), reportEnded(EndReason::Busy)});
    }
    case CallEvent::LocalCancel: {
        return transition(withEnded(state, EndReason::Cancelled),
                          {sendMessage(kTypeCallCancel)});
    }
    case CallEvent::TimerRingExpired: {
        // The peer never picked up.
        return transition(withEnded(state, EndReason::Timeout),
                          {reportEnded(EndReason::Timeout)});
    }
    default: {
        return ignored(state);
    }
    }
}

auto applyIncomingRinging(const CallState &state, const CallEvent &event)
    -> Transition {
    if (!addressesThisCall(state, event)) {
        return ignored(state);
    }
    switch (event.kind) {
    case CallEvent::LocalAccept: {
        // Answering stops the ringtone; ICE gets the connect budget.
        return transition(withKind(state, CallStateKind::Connecting),
                          {
                              sendMessage(kTypeCallAccept),
                              stopRinging(),
                              cancelTimer(kRingTimeoutMs),
                              startTimer(kConnectTimeoutMs),
                          });
    }
    case CallEvent::LocalReject: {
        return transition(withEnded(state, EndReason::Rejected),
                          {
                              sendMessage(kTypeCallReject),
                              stopRinging(),
                              cancelTimer(kRingTimeoutMs),
                              reportEnded(EndReason::Rejected),
                          });
    }
    case CallEvent::RemoteCancel: {
        // The caller withdrew the call before we answered.
        return transition(withEnded(state, EndReason::Cancelled),
                          {
                              stopRinging(),
                              cancelTimer(kRingTimeoutMs),
                              reportEnded(EndReason::Cancelled),
                          });
    }
    case CallEvent::TimerRingExpired: {
        // Nobody answered in time; the call was missed.
        return transition(withEnded(state, EndReason::Missed),
                          {stopRinging(), reportEnded(EndReason::Missed)});
    }
    default: {
        return ignored(state);
    }
    }
}

auto applyConnecting(const CallState &state, const CallEvent &event)
    -> Transition {
    if (!addressesThisCall(state, event)) {
        return ignored(state);
    }
    switch (event.kind) {
    case CallEvent::IceConnected: {
        return transition(withKind(state, CallStateKind::Active),
                          {cancelTimer(kConnectTimeoutMs)});
    }
    case CallEvent::TimerConnectExpired: {
        return transition(withEnded(state, EndReason::Timeout),
                          {reportEnded(EndReason::Timeout)});
    }
    case CallEvent::RemoteHangup: {
        return transition(
            withEnded(state, EndReason::Normal),
            {cancelTimer(kConnectTimeoutMs), reportEnded(EndReason::Normal)});
    }
    case CallEvent::RemoteCancel: {
        return transition(withEnded(state, EndReason::Cancelled),
                          {
                              cancelTimer(kConnectTimeoutMs),
                              reportEnded(EndReason::Cancelled),
                          });
    }
    case CallEvent::LocalCancel: {
        return transition(
            withEnded(state, EndReason::Cancelled),
            {sendMessage(kTypeCallCancel), cancelTimer(kConnectTimeoutMs)});
    }
    default: {
        // Includes offer, answer and candidate traffic: SDP and candidates
        // feed the peer connection, not the machine.
        return ignored(state);
    }
    }
}

auto applyActive(const CallState &state, const CallEvent &event) -> Transition {
    if (!addressesThisCall(state, event)) {
        return ignored(state);
    }
    switch (event.kind) {
    case CallEvent::LocalHangup: {
        // Local intent; CallKit already knows, so no ReportEnded.
        return transition(withEnded(state, EndReason::Normal),
                          {sendMessage(kTypeCallHangup)});
    }
    case CallEvent::RemoteHangup: {
        return transition(withEnded(state, EndReason::Normal),
                          {reportEnded(EndReason::Normal)});
    }
    case CallEvent::IceFailed: {
        return transition(withKind(state, CallStateKind::Reconnecting),
                          {startTimer(kReconnectTimeoutMs), startIceRestart()});
    }
    case CallEvent::NetworkChanged:
    case CallEvent::RemoteIceRestart: {
        // Ride a network switch, or adopt the peer's restart with one of our
        // own; either way the call itself stays up.
        return transition(state, {startIceRestart()});
    }
    default: {
        return ignored(state);
    }
    }
}

auto applyReconnecting(const CallState &state, const CallEvent &event)
    -> Transition {
    if (!addressesThisCall(state, event)) {
        return ignored(state);
    }
    switch (event.kind) {
    case CallEvent::IceConnected: {
        return transition(withKind(state, CallStateKind::Active),
                          {cancelTimer(kReconnectTimeoutMs)});
    }
    case CallEvent::TimerReconnectExpired: {
        return transition(withEnded(state, EndReason::Error),
                          {reportEnded(EndReason::Error)});
    }
    case CallEvent::RemoteIceRestart: {
        // Adopt the peer's restart; the reconnect timer keeps running.
        return transition(state, {startIceRestart()});
    }
    case CallEvent::LocalHangup: {
        // The user gives up on reconnecting.
        return transition(
            withEnded(state, EndReason::Normal),
            {sendMessage(kTypeCallHangup), cancelTimer(kReconnectTimeoutMs)});
    }
    case CallEvent::RemoteHangup: {
        return transition(
            withEnded(state, EndReason::Normal),
            {cancelTimer(kReconnectTimeoutMs), reportEnded(EndReason::Normal)});
    }
    default: {
        return ignored(state);
    }
    }
}

} // namespace

auto applyEvent(const CallState &state, const CallEvent &event) -> Transition {
    switch (state.kind) {
    case CallStateKind::Idle: {
        return applyIdle(state, event);
    }
    case CallStateKind::OutgoingRinging: {
        return applyOutgoingRinging(state, event);
    }
    case CallStateKind::IncomingRinging: {
        return applyIncomingRinging(state, event);
    }
    case CallStateKind::Connecting: {
        return applyConnecting(state, event);
    }
    case CallStateKind::Active: {
        return applyActive(state, event);
    }
    case CallStateKind::Reconnecting: {
        return applyReconnecting(state, event);
    }
    case CallStateKind::Ended: {
        // Ended is terminal: every event is dropped.
        return ignored(state);
    }
    }
    return ignored(state);
}

} // namespace linko
