package ru.yandex.yt.ytclient.proxy.request;

import javax.annotation.Nullable;

import ru.yandex.inside.yt.kosher.cypress.YPath;
import ru.yandex.lang.NonNullApi;

@NonNullApi
public abstract class GetLikeReq<T extends GetLikeReq<T>> extends TransactionalRequest<T> {
    protected final YPath path;
    @Nullable protected ColumnFilter attributes;
    @Nullable protected Integer maxSize;
    @Nullable protected MasterReadOptions masterReadOptions;
    @Nullable protected SuppressableAccessTrackingOptions suppressableAccessTrackingOptions;

    GetLikeReq(YPath path) {
        this.path = path.justPath();
    }

    public YPath getPath() {
        return path;
    }

    public T setAttributes(@Nullable ColumnFilter cf) {
        this.attributes = cf;
        return self();
    }

    public T setMaxSize(int maxSize) {
        this.maxSize = maxSize;
        return self();
    }

    public T setMasterReadOptions(@Nullable MasterReadOptions mo) {
        this.masterReadOptions = mo;
        return self();
    }

    public T setSuppressableAccessTrackingOptions(@Nullable SuppressableAccessTrackingOptions s) {
        this.suppressableAccessTrackingOptions = s;
        return self();
    }

    @Override
    protected void writeArgumentsLogString(StringBuilder sb) {
        sb.append("Path: ").append(path).append("; ");
        if (attributes != null) {
            sb.append("Attributes: ").append(attributes).append("; ");
        }
        super.writeArgumentsLogString(sb);
    }
}
