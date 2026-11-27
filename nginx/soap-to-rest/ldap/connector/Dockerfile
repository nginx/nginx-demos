FROM ubuntu:22.04@sha256:b000e69a50ff940390233025e7a337fb8469b509e2334fa4d018ac30f3e18cdb

ARG LDAP_SERVER
ENV LDAP_SERVER=$LDAP_SERVER

RUN apt-get update \
	&& apt-get -y install --no-install-recommends python3 python3-venv \
	&& rm -rf /var/lib/apt/lists/* \
	&& python3 -m venv /deployment/venv

COPY ./ldap/connector/requirements.txt /deployment/
RUN /deployment/venv/bin/pip install --no-cache-dir --require-hashes -r /deployment/requirements.txt

COPY ./ldap/connector/ldap-connector.py /deployment/

WORKDIR /deployment
CMD /deployment/venv/bin/python3 /deployment/ldap-connector.py $LDAP_SERVER
