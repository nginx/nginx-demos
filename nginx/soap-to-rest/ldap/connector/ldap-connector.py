#!/usr/bin/python3

import os
import secrets
import uvicorn
import sys

from fastapi import FastAPI, Depends, HTTPException, status, Response
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from typing_extensions import Annotated

from ldap3 import Server, Connection, SAFE_SYNC


app = FastAPI(openapi_url="/ldap/access/openapi.json", docs_url="/ldap/access/docs")
security = HTTPBasic()

def authorize(credentials: HTTPBasicCredentials = Depends(security)):

  #server = Server('ubuntu.ff.lan:389')
  server = Server(sys.argv[1])
  try:
    conn = Connection(server, f'uid={credentials.username},ou=users,dc=example,dc=org', credentials.password, client_strategy=SAFE_SYNC, auto_bind=True)
  except Exception:
    raise HTTPException(status_code=401, detail='Authentication failed')


@app.post('/ldap/auth', dependencies=[Depends(authorize)], status_code=status.HTTP_200_OK)
def auth(response: Response):
  return {'detail': 'Authentication successful'};


if __name__ == '__main__':
  if len(sys.argv) != 2:
    print('LDAP server missing')
  else:
    uvicorn.run("ldap-connector:app", host='0.0.0.0', port=5389)
