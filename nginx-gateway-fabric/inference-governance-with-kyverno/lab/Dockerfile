# Toolbox: every tool the demo runs, plus its own Docker daemon. lab/toolbox starts it as a
# long-running "lab" container and runs every make target inside it, so the host only needs
# Docker, make and git. The kind cluster's nodes are containers of the inner daemon.
#
# Docker-in-Docker rather than mounting the host's Docker socket: some Docker Desktop setups
# (Enhanced Container Isolation) refuse socket mounts and host networking, but allow a privileged
# dind. Every version comes from versions.env (passed as build args by lab/toolbox).
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
RUN apk add --no-cache asciinema bash coreutils curl diffutils gawk git grep jq make ncurses \
      python3 sed shellcheck tar yamllint

# kind and helm from their releases, checked against the published checksums.
RUN set -eux; \
    base="https://github.com/kubernetes-sigs/kind/releases/download/${KIND_VERSION}/kind-linux-${TARGETARCH}"; \
    curl -fsSLo /usr/local/bin/kind "$base"; \
    echo "$(curl -fsSL "$base.sha256sum" | cut -d' ' -f1)  /usr/local/bin/kind" | sha256sum -c -; \
    chmod +x /usr/local/bin/kind; \
    tgz="helm-${HELM_VERSION}-linux-${TARGETARCH}.tar.gz"; \
    cd /tmp; \
    curl -fsSLO "https://get.helm.sh/$tgz"; \
    echo "$(curl -fsSL "https://get.helm.sh/$tgz.sha256sum" | cut -d' ' -f1)  $tgz" | sha256sum -c -; \
    tar -xzf "$tgz" --strip-components=1 -C /usr/local/bin "linux-${TARGETARCH}/helm"; \
    rm -f "/tmp/$tgz"

COPY --from=kubectl /bin/kubectl /usr/local/bin/kubectl
COPY --from=yq /usr/bin/yq /usr/local/bin/yq
COPY --from=kyverno-cli /ko-app/kubectl-kyverno /usr/local/bin/kyverno

# The clips print colors and clear the screen.
ENV TERM=xterm-256color

# The inner Docker daemon listens on its unix socket only: every docker call goes through it
# (`docker exec <lab> docker ...`, and kind). With no command, the base image's entrypoint would
# also open TCP 2375 (or 2376 with TLS) on every address in the lab, reachable from kind's pods.
# Naming dockerd here keeps the entrypoint's init and iptables setup, without the TCP listener.
CMD ["dockerd", "--host=unix:///var/run/docker.sock"]
