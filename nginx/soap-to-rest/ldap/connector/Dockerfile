From ubuntu:22.04

ARG LDAP_SERVER

RUN apt-get update \
#	&& apt-get -y install python3 python3-venv python3-pip python3-openssl libsasl2-dev libldap2-dev libssl-dev \
	&& apt-get -y install python3 python3-pip python3-openssl libsasl2-dev libldap2-dev libssl-dev \
        && mkdir /deployment

COPY ./ldap/connector/ldap-connector.py /deployment
COPY ./ldap/connector/requirements.txt /deployment

RUN pip install --no-cache -r /deployment/requirements.txt

WORKDIR /deployment
CMD /deployment/ldap-connector.py $LDAP_SERVER
