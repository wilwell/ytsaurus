#include "arguments.h"

#include <build.h>

namespace NYT {

using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

TArgsBase::TArgsBase()
    : CmdLine("Command line", ' ', YT_VERSION)
    , ConfigArg("", "config", "configuration file", false, "", "file_name")
    , OutputFormatArg("", "format", "output format", false, TFormat(EYsonFormat::Text), "text, pretty, binary")
    , ConfigUpdatesArg("", "config_set", "set configuration value", false, "ypath=yson")
    , OptsArg("", "opts", "other options", false, "key=yson")
{ }

void TArgsBase::Parse(std::vector<std::string>& args)
{
    CmdLine.parse(args);
}

INodePtr TArgsBase::GetCommand()
{
    auto builder = CreateBuilderFromFactory(GetEphemeralNodeFactory());
    builder->BeginTree();
    builder->OnBeginMap();
    BuildCommand(~builder);
    builder->OnEndMap();
    return builder->EndTree();
}

Stroka TArgsBase::GetConfigName()
{
    return Stroka(ConfigArg.getValue());
}

TArgsBase::TFormat TArgsBase::GetOutputFormat()
{
    return OutputFormatArg.getValue();
}

void TArgsBase::ApplyConfigUpdates(NYTree::IYPathServicePtr service)
{
    FOREACH (auto updateString, ConfigUpdatesArg.getValue()) {
        int index = updateString.find_first_of('=');
        auto ypath = updateString.substr(0, index);
        auto yson = updateString.substr(index + 1);
        SyncYPathSet(service, ypath, yson);
    }

}

void TArgsBase::BuildOptions(IYsonConsumer* consumer, TCLAP::MultiArg<Stroka>& arg)
{
    // TODO(babenko): think about a better way of doing this
    FOREACH (const auto& opts, arg.getValue()) {
        NYTree::TYson yson = Stroka("{") + Stroka(opts) + "}";
        auto items = NYTree::DeserializeFromYson(yson)->AsMap();
        FOREACH (const auto& pair, items->GetChildren()) {
            consumer->OnMapItem(pair.first);
            VisitTree(pair.second, consumer, true);
        }
    }
}

void TArgsBase::BuildCommand(IYsonConsumer* consumer)
{
    BuildOptions(consumer, OptsArg);
}

////////////////////////////////////////////////////////////////////////////////

TTransactedArgs::TTransactedArgs()
    : TxArg("", "tx", "set transaction id", false, NObjectServer::NullTransactionId, "transaction_id")
{ }

void TTransactedArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("transaction_id").Scalar(TxArg.getValue());
    TArgsBase::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TGetArgs::TGetArgs()
    : PathArg("path", "path to an object in Cypress that must be retrieved", true, "", "path")
{ }

void TGetArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("get")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TSetArgs::TSetArgs()
    : PathArg("path", "path to an object in Cypress that must be set", true, "", "path")
    , ValueArg("value", "value to set", true, "", "yson")
{ }

void TSetArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("set")
        .Item("path").Scalar(PathArg.getValue())
        .Item("value").Node(ValueArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TRemoveArgs::TRemoveArgs()
    : PathArg("path", "path to an object in Cypress that must be removed", true, "", "path")
{ }

void TRemoveArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("remove")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TListArgs::TListArgs()
    : PathArg("path", "path to a object in Cypress whose children must be listed", true, "", "path")
{ }

void TListArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("list")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TCreateArgs::TCreateArgs()
    : PathArg("path", "path for a new object in Cypress", true, "", "ypath")
    , TypeArg("type", "type of node", true, NObjectServer::EObjectType::Undefined, "object type")
    , ManifestArg("", "manifest", "manifest", false, "", "yson")
{ }

void TCreateArgs::BuildCommand(IYsonConsumer* consumer)
{
    auto manifestYson = ManifestArg.getValue();

    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("create")
        .Item("path").Scalar(PathArg.getValue())
        .Item("type").Scalar(TypeArg.getValue().ToString())
        .DoIf(!manifestYson.empty(), [=] (TFluentMap fluent) {
            fluent.Item("manifest").Node(manifestYson);
         });

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TLockArgs::TLockArgs()
    : PathArg("path", "path to an object in Cypress that must be locked", true, "", "path")
    , ModeArg("", "mode", "lock mode", false, NCypress::ELockMode::Exclusive, "snapshot, shared, exclusive")
{ }

void TLockArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("lock")
        .Item("path").Scalar(PathArg.getValue())
        .Item("mode").Scalar(ModeArg.getValue().ToString());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TStartTxArgs::TStartTxArgs()
    : ManifestArg("", "manifest", "manifest", false, "", "yson")
{ }

void TStartTxArgs::BuildCommand(IYsonConsumer* consumer)
{
    auto manifestYson = ManifestArg.getValue();
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("start")
        .DoIf(!manifestYson.empty(), [=] (TFluentMap fluent) {
            fluent.Item("manifest").Node(manifestYson);
         });
    TArgsBase::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

void TCommitTxArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("commit");

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

void TAbortTxArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("abort");

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TReadArgs::TReadArgs()
    : PathArg("path", "path to a table in Cypress that must be read", true, "", "ypath")
{ }

void TReadArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("read")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TWriteArgs::TWriteArgs()
    : PathArg("path", "path to a table in Cypress that must be written", true, "", "ypath")
    , ValueArg("value", "row(s) to write", true, "", "yson")
{ }

    // TODO(panin): validation?
//    virtual void DoValidate() const
//    {
//        if (Value) {
//            auto type = Value->GetType();
//            if (type != NYTree::ENodeType::List && type != NYTree::ENodeType::Map) {
//                ythrow yexception() << "\"value\" must be a list or a map";
//            }
//        }
//    }

void TWriteArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("write")
        .Item("path").Scalar(PathArg.getValue())
        .Item("value").Node(ValueArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TUploadArgs::TUploadArgs()
    : PathArg("path", "to a new file in Cypress that must be uploaded", true, "", "ypath")
{ }

void TUploadArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("upload")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TDownloadArgs::TDownloadArgs()
    : PathArg("path", "path to a file in Cypress that must be downloaded", true, "", "ypath")
{ }

void TDownloadArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("download")
        .Item("path").Scalar(PathArg.getValue());

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

TMapArgs::TMapArgs()
    : InArg("", "in", "input tables", false, "ypath")
    , OutArg("", "out", "output tables", false, "ypath")
    , FilesArg("", "file", "additional files", false, "ypath")
    , ShellCommandArg("", "command", "shell command", true, "", "path")
{ }

void TMapArgs::BuildCommand(IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("do").Scalar("map")
        .Item("spec").BeginMap()
            .Item("shell_command").Scalar(ShellCommandArg.getValue())
            .Item("in").List(InArg.getValue())
            .Item("out").List(OutArg.getValue())
            .Item("files").List(FilesArg.getValue())
        .EndMap();

    TTransactedArgs::BuildCommand(consumer);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
