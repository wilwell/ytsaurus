#pragma once

#include "public.h"
#include "error.h"

#include <ytlib/rpc/rpc.pb.h>

#include <ytlib/bus/message.h>

namespace NYT {
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

NBus::IMessagePtr CreateRequestMessage(
    const NProto::TRequestHeader& header,
    TBlob&& body,
    const std::vector<TSharedRef>& attachments);

NBus::IMessagePtr CreateResponseMessage(
    const NProto::TResponseHeader& header,
    const TSharedRef& body,
    const std::vector<TSharedRef>& attachments);

NBus::IMessagePtr CreateErrorResponseMessage(
    const NProto::TResponseHeader& header);

NBus::IMessagePtr CreateErrorResponseMessage(
    const TRequestId& requestId,
    const TError& error);

NBus::IMessagePtr CreateErrorResponseMessage(
    const TError& error);

NProto::TRequestHeader GetRequestHeader(NBus::IMessagePtr message);
NBus::IMessagePtr SetRequestHeader(
    NBus::IMessagePtr message,
    const NProto::TRequestHeader& header);

NProto::TResponseHeader GetResponseHeader(NBus::IMessagePtr message);
NBus::IMessagePtr SetResponseHeader(
    NBus::IMessagePtr message,
    const NProto::TResponseHeader& header);

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
