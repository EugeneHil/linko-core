// Typed message bodies for protocol v1 (design spec 4.2); see
// include/linko/messages.hpp for the contract. Every fromBody is a pure
// mapping function that collects all validation errors, never throwing.
#include "linko/messages.hpp"

#include <initializer_list>
#include <limits>
#include <utility>

namespace linko {
namespace {

// Full JSON path of `key` inside the object at `path` ("body" + "sdp" ->
// "body.sdp").
auto fieldPath(const std::string &path, const char *key) -> std::string {
    return path + "." + key;
}

// The object at `path` must be a JSON object.
auto requireObject(const nlohmann::json &object, const std::string &path,
                   std::vector<ValidationError> &errors) -> bool {
    if (object.is_object()) {
        return true;
    }
    errors.push_back({path, "must be an object"});
    return false;
}

// Required string field; missing or non-string fields append an error and
// leave the result disengaged.
auto requireString(const nlohmann::json &object, const char *key,
                   const std::string &path,
                   std::vector<ValidationError> &errors)
    -> std::optional<std::string> {
    const auto field = object.find(key);
    if (field == object.end()) {
        errors.push_back({fieldPath(path, key), "missing"});
        return std::nullopt;
    }
    if (!field->is_string()) {
        errors.push_back({fieldPath(path, key), "must be a string"});
        return std::nullopt;
    }
    return field->get<std::string>();
}

// Required identifier string (callId, fileId, conversation, message id):
// like requireString, but an empty value is rejected too - an empty
// identifier is hostile input, not an identifier.
auto requireNonEmptyString(const nlohmann::json &object, const char *key,
                           const std::string &path,
                           std::vector<ValidationError> &errors)
    -> std::optional<std::string> {
    auto value = requireString(object, key, path, errors);
    if (value.has_value() && value->empty()) {
        errors.push_back({fieldPath(path, key), "must not be empty"});
        return std::nullopt;
    }
    return value;
}

// Required string field whose value must be one of `allowed`; a present but
// unknown value is reported as "invalid".
auto requireStringIn(const nlohmann::json &object, const char *key,
                     const std::string &path,
                     std::initializer_list<std::string_view> allowed,
                     std::vector<ValidationError> &errors)
    -> std::optional<std::string> {
    auto value = requireString(object, key, path, errors);
    if (!value.has_value()) {
        return std::nullopt;
    }
    for (const auto candidate : allowed) {
        if (*value == candidate) {
            return value;
        }
    }
    errors.push_back({fieldPath(path, key), "invalid"});
    return std::nullopt;
}

// Required integer field. Unsigned JSON numbers above INT64_MAX are
// rejected: nlohmann's get<int64_t> would silently wrap them.
auto requireInt64(const nlohmann::json &object, const char *key,
                  const std::string &path, std::vector<ValidationError> &errors)
    -> std::optional<std::int64_t> {
    const auto field = object.find(key);
    if (field == object.end()) {
        errors.push_back({fieldPath(path, key), "missing"});
        return std::nullopt;
    }
    if (!field->is_number_integer()) {
        errors.push_back({fieldPath(path, key), "must be an integer"});
        return std::nullopt;
    }
    if (field->is_number_unsigned() &&
        field->get<std::uint64_t>() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        errors.push_back({fieldPath(path, key), "out of range"});
        return std::nullopt;
    }
    return field->get<std::int64_t>();
}

// Required boolean field.
auto requireBool(const nlohmann::json &object, const char *key,
                 const std::string &path, std::vector<ValidationError> &errors)
    -> std::optional<bool> {
    const auto field = object.find(key);
    if (field == object.end()) {
        errors.push_back({fieldPath(path, key), "missing"});
        return std::nullopt;
    }
    if (!field->is_boolean()) {
        errors.push_back({fieldPath(path, key), "must be a boolean"});
        return std::nullopt;
    }
    return field->get<bool>();
}

// Optional string field: the outer optional is disengaged only when an error
// was appended; the inner optional is disengaged when the field is absent or
// null, which is a valid value.
auto optionalString(const nlohmann::json &object, const char *key,
                    const std::string &path,
                    std::vector<ValidationError> &errors)
    -> std::optional<std::optional<std::string>> {
    const auto field = object.find(key);
    if (field == object.end() || field->is_null()) {
        return std::optional<std::string>{};
    }
    if (!field->is_string()) {
        errors.push_back({fieldPath(path, key), "must be a string"});
        return std::nullopt;
    }
    return std::optional<std::string>{field->get<std::string>()};
}

// A FileMeta parsed from `object`, whose location in the envelope is `path`.
auto fileMetaFromJson(const nlohmann::json &object, const std::string &path,
                      std::vector<ValidationError> &errors)
    -> std::optional<FileMeta> {
    if (!requireObject(object, path, errors)) {
        return std::nullopt;
    }
    const auto name = requireString(object, "name", path, errors);
    const auto size = requireInt64(object, "size", path, errors);
    const auto sha256 = requireString(object, "sha256", path, errors);
    const auto transport =
        requireStringIn(object, "transport", path, {"p2p", "server"}, errors);
    const auto fileId = requireNonEmptyString(object, "fileId", path, errors);
    if (!name.has_value() || !size.has_value() || !sha256.has_value() ||
        !transport.has_value() || !fileId.has_value()) {
        return std::nullopt;
    }
    return FileMeta{*name, *size, *sha256, *transport, *fileId};
}

// The optional nested "file" object of msg.send / message records: absent or
// null means "no file". Returns false when the field exists but is invalid
// (errors already appended), leaving `out` disengaged.
auto fileFieldFromJson(const nlohmann::json &object, const std::string &path,
                       std::vector<ValidationError> &errors,
                       std::optional<FileMeta> &out) -> bool {
    out.reset();
    const auto field = object.find("file");
    if (field == object.end() || field->is_null()) {
        return true;
    }
    if (!requireObject(*field, path + ".file", errors)) {
        return false;
    }
    out = fileMetaFromJson(*field, path + ".file", errors);
    return out.has_value();
}

// A MessageRecord parsed from `object`, whose location in the envelope is
// `path`.
auto messageRecordFromJson(const nlohmann::json &object,
                           const std::string &path,
                           std::vector<ValidationError> &errors)
    -> std::optional<MessageRecord> {
    if (!requireObject(object, path, errors)) {
        return std::nullopt;
    }
    const auto recordId = requireNonEmptyString(object, "id", path, errors);
    const auto from = requireString(object, "from", path, errors);
    const auto toUser = requireString(object, "to", path, errors);
    const auto text = requireString(object, "text", path, errors);
    std::optional<FileMeta> file;
    const bool fileOk = fileFieldFromJson(object, path, errors, file);
    const auto seq = requireInt64(object, "seq", path, errors);
    const auto timestamp = requireInt64(object, "ts", path, errors);
    if (!recordId.has_value() || !from.has_value() || !toUser.has_value() ||
        !text.has_value() || !fileOk || !seq.has_value() ||
        !timestamp.has_value()) {
        return std::nullopt;
    }
    return MessageRecord{
        *recordId, *from, *toUser, *text, std::move(file), *seq, *timestamp,
    };
}

} // namespace

// --- session family ---

auto SessionHello::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"user", user},
        {"device", device},
    };
}

auto SessionHello::fromBody(const nlohmann::json &body,
                            std::vector<ValidationError> &errors)
    -> std::optional<SessionHello> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto user = requireString(body, "user", "body", errors);
    const auto device = requireString(body, "device", "body", errors);
    if (!user.has_value() || !device.has_value()) {
        return std::nullopt;
    }
    return SessionHello{*user, *device};
}

auto SessionError::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"code", code},
        {"detail", detail},
    };
}

auto SessionError::fromBody(const nlohmann::json &body,
                            std::vector<ValidationError> &errors)
    -> std::optional<SessionError> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto code = requireString(body, "code", "body", errors);
    const auto detail = requireString(body, "detail", "body", errors);
    if (!code.has_value() || !detail.has_value()) {
        return std::nullopt;
    }
    return SessionError{*code, *detail};
}

// --- presence family ---

auto PresenceChanged::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"user", user},
        {"online", online},
    };
}

auto PresenceChanged::fromBody(const nlohmann::json &body,
                               std::vector<ValidationError> &errors)
    -> std::optional<PresenceChanged> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto user = requireString(body, "user", "body", errors);
    const auto online = requireBool(body, "online", "body", errors);
    if (!user.has_value() || !online.has_value()) {
        return std::nullopt;
    }
    return PresenceChanged{*user, *online};
}

// --- call family ---

auto CallInvite::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"to", to},
        {"media", media},
    };
}

auto CallInvite::fromBody(const nlohmann::json &body,
                          std::vector<ValidationError> &errors)
    -> std::optional<CallInvite> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto toUser = requireString(body, "to", "body", errors);
    const auto media =
        requireStringIn(body, "media", "body", {"audio", "video"}, errors);
    if (!callId.has_value() || !toUser.has_value() || !media.has_value()) {
        return std::nullopt;
    }
    return CallInvite{*callId, *toUser, *media};
}

auto CallRef::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
    };
}

auto CallRef::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<CallRef> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    if (!callId.has_value()) {
        return std::nullopt;
    }
    return CallRef{*callId};
}

auto CallSdp::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"sdp", sdp},
    };
}

auto CallSdp::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<CallSdp> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    auto sdp = requireString(body, "sdp", "body", errors);
    if (sdp.has_value() && sdp->size() > kMaxSdpBytes) {
        errors.push_back({"body.sdp", "too large"});
        sdp.reset();
    }
    if (!callId.has_value() || !sdp.has_value()) {
        return std::nullopt;
    }
    return CallSdp{*callId, *sdp};
}

auto CallIce::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"candidate", candidate},
        {"sdpMid", sdpMid},
        {"sdpMLineIndex", sdpMLineIndex},
    };
}

auto CallIce::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<CallIce> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto candidate = requireString(body, "candidate", "body", errors);
    const auto sdpMid = requireString(body, "sdpMid", "body", errors);
    auto sdpMLineIndex = requireInt64(body, "sdpMLineIndex", "body", errors);
    if (sdpMLineIndex.has_value() &&
        (*sdpMLineIndex < 0 ||
         *sdpMLineIndex > std::numeric_limits<int>::max())) {
        errors.push_back({"body.sdpMLineIndex", "out of range"});
        sdpMLineIndex.reset();
    }
    if (!callId.has_value() || !candidate.has_value() || !sdpMid.has_value() ||
        !sdpMLineIndex.has_value()) {
        return std::nullopt;
    }
    return CallIce{
        *callId,
        *candidate,
        *sdpMid,
        static_cast<int>(*sdpMLineIndex),
    };
}

auto CallEnd::toBody() const -> nlohmann::json {
    nlohmann::json body{
        {"callId", callId},
    };
    if (reason.has_value()) {
        body["reason"] = *reason;
    }
    return body;
}

auto CallEnd::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<CallEnd> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto reason = optionalString(body, "reason", "body", errors);
    if (!callId.has_value() || !reason.has_value()) {
        return std::nullopt;
    }
    // *reason is itself optional: disengaged means "no reason given".
    return CallEnd{*callId, *reason};
}

// --- msg family ---

auto FileMeta::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"name", name},           {"size", size},     {"sha256", sha256},
        {"transport", transport}, {"fileId", fileId},
    };
}

auto FileMeta::fromBody(const nlohmann::json &body,
                        std::vector<ValidationError> &errors)
    -> std::optional<FileMeta> {
    return fileMetaFromJson(body, "body", errors);
}

auto MessageRecord::toBody() const -> nlohmann::json {
    nlohmann::json body;
    body["id"] = id;
    body["from"] = from;
    body["to"] = to;
    body["text"] = text;
    if (file.has_value()) {
        body["file"] = file->toBody();
    }
    body["seq"] = seq;
    body["ts"] = ts;
    return body;
}

auto MessageRecord::fromBody(const nlohmann::json &body,
                             std::vector<ValidationError> &errors)
    -> std::optional<MessageRecord> {
    return messageRecordFromJson(body, "body", errors);
}

auto MsgSend::toBody() const -> nlohmann::json {
    nlohmann::json body;
    body["id"] = id;
    body["to"] = to;
    body["text"] = text;
    if (file.has_value()) {
        body["file"] = file->toBody();
    }
    return body;
}

auto MsgSend::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<MsgSend> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto msgId = requireNonEmptyString(body, "id", "body", errors);
    const auto toUser = requireString(body, "to", "body", errors);
    const auto text = requireString(body, "text", "body", errors);
    std::optional<FileMeta> file;
    const bool fileOk = fileFieldFromJson(body, "body", errors, file);
    if (!msgId.has_value() || !toUser.has_value() || !text.has_value() ||
        !fileOk) {
        return std::nullopt;
    }
    return MsgSend{*msgId, *toUser, *text, std::move(file)};
}

auto MsgStored::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"id", id},
        {"seq", seq},
        {"ts", ts},
    };
}

auto MsgStored::fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
    -> std::optional<MsgStored> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto msgId = requireNonEmptyString(body, "id", "body", errors);
    const auto seq = requireInt64(body, "seq", "body", errors);
    const auto timestamp = requireInt64(body, "ts", "body", errors);
    if (!msgId.has_value() || !seq.has_value() || !timestamp.has_value()) {
        return std::nullopt;
    }
    return MsgStored{*msgId, *seq, *timestamp};
}

auto MsgDeliver::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"message", message.toBody()},
    };
}

auto MsgDeliver::fromBody(const nlohmann::json &body,
                          std::vector<ValidationError> &errors)
    -> std::optional<MsgDeliver> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto messageField = body.find("message");
    if (messageField == body.end()) {
        errors.push_back({"body.message", "missing"});
        return std::nullopt;
    }
    const auto message =
        messageRecordFromJson(*messageField, "body.message", errors);
    if (!message.has_value()) {
        return std::nullopt;
    }
    return MsgDeliver{*message};
}

auto MsgAck::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"conversation", conversation},
        {"uptoSeq", uptoSeq},
    };
}

auto MsgAck::fromBody(const nlohmann::json &body,
                      std::vector<ValidationError> &errors)
    -> std::optional<MsgAck> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto conversation =
        requireNonEmptyString(body, "conversation", "body", errors);
    const auto uptoSeq = requireInt64(body, "uptoSeq", "body", errors);
    if (!conversation.has_value() || !uptoSeq.has_value()) {
        return std::nullopt;
    }
    return MsgAck{*conversation, *uptoSeq};
}

auto MsgSync::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"conversation", conversation},
        {"afterSeq", afterSeq},
    };
}

auto MsgSync::fromBody(const nlohmann::json &body,
                       std::vector<ValidationError> &errors)
    -> std::optional<MsgSync> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto conversation =
        requireNonEmptyString(body, "conversation", "body", errors);
    const auto afterSeq = requireInt64(body, "afterSeq", "body", errors);
    if (!conversation.has_value() || !afterSeq.has_value()) {
        return std::nullopt;
    }
    return MsgSync{*conversation, *afterSeq};
}

// --- file family ---

auto FileOffer::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"fileId", fileId},
        {"name", manifest.name},
        {"size", manifest.size},
        {"sha256", manifest.sha256Hex},
        {"chunkSize", manifest.chunkSize},
        {"chunkCount", manifest.chunkCount},
    };
}

auto FileOffer::fromBody(const nlohmann::json &body,
                         std::vector<ValidationError> &errors)
    -> std::optional<FileOffer> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto fileId = requireNonEmptyString(body, "fileId", "body", errors);
    const auto name = requireString(body, "name", "body", errors);
    const auto size = requireInt64(body, "size", "body", errors);
    const auto sha256 = requireString(body, "sha256", "body", errors);
    const auto chunkSize = requireInt64(body, "chunkSize", "body", errors);
    const auto chunkCount = requireInt64(body, "chunkCount", "body", errors);
    if (!callId.has_value() || !fileId.has_value() || !name.has_value() ||
        !size.has_value() || !sha256.has_value() || !chunkSize.has_value() ||
        !chunkCount.has_value()) {
        return std::nullopt;
    }
    // A manifest that cannot describe real chunks would hang a transfer
    // instead of failing fast: reject negatives, non-positive or oversized
    // chunk sizes, and a chunkCount that disagrees with ceil(size/chunkSize)
    // (same overflow-free arithmetic as makeManifest).
    if (*size < 0) {
        errors.push_back({"body.size", "must not be negative"});
        return std::nullopt;
    }
    if (*chunkSize <= 0 ||
        *chunkSize > std::numeric_limits<std::uint32_t>::max()) {
        errors.push_back({"body.chunkSize", "out of range"});
        return std::nullopt;
    }
    const std::int64_t expectedChunks =
        *size == 0 ? 0 : ((*size - 1) / *chunkSize) + 1;
    if (*chunkCount != expectedChunks) {
        errors.push_back(
            {"body.chunkCount", "inconsistent with size and chunkSize"});
        return std::nullopt;
    }
    return FileOffer{
        *callId,
        *fileId,
        FileManifest{
            *name,
            static_cast<std::uint64_t>(*size),
            *sha256,
            static_cast<std::uint32_t>(*chunkSize),
            static_cast<std::uint32_t>(*chunkCount),
        },
    };
}

auto FileAccept::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"fileId", fileId},
    };
}

auto FileAccept::fromBody(const nlohmann::json &body,
                          std::vector<ValidationError> &errors)
    -> std::optional<FileAccept> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto fileId = requireNonEmptyString(body, "fileId", "body", errors);
    if (!callId.has_value() || !fileId.has_value()) {
        return std::nullopt;
    }
    return FileAccept{*callId, *fileId};
}

auto FileCancel::toBody() const -> nlohmann::json {
    return nlohmann::json{
        {"callId", callId},
        {"fileId", fileId},
        {"reason", reason},
    };
}

auto FileCancel::fromBody(const nlohmann::json &body,
                          std::vector<ValidationError> &errors)
    -> std::optional<FileCancel> {
    if (!requireObject(body, "body", errors)) {
        return std::nullopt;
    }
    const auto callId = requireNonEmptyString(body, "callId", "body", errors);
    const auto fileId = requireNonEmptyString(body, "fileId", "body", errors);
    const auto reason = requireString(body, "reason", "body", errors);
    if (!callId.has_value() || !fileId.has_value() || !reason.has_value()) {
        return std::nullopt;
    }
    return FileCancel{*callId, *fileId, *reason};
}

} // namespace linko
