// GoogleTest suite for the call state machine (design spec 4.2). The test
// names are the contract from the task brief; keep them verbatim. The suite
// is registered as the `call_state` executable, so `ctest -R call_state`
// runs exactly these scenarios.
#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "linko/call_state_machine.hpp"
#include "linko/messages.hpp"

namespace {

using linko::CallEvent;
using linko::CallState;
using linko::CallStateKind;
using linko::Effect;
using linko::EndReason;
using linko::Transition;

// A state at `kind` for call `callId` with the sample peer and media mode.
auto at(CallStateKind kind, std::string callId) -> CallState {
    return CallState{kind, std::move(callId), "anna", true, EndReason::Normal};
}

// The state after that call ended for `reason`.
auto endedState(std::string callId, EndReason reason) -> CallState {
    return CallState{
        CallStateKind::Ended, std::move(callId), "anna", true, reason,
    };
}

// An event of `kind` for `callId`, carrying the sample peer and media mode
// (only invites read them).
auto on(CallEvent::Kind kind, std::string callId) -> CallEvent {
    return CallEvent{kind, std::move(callId), "anna", true};
}

// Expected-effect constructors, one per Effect::Kind that carries a payload.
auto send(std::string_view messageType) -> Effect {
    Effect effect;
    effect.kind = Effect::SendMessage;
    effect.messageType = std::string(messageType);
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

auto reasonName(EndReason reason) -> std::string_view {
    switch (reason) {
    case EndReason::Normal: {
        return "Normal";
    }
    case EndReason::Busy: {
        return "Busy";
    }
    case EndReason::Rejected: {
        return "Rejected";
    }
    case EndReason::Cancelled: {
        return "Cancelled";
    }
    case EndReason::Missed: {
        return "Missed";
    }
    case EndReason::Timeout: {
        return "Timeout";
    }
    case EndReason::Error: {
        return "Error";
    }
    }
    return "?";
}

// A human-readable one-liner for failure messages.
auto describe(const Effect &effect) -> std::string {
    switch (effect.kind) {
    case Effect::SendMessage: {
        return "SendMessage " + effect.messageType;
    }
    case Effect::StartRinging: {
        return "StartRinging";
    }
    case Effect::StopRinging: {
        return "StopRinging";
    }
    case Effect::StartTimer: {
        return "StartTimer " + std::to_string(effect.timeoutMs);
    }
    case Effect::CancelTimer: {
        return "CancelTimer " + std::to_string(effect.timeoutMs);
    }
    case Effect::ReportIncoming: {
        return "ReportIncoming";
    }
    case Effect::ReportEnded: {
        return "ReportEnded " + std::string(reasonName(effect.reason));
    }
    case Effect::StartIceRestart: {
        return "StartIceRestart";
    }
    }
    return "?";
}

// Asserts the transition lands exactly in `want` (all five state fields).
void expectState(const Transition &transition, const CallState &want) {
    EXPECT_EQ(transition.next.kind, want.kind);
    EXPECT_EQ(transition.next.callId, want.callId);
    EXPECT_EQ(transition.next.peer, want.peer);
    EXPECT_EQ(transition.next.video, want.video);
    EXPECT_EQ(transition.next.reason, want.reason);
}

// Asserts the transition produces exactly `want` effects, in order.
void expectEffects(const Transition &transition,
                   const std::vector<Effect> &want) {
    ASSERT_EQ(transition.effects.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        EXPECT_TRUE(transition.effects[i] == want[i])
            << "effect " << i << ": want \"" << describe(want[i])
            << "\", got \"" << describe(transition.effects[i]) << "\"";
    }
}

} // namespace

TEST(call_state, test_outgoing_happy_path) {
    const CallState idle;
    const CallEvent invite{CallEvent::LocalInvite, "c1", "anna", true};
    Transition transition = linko::applyEvent(idle, invite);
    expectState(transition, at(CallStateKind::OutgoingRinging, "c1"));
    expectEffects(transition, {
                                  send(linko::kTypeCallInvite),
                                  startTimer(linko::kRingTimeoutMs),
                              });

    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteRinging, "c1"));
    expectState(transition, at(CallStateKind::OutgoingRinging, "c1"));
    expectEffects(transition, {});

    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteAccept, "c1"));
    expectState(transition, at(CallStateKind::Connecting, "c1"));
    expectEffects(transition, {
                                  send(linko::kTypeCallAccept),
                                  cancelTimer(linko::kRingTimeoutMs),
                                  startTimer(linko::kConnectTimeoutMs),
                              });

    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteAnswer, "c1"));
    expectState(transition, at(CallStateKind::Connecting, "c1"));
    expectEffects(transition, {});

    transition =
        linko::applyEvent(transition.next, on(CallEvent::IceConnected, "c1"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {cancelTimer(linko::kConnectTimeoutMs)});
}

TEST(call_state, test_incoming_happy_path) {
    const CallState idle;
    const CallEvent invite{CallEvent::RemoteInvite, "c2", "anna", true};
    Transition transition = linko::applyEvent(idle, invite);
    expectState(transition, at(CallStateKind::IncomingRinging, "c2"));
    expectEffects(transition, {
                                  startRinging(),
                                  reportIncoming(),
                                  startTimer(linko::kRingTimeoutMs),
                              });

    // Accepting stops the ringtone here; ICE still has the connect budget.
    transition =
        linko::applyEvent(transition.next, on(CallEvent::LocalAccept, "c2"));
    expectState(transition, at(CallStateKind::Connecting, "c2"));
    expectEffects(transition, {
                                  send(linko::kTypeCallAccept),
                                  stopRinging(),
                                  cancelTimer(linko::kRingTimeoutMs),
                                  startTimer(linko::kConnectTimeoutMs),
                              });

    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteOffer, "c2"));
    expectState(transition, at(CallStateKind::Connecting, "c2"));
    expectEffects(transition, {});

    transition =
        linko::applyEvent(transition.next, on(CallEvent::IceConnected, "c2"));
    expectState(transition, at(CallStateKind::Active, "c2"));
    expectEffects(transition, {cancelTimer(linko::kConnectTimeoutMs)});
}

TEST(call_state, test_remote_reject_ends) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::RemoteReject, "c1"));
    expectState(transition, endedState("c1", EndReason::Rejected));
    expectEffects(transition, {
                                  cancelTimer(linko::kRingTimeoutMs),
                                  reportEnded(EndReason::Rejected),
                              });
}

TEST(call_state, test_remote_busy_ends) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::RemoteBusy, "c1"));
    expectState(transition, endedState("c1", EndReason::Busy));
    expectEffects(transition, {
                                  cancelTimer(linko::kRingTimeoutMs),
                                  reportEnded(EndReason::Busy),
                              });
}

TEST(call_state, test_cancel_while_ringing) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::LocalCancel, "c1"));
    expectState(transition, endedState("c1", EndReason::Cancelled));
    expectEffects(transition, {send(linko::kTypeCallCancel)});
}

TEST(call_state, test_ring_timeout_missed) {
    // Incoming side: the callee let it ring for 45 s, so the call was missed.
    Transition transition =
        linko::applyEvent(at(CallStateKind::IncomingRinging, "c2"),
                          on(CallEvent::TimerRingExpired, "c2"));
    expectState(transition, endedState("c2", EndReason::Missed));
    expectEffects(transition, {stopRinging(), reportEnded(EndReason::Missed)});

    // Outgoing side: the peer never picked up, so the call times out.
    transition = linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                                   on(CallEvent::TimerRingExpired, "c1"));
    expectState(transition, endedState("c1", EndReason::Timeout));
    expectEffects(transition, {reportEnded(EndReason::Timeout)});
}

TEST(call_state, test_connect_timeout) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Connecting, "c1"),
                          on(CallEvent::TimerConnectExpired, "c1"));
    expectState(transition, endedState("c1", EndReason::Timeout));
    expectEffects(transition, {reportEnded(EndReason::Timeout)});
}

TEST(call_state, test_local_hangup_active) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Active, "c1"), on(CallEvent::LocalHangup, "c1"));
    expectState(transition, endedState("c1", EndReason::Normal));
    expectEffects(transition, {send(linko::kTypeCallHangup)});
}

TEST(call_state, test_remote_hangup_active) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Active, "c1"), on(CallEvent::RemoteHangup, "c1"));
    expectState(transition, endedState("c1", EndReason::Normal));
    expectEffects(transition, {reportEnded(EndReason::Normal)});
}

TEST(call_state, test_ice_failed_active_starts_reconnect) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Active, "c1"), on(CallEvent::IceFailed, "c1"));
    expectState(transition, at(CallStateKind::Reconnecting, "c1"));
    expectEffects(transition,
                  {startTimer(linko::kReconnectTimeoutMs), startIceRestart()});
}

TEST(call_state, test_network_change_triggers_ice_restart) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Active, "c1"), on(CallEvent::NetworkChanged, "c1"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {startIceRestart()});
}

TEST(call_state, test_reconnect_timeout_ends_error) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                          on(CallEvent::TimerReconnectExpired, "c1"));
    expectState(transition, endedState("c1", EndReason::Error));
    expectEffects(transition, {reportEnded(EndReason::Error)});
}

TEST(call_state, test_ice_restarts_use_remote_ice_restart) {
    // A remote ICE restart is adopted by restarting our own ICE agents; the
    // call state itself is unaffected.
    Transition transition = linko::applyEvent(
        at(CallStateKind::Active, "c1"), on(CallEvent::RemoteIceRestart, "c1"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {startIceRestart()});

    // A plain candidate must not trigger a restart.
    transition = linko::applyEvent(at(CallStateKind::Active, "c1"),
                                   on(CallEvent::RemoteIce, "c1"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {});
}

TEST(call_state, test_ignores_duplicate_accept) {
    const CallState idle;
    const CallEvent invite{CallEvent::LocalInvite, "c1", "anna", true};
    Transition transition = linko::applyEvent(idle, invite);
    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteAccept, "c1"));
    ASSERT_EQ(transition.next.kind, CallStateKind::Connecting);

    // A second call.accept for the same call changes nothing.
    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteAccept, "c1"));
    expectState(transition, at(CallStateKind::Connecting, "c1"));
    expectEffects(transition, {});
}

TEST(call_state, test_ignores_events_for_other_call) {
    Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::RemoteReject, "c9"));
    expectState(transition, at(CallStateKind::OutgoingRinging, "c1"));
    expectEffects(transition, {});

    transition = linko::applyEvent(at(CallStateKind::Active, "c1"),
                                   on(CallEvent::RemoteHangup, "c9"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {});
}

TEST(call_state, test_ignores_late_answer_after_ended) {
    Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::RemoteReject, "c1"));
    ASSERT_EQ(transition.next.kind, CallStateKind::Ended);

    // A call.answer arriving after the call ended is dropped.
    transition =
        linko::applyEvent(transition.next, on(CallEvent::RemoteAnswer, "c1"));
    expectState(transition, endedState("c1", EndReason::Rejected));
    expectEffects(transition, {});
}

TEST(call_state, test_no_effects_after_ended) {
    const CallState ended = endedState("c1", EndReason::Normal);
    const CallEvent localHangup = on(CallEvent::LocalHangup, "c1");
    const CallEvent remoteInviteOtherCall =
        CallEvent{CallEvent::RemoteInvite, "c9", "bea", false};
    const CallEvent iceConnected = on(CallEvent::IceConnected, "c1");
    const CallEvent timerRingExpired = on(CallEvent::TimerRingExpired, "c1");
    for (const CallEvent *event : {
             &localHangup,
             &remoteInviteOtherCall,
             &iceConnected,
             &timerRingExpired,
         }) {
        const Transition transition = linko::applyEvent(ended, *event);
        expectState(transition, ended);
        expectEffects(transition, {});
    }
}

TEST(call_state, test_idle_stays_idle_on_stray_events) {
    const CallState idle;
    const CallEvent remoteRinging = on(CallEvent::RemoteRinging, "c1");
    const CallEvent remoteAccept = on(CallEvent::RemoteAccept, "c1");
    const CallEvent remoteAnswer = on(CallEvent::RemoteAnswer, "c1");
    const CallEvent iceConnected = on(CallEvent::IceConnected, "c1");
    const CallEvent timerRingExpired = on(CallEvent::TimerRingExpired, "c1");
    const CallEvent localHangup = on(CallEvent::LocalHangup, "c1");
    for (const CallEvent *event : {
             &remoteRinging,
             &remoteAccept,
             &remoteAnswer,
             &iceConnected,
             &timerRingExpired,
             &localHangup,
         }) {
        const Transition transition = linko::applyEvent(idle, *event);
        expectState(transition, idle);
        expectEffects(transition, {});
    }
}

TEST(call_state, test_glare_smaller_call_id_survives) {
    // Both sides invited at once; our callId "aaa" is lexicographically
    // smaller than the peer's "bbb", so ours survives and they learn we are
    // busy with the winning call.
    const Transition transition = linko::applyEvent(
        at(CallStateKind::OutgoingRinging, "aaa"),
        CallEvent{CallEvent::RemoteInvite, "bbb", "anna", true});
    expectState(transition, at(CallStateKind::OutgoingRinging, "aaa"));
    expectEffects(transition, {send(linko::kTypeCallBusy)});
}

TEST(call_state, test_glare_larger_call_id_yields) {
    // Our callId "bbb" is larger than the peer's "aaa", so we withdraw our
    // outgoing call and take theirs as an incoming one.
    const Transition transition = linko::applyEvent(
        at(CallStateKind::OutgoingRinging, "bbb"),
        CallEvent{CallEvent::RemoteInvite, "aaa", "anna", true});
    expectState(transition, at(CallStateKind::IncomingRinging, "aaa"));
    expectEffects(transition, {
                                  send(linko::kTypeCallCancel),
                                  startRinging(),
                                  reportIncoming(),
                                  cancelTimer(linko::kRingTimeoutMs),
                                  startTimer(linko::kRingTimeoutMs),
                              });
}

// Supplemental scenario (not in the brief's list, added to round out the
// mandated count): the callee rejects the incoming call, which sends
// call.reject and reports the ended call.
TEST(call_state, test_local_reject_incoming_ends) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::IncomingRinging, "c2"),
                          on(CallEvent::LocalReject, "c2"));
    expectState(transition, endedState("c2", EndReason::Rejected));
    expectEffects(transition, {
                                  send(linko::kTypeCallReject),
                                  stopRinging(),
                                  cancelTimer(linko::kRingTimeoutMs),
                                  reportEnded(EndReason::Rejected),
                              });
}

// Late signaling: the peer withdraws or hangs up while ICE is still
// connecting. The connect timer is cancelled in every variant and the call
// ends; a local cancel additionally sends call.cancel so the peer learns.
TEST(call_state, test_connecting_remote_hangup_ends) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Connecting, "c1"), on(CallEvent::RemoteHangup, "c1"));
    expectState(transition, endedState("c1", EndReason::Normal));
    expectEffects(transition, {
                                  cancelTimer(linko::kConnectTimeoutMs),
                                  reportEnded(EndReason::Normal),
                              });
}

TEST(call_state, test_connecting_remote_cancel_ends) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Connecting, "c1"), on(CallEvent::RemoteCancel, "c1"));
    expectState(transition, endedState("c1", EndReason::Cancelled));
    expectEffects(transition, {
                                  cancelTimer(linko::kConnectTimeoutMs),
                                  reportEnded(EndReason::Cancelled),
                              });
}

TEST(call_state, test_connecting_local_cancel_ends) {
    const Transition transition = linko::applyEvent(
        at(CallStateKind::Connecting, "c1"), on(CallEvent::LocalCancel, "c1"));
    expectState(transition, endedState("c1", EndReason::Cancelled));
    expectEffects(transition, {
                                  send(linko::kTypeCallCancel),
                                  cancelTimer(linko::kConnectTimeoutMs),
                              });
}

// The caller withdrew the call while it was still ringing on our side: the
// ringtone stops, the ring timer is cancelled and the call reports ended.
TEST(call_state, test_incoming_remote_cancel_while_ringing) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::IncomingRinging, "c2"),
                          on(CallEvent::RemoteCancel, "c2"));
    expectState(transition, endedState("c2", EndReason::Cancelled));
    expectEffects(transition, {
                                  stopRinging(),
                                  cancelTimer(linko::kRingTimeoutMs),
                                  reportEnded(EndReason::Cancelled),
                              });
}

// Reconnecting: ICE came back before the reconnect budget ran out, so the
// call resumes exactly where it was with only the timer cancelled.
TEST(call_state, test_reconnecting_ice_connected_resumes) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                          on(CallEvent::IceConnected, "c1"));
    expectState(transition, at(CallStateKind::Active, "c1"));
    expectEffects(transition, {cancelTimer(linko::kReconnectTimeoutMs)});
}

// The user gives up while reconnecting: call.hangup goes out (the peer may
// still be connected) and the reconnect timer is cancelled.
TEST(call_state, test_reconnecting_local_hangup_ends) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                          on(CallEvent::LocalHangup, "c1"));
    expectState(transition, endedState("c1", EndReason::Normal));
    expectEffects(transition, {
                                  send(linko::kTypeCallHangup),
                                  cancelTimer(linko::kReconnectTimeoutMs),
                              });
}

TEST(call_state, test_reconnecting_remote_hangup_ends) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                          on(CallEvent::RemoteHangup, "c1"));
    expectState(transition, endedState("c1", EndReason::Normal));
    expectEffects(transition, {
                                  cancelTimer(linko::kReconnectTimeoutMs),
                                  reportEnded(EndReason::Normal),
                              });
}

// A remote ICE restart arriving while we are already reconnecting is adopted
// with one restart of our own; the reconnect timer keeps running.
TEST(call_state, test_reconnecting_adopts_remote_ice_restart) {
    const Transition transition =
        linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                          on(CallEvent::RemoteIceRestart, "c1"));
    expectState(transition, at(CallStateKind::Reconnecting, "c1"));
    expectEffects(transition, {startIceRestart()});
}

// Conservative-ignore semantics: events a live state does not list are
// dropped without effects, in every live state (not just Idle/Connecting).
TEST(call_state, test_ignores_stray_events_in_live_states) {
    Transition transition =
        linko::applyEvent(at(CallStateKind::OutgoingRinging, "c1"),
                          on(CallEvent::RemoteHangup, "c1"));
    expectState(transition, at(CallStateKind::OutgoingRinging, "c1"));
    expectEffects(transition, {});

    transition = linko::applyEvent(at(CallStateKind::IncomingRinging, "c2"),
                                   on(CallEvent::RemoteHangup, "c2"));
    expectState(transition, at(CallStateKind::IncomingRinging, "c2"));
    expectEffects(transition, {});

    transition = linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                                   on(CallEvent::RemoteOffer, "c1"));
    expectState(transition, at(CallStateKind::Reconnecting, "c1"));
    expectEffects(transition, {});
}

// Events naming another call are ignored in the remaining live states too
// (OutgoingRinging's glare handling is the one exception, pinned above).
TEST(call_state, test_ignores_other_call_events_in_live_states) {
    Transition transition =
        linko::applyEvent(at(CallStateKind::IncomingRinging, "c2"),
                          on(CallEvent::RemoteReject, "c9"));
    expectState(transition, at(CallStateKind::IncomingRinging, "c2"));
    expectEffects(transition, {});

    transition = linko::applyEvent(at(CallStateKind::Connecting, "c1"),
                                   on(CallEvent::IceConnected, "c9"));
    expectState(transition, at(CallStateKind::Connecting, "c1"));
    expectEffects(transition, {});

    transition = linko::applyEvent(at(CallStateKind::Reconnecting, "c1"),
                                   on(CallEvent::IceConnected, "c9"));
    expectState(transition, at(CallStateKind::Reconnecting, "c1"));
    expectEffects(transition, {});
}
