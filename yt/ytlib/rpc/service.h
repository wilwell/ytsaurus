#pragma once

#include "common.h"
#include "client.h"

#include "../logging/log.h"
#include "../misc/string.h"  // TODO: move to cpp

#include <util/generic/yexception.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

class TServiceException 
    : public yexception
{
public:
    TServiceException(EErrorCode errorCode = EErrorCode::ServiceError)
        : ErrorCode(errorCode)
        , ErrorCodeString(errorCode.ToString())
    { }

    TServiceException(EErrorCode errorCode, Stroka errorCodeString)
        : ErrorCode(errorCode)
        , ErrorCodeString(errorCodeString)
    { }

    EErrorCode GetErrorCode() const
    {
        return ErrorCode;
    }

    Stroka GetErrorCodeString() const
    {
        return ErrorCodeString;
    }

private:
    EErrorCode ErrorCode;
    Stroka ErrorCodeString;

};

////////////////////////////////////////////////////////////////////////////////

template<class TErrorCode>
class TTypedServiceException 
    : public TServiceException
{
public:
    TTypedServiceException(TErrorCode errorCode = (TErrorCode) EErrorCode::ServiceError)
        : TServiceException(errorCode, errorCode.ToString())
    { }

};

////////////////////////////////////////////////////////////////////////////////

class TServiceContext
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TServiceContext> TPtr;

    TServiceContext(
        TRequestId requestId,
        Stroka serviceName,
        Stroka methodName,
        IMessage::TPtr message,
        IBus::TPtr replyBus,
        NLog::TLogger& serviceLogger);
    
    void Reply(EErrorCode errorCode);

    TSharedRef GetRequestBody() const;
    void SetResponseBody(TBlob* responseBody);

    const yvector<TSharedRef>& GetRequestAttachments() const;
    void SetResponseAttachments(yvector<TSharedRef>* attachments);

    Stroka GetServiceName() const;
    Stroka GetMethodName() const;
    const TRequestId& GetRequestId() const;

    IBus::TPtr GetReplyBus() const;

    void SetRequestInfo(const Stroka& info)
    {
        RequestInfo = info;
        
        // TODO: move to a separate method
        Stroka str;
        AppendInfo(str, Sprintf("RequestId: %s", ~StringFromGuid(RequestId)));
        AppendInfo(str, RequestInfo);
        LOG_EVENT(
            ServiceLogger,
            NLog::ELogLevel::Debug,
            "%s <- %s",
            ~MethodName,
            ~str);
    }

    Stroka GetRequestInfo() const
    {
        return RequestInfo;
    }

    void SetResponseInfo(const Stroka& info)
    {
        ResponseInfo = info;
    }

    Stroka GetResponseInfo()
    {
        return ResponseInfo;
    }

    IAction::TPtr Wrap(IAction::TPtr action);

protected:
    enum EState
    {
        S_Received,
        S_Replied
    };

    TRequestId RequestId;
    Stroka ServiceName;
    Stroka MethodName;
    EState State;
    IBus::TPtr ReplyBus;
    TSharedRef RequestBody;
    yvector<TSharedRef> RequestAttachments;
    NLog::TLogger& ServiceLogger;

    TBlob ResponseBody;
    yvector<TSharedRef> ResponseAttachments;

    Stroka RequestInfo;
    Stroka ResponseInfo;

private:
    void WrapThunk(IAction::TPtr action) throw();

    static void AppendInfo(Stroka& lhs, Stroka rhs)
    {
        if (!rhs.Empty()) {
            if (!lhs.Empty()) {
                lhs.append(", ");
            }
            lhs.append(rhs);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

struct IService
    : public virtual TRefCountedBase
{
    typedef TIntrusivePtr<IService> TPtr;

    virtual Stroka GetServiceName() const = 0;
    virtual NLog::TLogger& GetLogger() = 0;
    virtual void OnRequest(TServiceContext::TPtr context) = 0;

    virtual ~IService()
    { }
};

template<class TRequestMessage, class TResponseMessage>
class TTypedServiceRequest
    : public TRequestMessage
    , private TNonCopyable
{
public:
    TTypedServiceRequest(const yvector<TSharedRef>& attachments)
        : Attachments_(attachments)
    { }

    yvector<TSharedRef>& Attachments()
    {
        return Attachments_;
    }

private:
    yvector<TSharedRef> Attachments_;

};

////////////////////////////////////////////////////////////////////////////////

template<class TRequestMessage, class TResponseMessage>
class TTypedServiceResponse
    : public TResponseMessage
    , private TNonCopyable
{
public:
    yvector<TSharedRef>& Attachments()
    {
        return Attachments_;
    }

private:
    yvector<TSharedRef> Attachments_;

};

////////////////////////////////////////////////////////////////////////////////

template<class TRequestMesssage, class TResponseMessage>
class TTypedServiceContext
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr< TTypedServiceContext<TRequestMesssage, TResponseMessage> > TPtr;
    typedef TTypedServiceRequest<TRequestMesssage, TResponseMessage> TTypedRequest;
    typedef TTypedServiceResponse<TRequestMesssage, TResponseMessage> TTypedResponse;

    TTypedServiceContext(TServiceContext::TPtr context)
        : Logger(TRpcManager::Get()->GetLogger())
        , Context(context)
        , Request_(context->GetRequestAttachments())
    {
        if (!DeserializeMessage(&Request_, context->GetRequestBody()))
            ythrow TServiceException(EErrorCode::ProtocolError) <<
                "Can't deserialize request body";
    }

    TTypedRequest& Request()
    {
        return Request_;
    }

    TTypedResponse& Response()
    {
        return Response_;
    }

    void Reply(EErrorCode errorCode = EErrorCode::OK)
    {
        TBlob responseData;
        if (!SerializeMessage(&Response_, &responseData)) {
            LOG_FATAL("Error serializing response");
        }
        Context->SetResponseBody(&responseData);
        Context->SetResponseAttachments(&Response_.Attachments());
        Context->Reply(errorCode);
    }

    IAction::TPtr Wrap(typename IParamAction<TPtr>::TPtr paramAction)
    {
        return Context->Wrap(paramAction->Bind(TPtr(this)));
    }
    
    void SetRequestInfo(const Stroka& info)
    {
        Context->SetRequestInfo(info);
    }

    void SetRequestInfo(const char* format, ...)
    {
        Stroka info;
        va_list params;
        va_start(params, format);
        vsprintf(info, format, params);
        va_end(params);
        Context->SetRequestInfo(info);
    }

    Stroka GetRequestInfo() const
    {
        return Context->GetRequestInfo();
    }

    void SetResponseInfo(const Stroka& info)
    {
        Context->SetResponse(info);
    }

    void SetResponseInfo(const char* format, ...)
    {
        Stroka info;
        va_list params;
        va_start(params, format);
        vsprintf(info, format, params);
        va_end(params);
        Context->SetResponseInfo(info);
    }

    Stroka GetResponseInfo()
    {
        return Context->GetResponseInfo();
    }

private:
    NLog::TLogger& Logger;
    TServiceContext::TPtr Context;
    TTypedRequest Request_;
    TTypedResponse Response_;

};

////////////////////////////////////////////////////////////////////////////////

class TServiceBase
    : public IService
{
protected:
    TServiceBase(Stroka serviceName, Stroka loggingCategory);

    virtual ~TServiceBase()
    { }

    typedef IParamAction<TServiceContext::TPtr> THandler;

    void RegisterHandler(Stroka methodName, THandler::TPtr handler);

    NLog::TLogger ServiceLogger;

private:
    typedef yhash_map<Stroka, THandler::TPtr> THandlerMap;

    Stroka ServiceName;
    THandlerMap Handlers;

    virtual void OnRequest(TServiceContext::TPtr context);

    virtual NLog::TLogger& GetLogger();
    virtual Stroka GetServiceName() const;

};

////////////////////////////////////////////////////////////////////////////////

#define RPC_SERVICE_METHOD_DECL(service, method) \
    typedef ::NYT::NRpc::TTypedServiceRequest<service::TReq##method, service::TRsp##method> TReq##method; \
    typedef ::NYT::NRpc::TTypedServiceResponse<service::TReq##method, service::TRsp##method> TRsp##method; \
    typedef ::NYT::NRpc::TTypedServiceContext<service::TReq##method, service::TRsp##method> TCtx##method; \
    \
    void method##Thunk(::NYT::NRpc::TServiceContext::TPtr context); \
    \
    void method( \
        TCtx##method::TTypedRequest* request, \
        TCtx##method::TTypedResponse* response, \
        TCtx##method::TPtr context)

#define RPC_SERVICE_METHOD_IMPL(type, method) \
    void type::method##Thunk(::NYT::NRpc::TServiceContext::TPtr context) \
    { \
        TCtx##method::TPtr typedContext = new TCtx##method(context); \
        method( \
            &typedContext->Request(), \
            &typedContext->Response(), \
            typedContext); \
    } \
    \
    void type::method( \
        TCtx##method::TTypedRequest* request, \
        TCtx##method::TTypedResponse* response, \
        TCtx##method::TPtr context)

#define RPC_REGISTER_METHOD(type, method) \
    RegisterHandler(#method, FromMethod(&type::method##Thunk, this))

#define USE_RPC_SERVICE_METHOD_LOGGER() \
    ::NYT::NLog::TPrefixLogger Logger( \
        ServiceLogger, \
        context->GetMethodName() + ": ");
        
////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
