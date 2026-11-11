# Toolbox: every tool the demo runs, plus its own Docker daemon. tools/toolbox starts it as a
# long-running "lab" container and runs every make target inside it, so the host only needs
# Docker, make and git. The kind cluster's nodes are containers of the inner daemon.
#
# Docker-in-Docker rather than mounting the host's Docker socket: some Docker Desktop setups
# (Enhanced Container Isolation) refuse socket mounts and host networking, but allow a privileged
# dind. Every version comes from versions.env (passed as build args by tools/toolbox).
#
# TLS-intercepting proxies: a CA bundle (CA_BUNDLE, see tools/toolbox) is an optional build secret
# (`ca-bundle`) used only by the download steps, and a runtime mount (/etc/ca-bundle.pem) added to
# the trust store at start. It is never stored in an image layer. Without it, both are skipped.
ARG DIND_IMAGE
ARG KUBECTL_IMAGE
ARG YQ_IMAGE
ARG KYVERNO_CLI_IMAGE

FROM ${KUBECTL_IMAGE} AS kubectl
FROM ${YQ_IMAGE} AS yq
FROM ${KYVERNO_CLI_IMAGE} AS kyverno-cli

FROM ${DIND_IMAGE}
ARG KIND_VERSION
ARG HELM_VERSION
ARG TARGETARCH

# Alpine packages are current for these. kubectl, helm, yq and kind lag behind, so they come from
# upstream below.
RUN --mount=type=secret,id=ca-bundle,required=false \
    set -eu; \
    if [ -s /run/secrets/ca-bundle ]; then \
      cat /etc/ssl/certs/ca-certificates.crt /run/secrets/ca-bundle > /tmp/ca.crt; \
      export SSL_CERT_FILE=/tmp/ca.crt; \
    fi; \
    apk add --no-cache asciinema bash coreutils curl diffutils gawk git grep jq make ncurses \
      python3 sed shellcheck tar yamllint; \
    rm -f /tmp/ca.crt

# kind and helm from their releases, checked against the published checksums.
RUN --mount=type=secret,id=ca-bundle,required=false \
    set -eux; \
    if [ -s /run/secrets/ca-bundle ]; then \
      cat /etc/ssl/certs/ca-certificates.crt /run/secrets/ca-bundle > /tmp/ca.crt; \
      export CURL_CA_BUNDLE=/tmp/ca.crt; \
    fi; \
    base="https://github.com/kubernetes-sigs/kind/releases/download/${KIND_VERSION}/kind-linux-${TARGETARCH}"; \
    curl -fsSLo /usr/local/bin/kind "$base"; \
    echo "$(curl -fsSL "$base.sha256sum" | cut -d' ' -f1)  /usr/local/bin/kind" | sha256sum -c -; \
    chmod +x /usr/local/bin/kind; \
    tgz="helm-${HELM_VERSION}-linux-${TARGETARCH}.tar.gz"; \
    cd /tmp; \
    curl -fsSLO "https://get.helm.sh/$tgz"; \
    echo "$(curl -fsSL "https://get.helm.sh/$tgz.sha256sum" | cut -d' ' -f1)  $tgz" | sha256sum -c -; \
    tar -xzf "$tgz" --strip-components=1 -C /usr/local/bin "linux-${TARGETARCH}/helm"; \
    rm -f "/tmp/$tgz" /tmp/ca.crt

COPY --from=kubectl /bin/kubectl /usr/local/bin/kubectl
COPY --from=yq /usr/bin/yq /usr/local/bin/yq
COPY --from=kyverno-cli /ko-app/kubectl-kyverno /usr/local/bin/kyverno

# At start: trust the CA bundle if tools/toolbox mounted one, then start dockerd as usual.
COPY --chmod=0755 entrypoint.sh /usr/local/bin/lab-entrypoint.sh
ENTRYPOINT ["lab-entrypoint.sh"]
CMD []

# The clips print colors and clear the screen.
ENV TERM=xterm-256color
