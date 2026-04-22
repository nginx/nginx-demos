FROM ubuntu:24.04@sha256:66460d557b25769b102175144d538d88219c077c678a49af4afca6fbfc1b5252

# Create non-root user early so we can assign ownership during setup
#RUN groupadd --gid 990 nms && \
#    useradd --uid 990 --gid 990 --no-create-home -d /nonexistent --shell /bin/false nms

# Initial setup
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
        build-essential git nano curl jq wget gawk \
        nginx lsb-release rsyslog apt-transport-https \
        ca-certificates netcat-traditional sudo && \
    mkdir -p /etc/ssl/nginx /deployment

COPY ./container/startNIM.sh /deployment/

RUN --mount=type=secret,id=nginx-crt,dst=/etc/ssl/nginx/nginx-repo.crt,mode=0644 \
    --mount=type=secret,id=nginx-key,dst=/etc/ssl/nginx/nginx-repo.key,mode=0644 \
    set -x \
    && chmod +x /deployment/startNIM.sh \
    && printf "deb [signed-by=/usr/share/keyrings/nginx-archive-keyring.gpg] https://pkgs.nginx.com/nms/ubuntu $(lsb_release -cs) nginx-plus\n" \
        | tee /etc/apt/sources.list.d/nms.list > /dev/null \
    && wget -q -O /etc/apt/apt.conf.d/90pkgs-nginx https://cs.nginx.com/static/files/90pkgs-nginx \
    && wget -qO - https://cs.nginx.com/static/keys/nginx_signing.key \
        | gpg --dearmor \
        | tee /usr/share/keyrings/nginx-archive-keyring.gpg >/dev/null \
    && apt-get update \
    && apt-get install -y nms-instance-manager \
    && curl -s https://hg.nginx.org/nginx.org/raw-file/tip/xml/en/security_advisories.xml > /usr/share/nms/cve.xml \
    # Optional WAF Policy Compiler
    && LATEST_COMPILER=$(apt-cache search nms-nap-compiler | sort | tail -n1 | awk '{print $1}') \
    && apt-get -y install "$LATEST_COMPILER" \
    # Install yq
    && wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_$(dpkg --print-architecture) \
        -O /usr/bin/yq \
    && chmod +x /usr/bin/yq \
    && chmod +x /etc/nms/scripts/*.sh \
    # Allow binding privileged ports (80/443) without root.
    # Remove this block if your app uses only ports >= 1024.
    && apt-get install -y libcap2-bin \
    && setcap 'cap_net_bind_service=+ep' $(which nginx) \
    # Fix ownership of every directory the app reads/writes at runtime
    && chown -R nms:nms \
        /deployment \
        /etc/nms \
        /var/log/nms \
        /var/lib/nms \
        /usr/share/nms \
        /var/run/nms \
    && chown -R nms:nms /var/log/nginx /var/lib/nginx \
    # nginx master process writes its PID here
    && touch /run/nginx.pid && chown nms:nms /run/nginx.pid \
    # Clean up apt cache
    && rm -rf /var/lib/apt/lists/*

WORKDIR /deployment

# Drop to non-root for all subsequent layers and at runtime
USER nms

CMD ["/deployment/startNIM.sh"]
