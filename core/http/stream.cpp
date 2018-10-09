#include "stream.h"
#include "config.h"
#include "private.h"

#include <yt/core/net/connection.h>

#include <yt/core/misc/finally.h>

#include <util/generic/buffer.h>
#include <util/string/escape.h>

namespace NYT {
namespace NHttp {

using namespace NConcurrency;
using namespace NNet;

static const auto& Logger = HttpLogger;

////////////////////////////////////////////////////////////////////////////////

http_parser_settings THttpParser::GetParserSettings()
{
    http_parser_settings settings;
    yt_http_parser_settings_init(&settings);

    settings.on_url = &OnUrl;
    settings.on_status = &OnStatus;
    settings.on_header_field = &OnHeaderField;
    settings.on_header_value = &OnHeaderValue;
    settings.on_headers_complete = &OnHeadersComplete;
    settings.on_body = &OnBody;
    settings.on_message_complete = &OnMessageComplete;

    return settings;
}

const http_parser_settings ParserSettings = THttpParser::GetParserSettings();

THttpParser::THttpParser(http_parser_type parserType)
    : Headers_(New<THeaders>())
{
    yt_http_parser_init(&Parser_, parserType);
    Parser_.data = reinterpret_cast<void*>(this);
}

EParserState THttpParser::GetState() const
{
    return State_;
}

void THttpParser::Reset()
{
    Headers_ = New<THeaders>();
    Trailers_.Reset();

    ShouldKeepAlive_ = false;
    State_ = EParserState::Initialized;

    FirstLine_.Reset();
    NextField_.Reset();
    NextValue_.Reset();
    YCHECK(FirstLine_.GetLength() == 0);
    YCHECK(NextField_.GetLength() == 0);
    YCHECK(NextValue_.GetLength() == 0);
}

TSharedRef THttpParser::Feed(const TSharedRef& input)
{
    InputBuffer_ = &input;
    auto finally = Finally([&] {
        InputBuffer_ = nullptr;
    });

    size_t read = yt_http_parser_execute(&Parser_, &ParserSettings, input.Begin(), input.Size());
    auto http_errno = static_cast<enum http_errno>(Parser_.http_errno);
    if (http_errno != 0 && http_errno != HPE_PAUSED) {
        // 64 bytes before error
        size_t contextStart = read - std::min<size_t>(read, 64);

        // and 64 bytes after error
        size_t contextEnd = std::min(read + 64, input.Size());

        TString errorContext(input.Begin() + contextStart, contextEnd - contextStart);
    
        THROW_ERROR_EXCEPTION("HTTP parse error: %s", yt_http_errno_description(http_errno))
            << TErrorAttribute("parser_error_name", yt_http_errno_name(http_errno))
            << TErrorAttribute("error_context", EscapeC(errorContext));
    }

    if (http_errno == HPE_PAUSED) {
        yt_http_parser_pause(&Parser_, 0);
    }

    return input.Slice(read, input.Size());
}

std::pair<int, int> THttpParser::GetVersion() const
{
    return std::make_pair<int, int>(Parser_.http_major, Parser_.http_minor);
}

EStatusCode THttpParser::GetStatusCode() const
{
    return EStatusCode(Parser_.status_code);
}

EMethod THttpParser::GetMethod() const
{
    return EMethod(Parser_.method);
}

TString THttpParser::GetFirstLine()
{
    return FirstLine_.Flush();
}

const THeadersPtr& THttpParser::GetHeaders() const
{
    return Headers_;
}

const THeadersPtr& THttpParser::GetTrailers() const
{
    return Trailers_;
}

TSharedRef THttpParser::GetLastBodyChunk()
{
    auto chunk = LastBodyChunk_;
    LastBodyChunk_ = EmptySharedRef;
    return chunk;
}

bool THttpParser::ShouldKeepAlive() const
{
    return ShouldKeepAlive_;
}

void THttpParser::MaybeFlushHeader(bool trailer)
{
    if (NextField_.GetLength() == 0) {
        return;
    }

    if (trailer) {
        if (!Trailers_) {
            Trailers_ = New<THeaders>();
        }
        Trailers_->Set(NextField_.Flush(), NextValue_.Flush());
    } else {
        Headers_->Set(NextField_.Flush(), NextValue_.Flush());
    }     
}
    
int THttpParser::OnUrl(http_parser* parser, const char *at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->FirstLine_.AppendString(TStringBuf(at, length));

    return 0;
}

int THttpParser::OnStatus(http_parser* parser, const char *at, size_t length)
{
    return 0;
}

int THttpParser::OnHeaderField(http_parser* parser, const char *at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->NextField_.AppendString(TStringBuf(at, length));
    return 0;
}

int THttpParser::OnHeaderValue(http_parser* parser, const char *at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->NextValue_.AppendString(TStringBuf(at, length));
    
    return 0;
}

int THttpParser::OnHeadersComplete(http_parser* parser) 
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->State_ = EParserState::HeadersFinished;

    return 0;
}

int THttpParser::OnBody(http_parser* parser, const char *at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->LastBodyChunk_ = that->InputBuffer_->Slice(at, at + length);
    yt_http_parser_pause(parser, 1);
    return 0;
}

int THttpParser::OnMessageComplete(http_parser* parser)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->State_ = EParserState::MessageFinished;
    that->ShouldKeepAlive_ = yt_http_should_keep_alive(parser);
    yt_http_parser_pause(parser, 1);    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////

struct THttpParserTag
{ };

THttpInput::THttpInput(
    const IConnectionPtr& connection,
    const TNetworkAddress& remoteAddress,
    const IInvokerPtr& readInvoker,
    EMessageType messageType,
    const THttpIOConfigPtr& config)
    : Connection_(connection)
    , RemoteAddress_(remoteAddress)
    , MessageType_(messageType)
    , Config_(config)
    , InputBuffer_(TSharedMutableRef::Allocate<THttpParserTag>(Config_->ReadBufferSize))
    , Parser_(messageType == EMessageType::Request ? HTTP_REQUEST : HTTP_RESPONSE)
    , StartByteCount_(connection->GetReadByteCount())
    , ReadInvoker_(readInvoker)
{ }

std::pair<int, int> THttpInput::GetVersion()
{
    EnsureHeadersReceived();
    return Parser_.GetVersion();
}

EMethod THttpInput::GetMethod()
{
    YCHECK(MessageType_ == EMessageType::Request);

    EnsureHeadersReceived();
    return Parser_.GetMethod();
}

const TUrlRef& THttpInput::GetUrl()
{
    YCHECK(MessageType_ == EMessageType::Request);

    EnsureHeadersReceived();
    return Url_;
}

const THeadersPtr& THttpInput::GetHeaders()
{
    EnsureHeadersReceived();
    return Headers_;
}

EStatusCode THttpInput::GetStatusCode()
{
    EnsureHeadersReceived();
    return Parser_.GetStatusCode();
}

const THeadersPtr& THttpInput::GetTrailers()
{
    if (Parser_.GetState() != EParserState::MessageFinished) {
        THROW_ERROR_EXCEPTION("Cannot access trailers while body is not fully consumed");
    }

    const auto& trailers = Parser_.GetTrailers();
    if (!trailers) {
        static THeadersPtr emptyTrailers = New<THeaders>();
        return emptyTrailers;
    }
    return trailers;
}

const TNetworkAddress& THttpInput::GetRemoteAddress() const
{
    return RemoteAddress_;
}

TGuid THttpInput::GetConnectionId() const
{
    return ConnectionId_;
}

void THttpInput::SetConnectionId(TGuid connectionId)
{
    ConnectionId_ = connectionId;
}

TGuid THttpInput::GetRequestId() const
{
    return RequestId_;
}

void THttpInput::SetRequestId(TGuid requestId)
{
    RequestId_ = requestId;
}

bool THttpInput::IsSafeToReuse() const
{
    return SafeToReuse_;
}

void THttpInput::Reset()
{
    HeadersReceived_ = false;
    Headers_.Reset();
    Parser_.Reset();
    RawUrl_ = {};
    Url_ = {};
    SafeToReuse_ = false;

    StartByteCount_ = Connection_->GetReadByteCount();
}

void THttpInput::FinishHeaders()
{
    HeadersReceived_ = true;
    Headers_ = Parser_.GetHeaders();

    if (MessageType_ == EMessageType::Request) {
        RawUrl_ = Parser_.GetFirstLine();
        Url_ = ParseUrl(RawUrl_);
    }
}

void THttpInput::EnsureHeadersReceived()
{
    if (!ReceiveHeaders()) {
        THROW_ERROR_EXCEPTION("Connection was closed before the first byte of HTTP message");
    }
}

bool THttpInput::ReceiveHeaders()
{
    if (HeadersReceived_) {
        return true;
    }

    bool idleConnection = MessageType_ == EMessageType::Request;
    auto start = TInstant::Now();

    if (idleConnection) {
        Connection_->SetReadDeadline(start + Config_->ConnectionIdleTimeout);
    } else {
        Connection_->SetReadDeadline(start + Config_->HeaderReadTimeout);
    }

    while (true) {
        bool eof = false;
        if (UnconsumedData_.Empty()) {
            auto asyncRead = Connection_->Read(InputBuffer_);
            UnconsumedData_ = InputBuffer_.Slice(0, WaitFor(asyncRead).ValueOrThrow());
            eof = UnconsumedData_.Size() == 0;
        }

        UnconsumedData_ = Parser_.Feed(UnconsumedData_);
        if (Parser_.GetState() != EParserState::Initialized) {
            FinishHeaders();
            if (Parser_.GetState() == EParserState::MessageFinished) {
                SafeToReuse_ = Parser_.ShouldKeepAlive();
            }
            Connection_->SetReadDeadline({});
            return true;
        }

        // HTTP parser does not treat EOF at message start as error.
        if (eof) {
            return false;
        }

        if (idleConnection) {
            idleConnection = false;
            Connection_->SetReadDeadline(start + Config_->HeaderReadTimeout);
        }
    }
}

TFuture<TSharedRef> THttpInput::Read()
{
    return BIND(&THttpInput::DoRead, MakeStrong(this))
        .AsyncVia(ReadInvoker_)
        .Run();
}

TSharedRef THttpInput::ReadBody()
{
    std::vector<TSharedRef> chunks;

    // TODO(prime@): Add hard limit on body size.
    while (true) {
        auto chunk = WaitFor(Read())
            .ValueOrThrow();

        if (chunk.Empty()) {
            break;
        }

        chunks.emplace_back(TSharedRef::MakeCopy<THttpParserTag>(chunk));
    }

    return MergeRefsToRef<THttpParserTag>(std::move(chunks));
}

i64 THttpInput::GetReadByteCount() const
{
    return Connection_->GetReadByteCount() - StartByteCount_;
}

TSharedRef THttpInput::DoRead()
{
    if (Parser_.GetState() == EParserState::MessageFinished) {
        return EmptySharedRef;
    }

    Connection_->SetReadDeadline(TInstant::Now() + Config_->BodyReadIdleTimeout);
    while (true) {
        auto chunk = Parser_.GetLastBodyChunk();
        if (!chunk.Empty()) {
            Connection_->SetReadDeadline({});
            return chunk;
        }

        bool eof = false;
        if (UnconsumedData_.Empty()) {
            auto asyncRead = Connection_->Read(InputBuffer_);
            UnconsumedData_ = InputBuffer_.Slice(0, WaitFor(asyncRead).ValueOrThrow());
            eof = UnconsumedData_.Size() == 0;
        }

        UnconsumedData_ = Parser_.Feed(UnconsumedData_);
        if (Parser_.GetState() == EParserState::MessageFinished) {
            SafeToReuse_ = Parser_.ShouldKeepAlive();
            Connection_->SetReadDeadline({});

            if (MessageType_ == EMessageType::Request) {
                LOG_DEBUG("Finished reading HTTP request body (RequestId: %v, BytesIn: %d)",
                    RequestId_,
                    GetReadByteCount());
            }
            return EmptySharedRef;
        }

        // EOF must be handled by HTTP parser.
        YCHECK(!eof);
    }    
}

////////////////////////////////////////////////////////////////////////////////

THttpOutput::THttpOutput(
    const THeadersPtr& headers,
    const IConnectionPtr& connection,
    EMessageType messageType,
    const THttpIOConfigPtr& config)
    : Connection_(connection)
    , MessageType_(messageType)
    , Config_(config)
    , OnWriteFinish_(BIND(&THttpOutput::OnWriteFinish, MakeWeak(this)))
    , StartByteCount_(connection->GetWriteByteCount())
    , Headers_(headers)
{ }

THttpOutput::THttpOutput(
    const IConnectionPtr& connection,
    EMessageType messageType,
    const THttpIOConfigPtr& config)
    : THttpOutput(New<THeaders>(), connection, messageType, config)
{ }

const THeadersPtr& THttpOutput::GetHeaders()
{
    return Headers_;
}

void THttpOutput::SetHost(TStringBuf host, TStringBuf port)
{
    if (!port.empty()) {
        HostHeader_ = Format("%v:%v", host, port);
    } else {
        HostHeader_ = TString(host);
    }
}

void THttpOutput::SetHeaders(const THeadersPtr& headers)
{
    Headers_ = headers;
}

bool THttpOutput::IsHeadersFlushed() const
{
    return HeadersFlushed_;
}

const THeadersPtr& THttpOutput::GetTrailers()
{
    if (!Trailers_) {
        Trailers_ = New<THeaders>();
    }
    return Trailers_;
}

void THttpOutput::AddConnectionCloseHeader()
{
    YCHECK(MessageType_ == EMessageType::Response);
    ConnectionClose_ = true;
}

bool THttpOutput::IsSafeToReuse() const
{
    return MessageFinished_ && !ConnectionClose_;
}

void THttpOutput::Reset()
{
    StartByteCount_ = Connection_->GetWriteByteCount();
    HeadersLogged_ = false;

    ConnectionClose_ = false;
    Headers_ = New<THeaders>();

    Status_.Reset();
    Method_.Reset();
    HostHeader_.Reset();
    Path_ = {};

    HeadersFlushed_ = false;
    MessageFinished_ = false;

    Trailers_.Reset();
}

void THttpOutput::SetConnectionId(TGuid connectionId)
{
    ConnectionId_ = connectionId;
}

void THttpOutput::SetRequestId(TGuid requestId)
{
    RequestId_ = requestId;
}

void THttpOutput::WriteRequest(EMethod method, const TString& path)
{
    YCHECK(MessageType_ == EMessageType::Request);

    Method_ = method;
    Path_ = path;
}

TNullable<EStatusCode> THttpOutput::GetStatus() const
{
    return Status_;
}

void THttpOutput::SetStatus(EStatusCode status)
{
    YCHECK(MessageType_ == EMessageType::Response);

    Status_ = status;
}

TSharedRef THttpOutput::GetHeadersPart(TNullable<size_t> contentLength)
{
    TBufferOutput messageHeaders;
    if (MessageType_ == EMessageType::Request) {
        YCHECK(Method_);

        messageHeaders << ToHttpString(*Method_) << " " << Path_ << " HTTP/1.1\r\n";
    } else {
        if (!Status_) {
            Status_ = EStatusCode::OK;
        }

        messageHeaders << "HTTP/1.1 " << static_cast<int>(*Status_) << " " << ToHttpString(*Status_) << "\r\n";
    }

    bool methodNeedsContentLength = Method_ && *Method_ != EMethod::Get && *Method_ != EMethod::Head;

    if (contentLength) {
        if (MessageType_ == EMessageType::Response ||
            (MessageType_ == EMessageType::Request && methodNeedsContentLength)) {
            messageHeaders << "Content-Length: " << *contentLength << "\r\n";
        }
    } else {
        messageHeaders << "Transfer-Encoding: chunked\r\n";
    }

    if (ConnectionClose_) {
        messageHeaders << "Connection: close\r\n";
    }

    if (HostHeader_) {
        messageHeaders << "Host: " << *HostHeader_ << "\r\n";
    }

    Headers_->WriteTo(&messageHeaders, &FilteredHeaders_);

    TString headers;
    messageHeaders.Buffer().AsString(headers);
    return TSharedRef::FromString(headers);
}

TSharedRef THttpOutput::GetTrailersPart()
{
    TBufferOutput messageTrailers;

    Trailers_->WriteTo(&messageTrailers, &FilteredHeaders_);

    TString trailers;
    messageTrailers.Buffer().AsString(trailers);
    return TSharedRef::FromString(trailers);
}

TSharedRef THttpOutput::GetChunkHeader(size_t size)
{
    return TSharedRef::FromString(Format("%X\r\n", size));
}

TFuture<void> THttpOutput::Write(const TSharedRef& data)
{
    if (MessageFinished_) {
        THROW_ERROR_EXCEPTION("Cannot write to finished HTTP message");
    }

    std::vector<TSharedRef> writeRefs;
    if (!HeadersFlushed_) {
        HeadersFlushed_ = true;
        writeRefs.emplace_back(GetHeadersPart(Null));
        writeRefs.emplace_back(CrLf);
    }

    if (data.Size() != 0) {
        writeRefs.emplace_back(GetChunkHeader(data.Size()));
        writeRefs.emplace_back(data);
        writeRefs.push_back(CrLf);
    }

    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(TSharedRefArray(std::move(writeRefs)))
        .Apply(OnWriteFinish_);
}

TFuture<void> THttpOutput::Close()
{
    if (MessageFinished_) {
        return VoidFuture;
    }

    if (!HeadersFlushed_) {
        return WriteBody(EmptySharedRef);
    }

    return FinishChunked();
}

TFuture<void> THttpOutput::FinishChunked()
{
    std::vector<TSharedRef> writeRefs;
    
    if (Trailers_) {
        writeRefs.emplace_back(ZeroCrLf);
        writeRefs.emplace_back(GetTrailersPart());
        writeRefs.emplace_back(CrLf);
    } else {
        writeRefs.emplace_back(ZeroCrLfCrLf);
    }

    MessageFinished_ = true;
    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(TSharedRefArray(std::move(writeRefs)))
        .Apply(OnWriteFinish_);
}

TFuture<void> THttpOutput::WriteBody(const TSharedRef& smallBody)
{
    YCHECK(!HeadersFlushed_ && !MessageFinished_);

    TSharedRefArray writeRefs;
    if (Trailers_) {        
        writeRefs = TSharedRefArray({
            GetHeadersPart(smallBody.Size()),
            GetTrailersPart(),
            CrLf,
            smallBody
        });
    } else {
        writeRefs = TSharedRefArray({
            GetHeadersPart(smallBody.Size()),
            CrLf,
            smallBody
        });
    }

    HeadersFlushed_ = true;
    MessageFinished_ = true;
    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(writeRefs)
        .Apply(OnWriteFinish_);
}

i64 THttpOutput::GetWriteByteCount() const
{
    return Connection_->GetWriteByteCount() - StartByteCount_;
}

void THttpOutput::OnWriteFinish()
{
    Connection_->SetWriteDeadline({});

    if (MessageType_ == EMessageType::Response) {
        if (HeadersFlushed_ && !HeadersLogged_) {
            HeadersLogged_ = true;
            LOG_DEBUG("Finished writing HTTP headers (RequestId: %v, StatusCode: %v)",
                RequestId_,
                Status_);
        }

        if (MessageFinished_) {
            LOG_DEBUG("Finished writing HTTP response (RequestId: %v, BytesOut: %d)",
                RequestId_,
                GetWriteByteCount());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

const THashSet<TString> THttpOutput::FilteredHeaders_ = {
    "transfer-encoding",
    "content-length",
    "connection",
    "host"
};

const TSharedRef THttpOutput::CrLf = TSharedRef::FromString("\r\n");
const TSharedRef THttpOutput::ZeroCrLf = TSharedRef::FromString("0\r\n");
const TSharedRef THttpOutput::ZeroCrLfCrLf = TSharedRef::FromString("0\r\n\r\n");

////////////////////////////////////////////////////////////////////////////////

} // namespace NHttp
} // namespace NYT
