package ru.yandex.yt.ytclient.request;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTreeBuilder;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.rpcproxy.TReqSuspendOperation;
import ru.yandex.yt.ytclient.proxy.request.HighLevelRequest;
import ru.yandex.yt.ytclient.rpc.RpcClientRequestBuilder;
import ru.yandex.yt.ytclient.rpc.RpcUtil;

/**
 * Request for suspending operation
 *
 * @see <a href="https://docs.yandex-team.ru/yt/api/commands#suspend_operation">
 *     suspend_operation documentation
 *     </a>
 * @see ResumeOperation
 */
@NonNullApi
@NonNullFields
public class SuspendOperation extends OperationReq<SuspendOperation.Builder, SuspendOperation>
        implements HighLevelRequest<TReqSuspendOperation.Builder> {
    private boolean abortRunningJobs;

    public SuspendOperation(BuilderBase<?, ?> builder) {
        super(builder);
        this.abortRunningJobs = builder.abortRunningJobs;
    }

    /**
     * Construct request from operation id.
     */
    public SuspendOperation(GUID operationId) {
        this(builder().setOperationId(operationId));
    }

    SuspendOperation(String operationAlias) {
        this(builder().setOperationAlias(operationAlias));
    }

    public static Builder builder() {
        return new Builder();
    }

    /**
     * Construct request from operation alias
     */
    public static SuspendOperation fromAlias(String operationAlias) {
        return new SuspendOperation(operationAlias);
    }

    /**
     * Abort running jobs
     */
    public SuspendOperation setAbortRunningJobs(boolean abortRunningJobs) {
        this.abortRunningJobs = abortRunningJobs;
        return this;
    }

    @Override
    public void writeTo(RpcClientRequestBuilder<TReqSuspendOperation.Builder, ?> builder) {
        TReqSuspendOperation.Builder messageBuilder = builder.body();
        if (operationId != null) {
            messageBuilder.setOperationId(RpcUtil.toProto(operationId));
        } else {
            assert operationAlias != null;
            messageBuilder.setOperationAlias(operationAlias);
        }
        if (abortRunningJobs) {
            messageBuilder.setAbortRunningJobs(abortRunningJobs);
        }
    }

    public YTreeBuilder toTree(YTreeBuilder builder) {
        super.toTree(builder);
        if (abortRunningJobs) {
            builder.key("abort_running_jobs").value(abortRunningJobs);
        }
        return builder;
    }

    @Override
    public Builder toBuilder() {
        return builder()
                .setOperationId(operationId)
                .setOperationAlias(operationAlias)
                .setAbortRunningJobs(abortRunningJobs)
                .setTimeout(timeout)
                .setRequestId(requestId)
                .setUserAgent(userAgent)
                .setTraceId(traceId, traceSampled)
                .setAdditionalData(additionalData);
    }

    public static class Builder extends BuilderBase<Builder, SuspendOperation> {
        @Override
        protected Builder self() {
            return this;
        }

        @Override
        public SuspendOperation build() {
            return new SuspendOperation(this);
        }
    }

    public abstract static class BuilderBase<
            TBuilder extends BuilderBase<TBuilder, TRequest>,
            TRequest extends OperationReq<?, TRequest>>
            extends OperationReq.Builder<TBuilder, TRequest>
            implements HighLevelRequest<TReqSuspendOperation.Builder> {
        private boolean abortRunningJobs;

        public BuilderBase() {
        }

        public BuilderBase(BuilderBase<?, ?> builder) {
            super(builder);
            this.abortRunningJobs = builder.abortRunningJobs;
        }

        /**
         * Abort running jobs
         */
        public TBuilder setAbortRunningJobs(boolean abortRunningJobs) {
            this.abortRunningJobs = abortRunningJobs;
            return self();
        }

        @Override
        public void writeTo(RpcClientRequestBuilder<TReqSuspendOperation.Builder, ?> builder) {
            TReqSuspendOperation.Builder messageBuilder = builder.body();
            if (operationId != null) {
                messageBuilder.setOperationId(RpcUtil.toProto(operationId));
            } else {
                assert operationAlias != null;
                messageBuilder.setOperationAlias(operationAlias);
            }
            if (abortRunningJobs) {
                messageBuilder.setAbortRunningJobs(abortRunningJobs);
            }
        }

        public YTreeBuilder toTree(YTreeBuilder builder) {
            super.toTree(builder);
            if (abortRunningJobs) {
                builder.key("abort_running_jobs").value(abortRunningJobs);
            }
            return builder;
        }
    }
}
