FROM ubuntu:latest@sha256:b59d21599a2b151e23eea5f6602f4af4d7d31c4e236d22bf0b62b86d2e386b8f
RUN apt-get update

RUN apt-get install -y -q build-essential python3-pip python3-dev python3-simplejson git
RUN pip3 install --upgrade pip
RUN pip3 install --upgrade virtualenv

RUN mkdir deployment
RUN virtualenv /deployment/env/
RUN /deployment/env/bin/python -m pip install --upgrade pip
RUN /deployment/env/bin/pip3 install flask
WORKDIR /deployment
COPY app.py .

CMD env/bin/python3 app.py
