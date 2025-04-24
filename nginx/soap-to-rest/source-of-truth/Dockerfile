From ubuntu:22.04

RUN apt-get update \
	&& apt-get -y install python3 python3-venv python3-pip python3-openssl libsasl2-dev libldap2-dev libssl-dev \
        && mkdir /deployment

COPY ./source-of-truth/source-of-truth.py /deployment
COPY ./source-of-truth/requirements.txt /deployment

RUN pip install --no-cache -r /deployment/requirements.txt

WORKDIR /deployment
CMD /deployment/source-of-truth.py
