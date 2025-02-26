#!/bin/bash

if [ -z $1 ]; then
  echo "You need to pass a step number"
  exit 1;
fi

if (($1 < 1 || $1 > 8)); then
  echo "Only steps 1 through 8 exist"
  exit 1;
fi

sudo cp -r step_$1/* /etc/nginx
sudo nginx -s reload

if [ $? = 0 ]; then
  echo "NGINX successfully reloaded"
fi

