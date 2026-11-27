FROM ubuntu:22.04@sha256:b000e69a50ff940390233025e7a337fb8469b509e2334fa4d018ac30f3e18cdb

RUN apt-get update \
	&& apt-get -y install python3 \
        && mkdir /deployment

COPY ./echo-server/echo-server.py /deployment

WORKDIR /deployment
CMD /deployment/echo-server.py
