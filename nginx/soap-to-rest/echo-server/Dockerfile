From ubuntu:22.04

RUN apt-get update \
	&& apt-get -y install python3 \
        && mkdir /deployment

COPY ./echo-server/echo-server.py /deployment

WORKDIR /deployment
CMD /deployment/echo-server.py
