FROM ubuntu:22.04@sha256:b000e69a50ff940390233025e7a337fb8469b509e2334fa4d018ac30f3e18cdb

RUN apt-get update \
	&& apt-get -y install --no-install-recommends python3 python3-venv \
	&& rm -rf /var/lib/apt/lists/* \
	&& python3 -m venv /deployment/venv

COPY ./source-of-truth/requirements.txt /deployment/
RUN /deployment/venv/bin/pip install --no-cache-dir --require-hashes -r /deployment/requirements.txt

COPY ./source-of-truth/source-of-truth.py /deployment/

WORKDIR /deployment
CMD ["/deployment/venv/bin/python3", "/deployment/source-of-truth.py"]
