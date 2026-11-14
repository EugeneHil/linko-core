// Protocol v1 typed message bodies (design spec 4.2).
//
// Every envelope family has typed structs: toBody() builds the JSON value for
// the envelope's "body" member, fromBody() validates such a value and reports
// every problem as a "body.<field>" JSON path appended to `errors`, returning
// std::nullopt when anything failed. Unknown body fields are ignored,
// mirroring the envelope parser. session.ping and session.pong carry empty
// bodies ("{}") and therefore have no struct.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "linko/envelope.hpp"
#include "linko/file_transfer.hpp"

namespace linko {

// Every protocol v1 message type name, one constant per type (design spec
// 4.2). Callers that build or inspect envelopes consume these; kKnownTypes
// below is built from this list, so the constants are the single source of
// truth for what the parser accepts.
inline constexpr std::string_view kTypeSessionHello = "session.hello";
inline constexpr std::string_view kTypeSessionPing = "session.ping";
inline constexpr std::string_view kTypeSessionPong = "session.pong";
inline constexpr std::string_view kTypeSessionError = "session.error";
inline constexpr std::string_view kTypePresenceChanged = "presence.changed";
inline constexpr std::string_view kTypeCallInvite = "call.invite";
inline constexpr std::string_view kTypeCallRinging = "call.ringing";
inline constexpr std::string_view kTypeCallAccept = "call.accept";
inline constexpr std::string_view kTypeCallReject = "call.reject";
inline constexpr std::string_view kTypeCallBusy = "call.busy";
inline constexpr std::string_view kTypeCallCancel = "call.cancel";
inline constexpr std::string_view kTypeCallOffer = "call.offer";
inline constexpr std::string_view kTypeCallAnswer = "call.answer";
inline constexpr std::string_view kTypeCallIce = "call.ice";
inline constexpr std::string_view kTypeCallIceRestart = "call.ice-restart";
inline constexpr std::string_view kTypeCallHangup = "call.hangup";
inline constexpr std::string_view kTypeMsgSend = "msg.send";
inline constexpr std::string_view kTypeMsgStored = "msg.stored";
inline constexpr std::string_view kTypeMsgDeliver = "msg.deliver";
inline constexpr std::string_view kTypeMsgAckDelivered = "msg.ack-delivered";
inline constexpr std::string_view kTypeMsgAckRead = "msg.ack-read";
inline constexpr std::string_view kTypeMsgSync = "msg.sync";
inline constexpr std::string_view kTypeFileOffer = "file.offer";
inline constexpr std::string_view kTypeFileAccept = "file.accept";
inline constexpr std::string_view kTypeFileCancel = "file.cancel";

// Message types the envelope parser accepts: one entry per type-name constant
// above, in family order.
inline constexpr std::array<std::string_view, 25> kKnownTypes{
    kTypeSessionHello, kTypeSessionPing,     kTypeSessionPong,
    kTypeSessionError, kTypePresenceChanged, kTypeCallInvite,
    kTypeCallRinging,  kTypeCallAccept,      kTypeCallReject,
    kTypeCallBusy,     kTypeCallCancel,      kTypeCallOffer,
    kTypeCallAnswer,   kTypeCallIce,         kTypeCallIceRestart,
    kTypeCallHangup,   kTypeMsgSend,         kTypeMsgStored,
    kTypeMsgDeliver,   kTypeMsgAckDelivered, kTypeMsgAckRead,
    kTypeMsgSync,      kTypeFileOffer,       kTypeFileAccept,
    kTypeFileCancel,
};

// --- session family ---

// Body of session.hello.
struct SessionHello {
    std::string user;
    std::string device;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<SessionHello>;
    auto operator==(const SessionHello &other) const -> bool {
        return std::tie(user, device) == std::tie(other.user, other.device);
    }
};

// Body of session.error.
struct SessionError {
    std::string code;
    std::string detail;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<SessionError>;
    auto operator==(const SessionError &other) const -> bool {
        return std::tie(code, detail) == std::tie(other.code, other.detail);
    }
};

// --- presence family ---

// Body of presence.changed.
struct PresenceChanged {
    std::string user;
    bool online = false;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<PresenceChanged>;
    auto operator==(const PresenceChanged &other) const -> bool {
        return std::tie(user, online) == std::tie(other.user, other.online);
    }
};

// --- call family ---

// Body of call.invite. media is "audio" or "video".
struct CallInvite {
    std::string callId;
    std::string to;
    std::string media;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<CallInvite>;
    auto operator==(const CallInvite &other) const -> bool {
        return std::tie(callId, to, media) ==
               std::tie(other.callId, other.to, other.media);
    }
};

// Body of call.ringing, call.accept and call.ice-restart.
struct CallRef {
    std::string callId;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<CallRef>;
    auto operator==(const CallRef &other) const -> bool {
        return callId == other.callId;
    }
};

// Body of call.offer and call.answer. sdp is rejected beyond kMaxSdpBytes.
struct CallSdp {
    std::string callId;
    std::string sdp;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<CallSdp>;
    auto operator==(const CallSdp &other) const -> bool {
        return std::tie(callId, sdp) == std::tie(other.callId, other.sdp);
    }
};

// Body of call.ice.
struct CallIce {
    std::string callId;
    std::string candidate;
    std::string sdpMid;
    int sdpMLineIndex = 0;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<CallIce>;
    auto operator==(const CallIce &other) const -> bool {
        return std::tie(callId, candidate, sdpMid, sdpMLineIndex) ==
               std::tie(other.callId, other.candidate, other.sdpMid,
                        other.sdpMLineIndex);
    }
};

// Body of call.reject, call.cancel, call.hangup and call.busy; reason is
// optional and omitted from the body when disengaged.
struct CallEnd {
    std::string callId;
    std::optional<std::string> reason;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<CallEnd>;
    auto operator==(const CallEnd &other) const -> bool {
        return std::tie(callId, reason) == std::tie(other.callId, other.reason);
    }
};

// --- msg family ---

// File metadata for a transferred file; nested under "file" in msg.send and
// message records. transport is "p2p" or "server".
struct FileMeta {
    std::string name;
    std::int64_t size = 0;
    std::string sha256;
    std::string transport;
    std::string fileId;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<FileMeta>;
    auto operator==(const FileMeta &other) const -> bool {
        return std::tie(name, size, sha256, transport, fileId) ==
               std::tie(other.name, other.size, other.sha256, other.transport,
                        other.fileId);
    }
};

// A stored chat message; nested under "message" in msg.deliver bodies. file
// is optional and omitted from the body when disengaged.
struct MessageRecord {
    std::string id;
    std::string from;
    std::string to;
    std::string text;
    std::optional<FileMeta> file;
    std::int64_t seq = 0;
    std::int64_t ts = 0;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MessageRecord>;
    auto operator==(const MessageRecord &other) const -> bool {
        return std::tie(id, from, to, text, file, seq, ts) ==
               std::tie(other.id, other.from, other.to, other.text, other.file,
                        other.seq, other.ts);
    }
};

// Body of msg.send; file is optional.
struct MsgSend {
    std::string id;
    std::string to;
    std::string text;
    std::optional<FileMeta> file;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MsgSend>;
    auto operator==(const MsgSend &other) const -> bool {
        return std::tie(id, to, text, file) ==
               std::tie(other.id, other.to, other.text, other.file);
    }
};

// Body of msg.stored.
struct MsgStored {
    std::string id;
    std::int64_t seq = 0;
    std::int64_t ts = 0;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MsgStored>;
    auto operator==(const MsgStored &other) const -> bool {
        return std::tie(id, seq, ts) == std::tie(other.id, other.seq, other.ts);
    }
};

// Body of msg.deliver.
struct MsgDeliver {
    MessageRecord message;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MsgDeliver>;
    auto operator==(const MsgDeliver &other) const -> bool {
        return message == other.message;
    }
};

// Body of msg.ack-delivered and msg.ack-read.
struct MsgAck {
    std::string conversation;
    std::int64_t uptoSeq = 0;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MsgAck>;
    auto operator==(const MsgAck &other) const -> bool {
        return std::tie(conversation, uptoSeq) ==
               std::tie(other.conversation, other.uptoSeq);
    }
};

// Body of msg.sync.
struct MsgSync {
    std::string conversation;
    std::int64_t afterSeq = 0;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<MsgSync>;
    auto operator==(const MsgSync &other) const -> bool {
        return std::tie(conversation, afterSeq) ==
               std::tie(other.conversation, other.afterSeq);
    }
};

// --- file family (in-call transfers; metadata only, bytes go over the data
// channel) ---

// Body of file.offer: the call and file ids plus the transfer manifest
// (FileManifest from file_transfer.hpp). The manifest fields ride at the top
// level of the body - "name", "size", "sha256", "chunkSize", "chunkCount" -
// which is the same JSON shape file.offer has always had; only the C++
// layout changed when the inline fields became a FileManifest.
struct FileOffer {
    std::string callId;
    std::string fileId;
    FileManifest manifest;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<FileOffer>;
    auto operator==(const FileOffer &other) const -> bool {
        return std::tie(callId, fileId, manifest) ==
               std::tie(other.callId, other.fileId, other.manifest);
    }
};

// Body of file.accept.
struct FileAccept {
    std::string callId;
    std::string fileId;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<FileAccept>;
    auto operator==(const FileAccept &other) const -> bool {
        return std::tie(callId, fileId) == std::tie(other.callId, other.fileId);
    }
};

// Body of file.cancel.
struct FileCancel {
    std::string callId;
    std::string fileId;
    std::string reason;

    [[nodiscard]] auto toBody() const -> nlohmann::json;
    static auto fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
        -> std::optional<FileCancel>;
    auto operator==(const FileCancel &other) const -> bool {
        return std::tie(callId, fileId, reason) ==
               std::tie(other.callId, other.fileId, other.reason);
    }
};

} // namespace linko
