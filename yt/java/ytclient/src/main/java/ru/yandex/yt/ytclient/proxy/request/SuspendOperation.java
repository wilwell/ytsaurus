package ru.yandex.yt.ytclient.proxy.request;

import ru.yandex.inside.yt.kosher.common.GUID;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;

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
public class SuspendOperation extends ru.yandex.yt.ytclient.request.SuspendOperation.BuilderBase<
        SuspendOperation, ru.yandex.yt.ytclient.request.SuspendOperation> {

    /**
     * Construct request from operation id.
     */
    public SuspendOperation(GUID operationId) {
        setOperationId(operationId);
    }

    SuspendOperation(String operationAlias) {
        setOperationAlias(operationAlias);
    }

    /**
     * Construct request from operation alias
     */
    public static SuspendOperation fromAlias(String operationAlias) {
        return new SuspendOperation(operationAlias);
    }

    @Override
    protected SuspendOperation self() {
        return this;
    }

    @Override
    public ru.yandex.yt.ytclient.request.SuspendOperation build() {
        return new ru.yandex.yt.ytclient.request.SuspendOperation(this);
    }
}
