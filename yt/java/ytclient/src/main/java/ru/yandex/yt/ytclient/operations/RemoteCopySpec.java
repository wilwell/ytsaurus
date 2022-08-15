package ru.yandex.yt.ytclient.operations;

import java.util.Objects;

import javax.annotation.Nullable;

import ru.yandex.inside.yt.kosher.cypress.CypressNodeType;
import ru.yandex.inside.yt.kosher.cypress.YPath;
import ru.yandex.inside.yt.kosher.impl.ytree.builder.YTreeBuilder;
import ru.yandex.lang.NonNullApi;
import ru.yandex.lang.NonNullFields;
import ru.yandex.yt.ytclient.proxy.TransactionalClient;
import ru.yandex.yt.ytclient.proxy.request.CreateNode;

@NonNullApi
@NonNullFields
public class RemoteCopySpec extends SystemOperationSpecBase implements Spec {
    private final String cluster;
    private final @Nullable String network;
    private final @Nullable Boolean copyAttributes;

    public RemoteCopySpec(YPath source, YPath destination, String cluster) {
        this(builder()
                .setInputTables(source)
                .setOutputTable(destination)
                .setCluster(cluster)
        );
    }

    protected <T extends BuilderBase<T>> RemoteCopySpec(BuilderBase<T> builder) {
        super(builder);
        this.cluster = Objects.requireNonNull(builder.cluster);
        this.network = builder.network;
        this.copyAttributes = builder.copyAttributes;

        Objects.requireNonNull(this.cluster);
    }

    public static BuilderBase<?> builder() {
        return new Builder();
    }

    @Override
    public YTreeBuilder prepare(YTreeBuilder builder, TransactionalClient yt, SpecPreparationContext context) {
        yt.createNode(new CreateNode(getOutputTable(), CypressNodeType.TABLE, getOutputTableAttributes())
                .setRecursive(true)
                .setIgnoreExisting(true)
        );
        return builder.beginMap()
                .apply(super::toTree)
                .key("cluster_name").value(cluster)
                .when(network != null, b -> b.key("network_name").value(network))
                .when(copyAttributes != null, b -> b.key("copy_attributes").value(copyAttributes))
                .endMap();
    }

    protected static class Builder extends BuilderBase<Builder> {
        @Override
        protected Builder self() {
            return this;
        }
    }

    // BuilderBase was taken out because there is another client
    // which we need to support too and which use the same RemoteCopySpec class.
    @NonNullApi
    @NonNullFields
    public abstract static class BuilderBase<T extends BuilderBase<T>> extends SystemOperationSpecBase.Builder<T> {
        private @Nullable String cluster;
        private @Nullable String network;
        private @Nullable Boolean copyAttributes;

        public RemoteCopySpec build() {
            return new RemoteCopySpec(this);
        }

        public T setCluster(String cluster) {
            this.cluster = cluster;
            return self();
        }

        public T setNetwork(@Nullable String network) {
            this.network = network;
            return self();
        }

        public T setCopyAttributes(@Nullable Boolean copyAttributes) {
            this.copyAttributes = copyAttributes;
            return self();
        }
    }
}
